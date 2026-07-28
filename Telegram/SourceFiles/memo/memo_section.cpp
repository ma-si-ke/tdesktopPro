/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "memo/memo_section.h"

#include "base/call_delayed.h"
#include "boxes/delete_messages_box.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "data/data_memo_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_list_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "memo/memo_fill.h"
#include "menu/menu_send.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/sub_tabs.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_memo.h"

namespace Memo {

using namespace HistoryView;

namespace {

[[nodiscard]] QString FolderTabId(uint64 folderId) {
	return QString::number(folderId, 16);
}

[[nodiscard]] uint64 FolderFromTabId(const QString &id) {
	return id.toULongLong(nullptr, 16);
}

constexpr auto kNewFolderTabId = "+";

} // namespace

object_ptr<Window::SectionWidget> MemoMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto result = object_ptr<MemoSection>(parent, controller);
	result->setGeometry(geometry);
	return result;
}

MemoSection::MemoSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, WindowListDelegate(controller)
, _show(controller->uiShow())
, _history(controller->session().data().memoMessages().history())
, _tabs(this, st::memoTabs)
, _tabsShadow(this)
, _scroll(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll))
, _composeControls(std::make_unique<ComposeControls>(
	this,
	ComposeControlsDescriptor{
		.stOverride = &st::memoComposeControls,
		.show = controller->uiShow(),
		.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
			listShowPremiumToast(emoji);
		},
		.mode = ComposeControlsMode::Normal,
		.sendMenuDetails = [] { return SendMenu::Details(); },
		.regularWindow = controller,
		.stickerOrEmojiChosen = controller->stickerOrEmojiChosen(),
		.customPlaceholder = tr::lng_memo_placeholder(),
		.panelsLevel = Window::GifPauseReason::Layer,
		.features = {
			.sendAs = false,
			.ttlInfo = false,
			.botCommandSend = false,
			.silentBroadcastToggle = false,
			.attachBotsMenu = false,
			.inlineBots = false,
			.megagroupSet = false,
			.autocompleteHashtags = false,
			.autocompleteMentions = false,
			.autocompleteCommands = false,
			.commonTabbedPanel = false,
		},
	})) {
	setAttribute(Qt::WA_OpaquePaintEvent, true);

	_theme = Window::Theme::DefaultChatThemeOn(lifetime());
	controller->setChatStyleTheme(_theme);

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_inner->overrideChatMode(ElementChatMode::Default);

	_scroll->scrolls(
	) | rpl::on_next([=] {
		updateInnerVisibleArea();
	}, lifetime());

	_inner->editMessageRequested(
	) | rpl::on_next([=](FullMsgId fullId) {
		if (const auto item = session().data().message(fullId)) {
			edit(item);
		}
	}, _inner->lifetime());

	setupTabs();
	setupComposeControls();

	_folderId = session().data().memoMessages().defaultFolder();
	refreshTabs();
	showAtEnd();
}

MemoSection::~MemoSection() = default;

void MemoSection::setupTabs() {
	_tabs->activated(
	) | rpl::on_next([=](const QString &id) {
		if (id == QString(kNewFolderTabId)) {
			_tabs->setActiveTab(FolderTabId(_folderId.current()));
			promptNewFolder();
			return;
		}
		const auto folderId = FolderFromTabId(id);
		if (folderId && folderId != _folderId.current()) {
			_folderId = folderId;
			showAtEnd();
		}
	}, _tabs->lifetime());

	_tabs->contextMenuRequests(
	) | rpl::on_next([=](const QString &id) {
		if (const auto folderId = FolderFromTabId(id)) {
			showFolderMenu(folderId);
		}
	}, _tabs->lifetime());

	_tabs->setReorderEnabled(true);
	_tabs->reorderUpdates(
	) | rpl::filter([](const Ui::SubTabsReorderUpdate &update) {
		return (update.state == Ui::SubTabsReorderUpdate::State::Applied);
	}) | rpl::on_next([=](const Ui::SubTabsReorderUpdate &update) {
		auto order = std::vector<uint64>();
		const auto &folders = session().data().memoMessages().folders();
		order.reserve(folders.size());
		for (const auto &folder : folders) {
			order.push_back(folder.id);
		}
		if (update.oldPosition < int(order.size())
			&& update.newPosition < int(order.size())) {
			const auto moved = order[update.oldPosition];
			order.erase(begin(order) + update.oldPosition);
			order.insert(begin(order) + update.newPosition, moved);
			session().data().memoMessages().applyFolderOrder(order);
		}
	}, _tabs->lifetime());

	session().data().memoMessages().foldersChanged(
	) | rpl::on_next([=] {
		refreshTabs();
	}, lifetime());
}

void MemoSection::refreshTabs() {
	const auto memo = &session().data().memoMessages();
	const auto &folders = memo->folders();
	auto tabs = std::vector<Ui::SubTabs::Tab>();
	tabs.reserve(folders.size() + 1);
	for (const auto &folder : folders) {
		tabs.push_back({
			.id = FolderTabId(folder.id),
			.text = { folder.title },
		});
	}
	tabs.push_back({
		.id = QString(kNewFolderTabId),
		.text = { u"+"_q },
	});
	const auto newFolderIndex = int(folders.size());
	_tabs->clearPinnedIntervals();
	_tabs->setPinnedInterval(newFolderIndex, newFolderIndex + 1);
	_tabs->setTabs(std::move(tabs));

	if (!memo->folder(_folderId.current()) && !folders.empty()) {
		_folderId = folders.front().id;
	}
	_tabs->setActiveTab(FolderTabId(_folderId.current()));
	updateControlsGeometry();
}

void MemoSection::promptNewFolder() {
	const auto submit = [=](QString name, Fn<void()> close) {
		name = name.trimmed();
		if (name.isEmpty()) {
			return;
		}
		const auto id = session().data().memoMessages().createFolder(name);
		_folderId = id;
		refreshTabs();
		showAtEnd();
		close();
	};
	_show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_memo_folder_new());
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_memo_folder_name(),
			QString()));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto save = [=] {
			submit(field->getLastText(), [=] { box->closeBox(); });
		};
		field->submits() | rpl::on_next(save, field->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void MemoSection::showFolderMenu(uint64 folderId) {
	const auto memo = &session().data().memoMessages();
	const auto folder = memo->folder(folderId);
	if (!folder) {
		return;
	}
	const auto name = folder->title;
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		_tabs.data(),
		st::popupMenuWithIcons);
	menu->addAction(tr::lng_memo_folder_rename(tr::now), [=] {
		_show->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(tr::lng_memo_folder_rename());
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				tr::lng_memo_folder_name(),
				name));
			box->setFocusCallback([=] {
				field->setFocusFast();
				field->selectAll();
			});
			const auto save = [=] {
				const auto value = field->getLastText().trimmed();
				if (!value.isEmpty()) {
					memo->renameFolder(folderId, value);
				}
				box->closeBox();
			};
			field->submits() | rpl::on_next(save, field->lifetime());
			box->addButton(tr::lng_settings_save(), save);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
	}, &st::menuIconEdit);
	menu->addAction({
		.text = tr::lng_memo_folder_delete(tr::now),
		.handler = [=] {
			_show->show(Ui::MakeConfirmBox({
				.text = tr::lng_memo_folder_delete_sure(
					tr::now,
					lt_name,
					name),
				.confirmed = [=](Fn<void()> close) {
					memo->removeFolder(folderId);
					close();
				},
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		},
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	menu->popup(QCursor::pos());
}

void MemoSection::setupComposeControls() {
	_composeControls->setHistory({ .history = _history.get() });

	_composeControls->cancelRequests(
	) | rpl::on_next([=] {
		listCancelRequest();
	}, lifetime());

	_composeControls->sendRequests(
	) | rpl::on_next([=] {
		send();
	}, lifetime());

	_composeControls->sendVoiceRequests(
	) | rpl::on_next([=](ComposeControls::VoiceToSend &&data) {
		session().data().memoMessages().sendVoice(
			_folderId.current(),
			data.bytes,
			data.waveform,
			data.duration);
		_composeControls->cancelReplyMessage();
		_composeControls->clear();
		showAtEnd();
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::on_next([=](std::optional<bool> overrideCompress) {
		_choosingAttach = true;
		base::call_delayed(
			st::historyAttach.ripple.hideDuration,
			this,
			[=] { chooseAttach(overrideCompress); });
	}, lifetime());

	_composeControls->height(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, lifetime());

	_composeControls->editLastMessageRequests(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		if (!_inner->lastMessageEditRequestNotify()) {
			_scroll->keyPressEvent(e);
		}
	}, lifetime());

	_composeControls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return Core::CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(data);
		}
		Unexpected("Action in MimeData hook.");
	});
}

void MemoSection::send() {
	auto text = _composeControls->getTextWithAppliedMarkdown();
	if (text.text.isEmpty()) {
		return;
	}
	session().data().memoMessages().sendText(_folderId.current(), text);
	_composeControls->clear();
	showAtEnd();
}

void MemoSection::edit(not_null<HistoryItem*> item) {
	const auto memo = &session().data().memoMessages();
	if (!memo->lookupFolder(item)) {
		return;
	}
	const auto id = item->fullId();
	_show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_context_edit_msg());
		const auto current = session().data().message(id);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_memo_placeholder(),
			current ? current->originalText().text : QString()));
		field->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
		box->setFocusCallback([=] {
			field->setFocusFast();
			field->selectAll();
		});
		const auto save = [=] {
			if (const auto item = session().data().message(id)) {
				memo->editText(item, { field->getLastText() });
			}
			box->closeBox();
		};
		field->submits() | rpl::on_next(save, field->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void MemoSection::chooseAttach(std::optional<bool> overrideCompress) {
	_choosingAttach = false;

	const auto premium = session().premium();
	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), QString(), [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}
		if (!result.remoteContent.isEmpty()) {
			auto list = Storage::PrepareMediaFromImage(
				QImage(),
				std::move(result.remoteContent),
				st::sendMediaPreviewSize);
			confirmSendingFiles(std::move(list));
			return;
		}
		auto list = Storage::PrepareMediaList(
			result.paths,
			st::sendMediaPreviewSize,
			premium);
		if (overrideCompress) {
			list.overrideSendImagesAsPhotos = *overrideCompress;
		}
		confirmSendingFiles(std::move(list));
	}, nullptr);
}

bool MemoSection::confirmSendingFiles(not_null<const QMimeData*> data) {
	const auto premium = session().premium();
	if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
		if (list.error == Ui::PreparedList::Error::None) {
			confirmSendingFiles(std::move(list));
			return true;
		}
	}
	if (auto read = Core::ReadMimeImage(data)) {
		auto list = Storage::PrepareMediaFromImage(
			std::move(read.image),
			std::move(read.content),
			st::sendMediaPreviewSize);
		confirmSendingFiles(std::move(list));
		return true;
	}
	return false;
}

void MemoSection::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (list.error != Ui::PreparedList::Error::None) {
		return;
	}
	auto box = Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = _show,
		.list = std::move(list),
		.caption = _composeControls->getTextWithAppliedMarkdown(),
		.toPeer = _history->peer,
		.limits = DefaultLimitsForPeer(_history->peer),
		.check = DefaultCheckForPeer(_show, _history->peer),
		.sendType = Api::SendType::Normal,
		.sendMenuDetails = [] { return SendMenu::Details(); },
		.confirmed = [=](
				std::shared_ptr<Ui::PreparedBundle> bundle,
				Api::SendOptions,
				FullReplyTo) {
			session().data().memoMessages().sendFiles(
				_folderId.current(),
				bundle);
			_composeControls->clear();
			showAtEnd();
		},
	});
	_show->show(std::move(box));
}

void MemoSection::showAtEnd() {
	_inner->showAtPosition(Data::MaxMessagePosition, {}, nullptr);
}

bool MemoSection::listDoubleClicked(not_null<HistoryItem*> item) {
	if (!session().data().memoMessages().lookupFolder(item)) {
		return false;
	}
	FillCurrentChat(controller(), item);
	return true;
}

bool MemoSection::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return false;
}

bool MemoSection::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect MemoSection::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

void MemoSection::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MemoSection::updateControlsGeometry() {
	if (!width() || !height()) {
		return;
	}
	_tabs->resizeToWidth(width());
	_tabs->moveToLeft(0, 0);
	const auto tabsHeight = _tabs->height();
	_tabsShadow->setGeometry(
		0,
		tabsHeight,
		width(),
		st::lineWidth);

	const auto controlsHeight = _composeControls->heightCurrent();
	const auto top = tabsHeight + st::lineWidth;
	const auto scrollHeight = height() - top - controlsHeight;
	if (scrollHeight > 0) {
		_scroll->setGeometry(0, top, width(), scrollHeight);
		_inner->resizeToWidth(width(), _scroll->height());
	}
	_composeControls->resizeToWidth(width());
	_composeControls->move(0, height() - controlsHeight);
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());
	updateInnerVisibleArea();
}

void MemoSection::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void MemoSection::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}
	SectionWidget::PaintBackground(
		controller(),
		_theme.get(),
		this,
		e->rect());
}

Context MemoSection::listContext() {
	return Context::LocalMemo;
}

bool MemoSection::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void MemoSection::listCancelRequest() {
	if (_inner && !_inner->getSelectedIds().empty()) {
		_inner->cancelSelection();
		return;
	}
	controller()->closeThirdSection();
}

void MemoSection::listDeleteRequest() {
	ConfirmDeleteSelectedItems(_inner);
}

void MemoSection::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	_composeControls->tryProcessKeyInput(e);
}

rpl::producer<Data::MessagesSlice> MemoSection::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto memo = &session().data().memoMessages();
	return _folderId.value(
	) | rpl::map([=](uint64 folderId) {
		return rpl::single(
			rpl::empty
		) | rpl::then(
			memo->updates(folderId)
		) | rpl::map([=] {
			return memo->list(folderId, aroundId, limitBefore, limitAfter);
		});
	}) | rpl::flatten_latest();
}

bool MemoSection::listAllowsMultiSelect() {
	return true;
}

bool MemoSection::listIsItemGoodForSelection(not_null<HistoryItem*> item) {
	return !item->isSending() && !item->hasFailed();
}

bool MemoSection::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void MemoSection::listSelectionChanged(SelectedItems &&items) {
}

void MemoSection::listMarkReadTill(not_null<HistoryItem*> item) {
}

void MemoSection::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData MemoSection::listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) {
	return {};
}

void MemoSection::listContentRefreshed() {
}

void MemoSection::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool MemoSection::listElementHideReply(not_null<const Element*> view) {
	return true;
}

bool MemoSection::listElementShownUnread(not_null<const Element*> view) {
	return true;
}

bool MemoSection::listIsGoodForAroundPosition(not_null<const Element*> view) {
	return true;
}

void MemoSection::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void MemoSection::listSearch(
	const QString &query,
	const FullMsgId &context) {
}

void MemoSection::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> MemoSection::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType MemoSection::listCopyRestrictionType(HistoryItem *item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType MemoSection::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType MemoSection::listSelectRestrictionType() {
	return CopyRestrictionType::None;
}

auto MemoSection::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return rpl::single(Data::AllowedReactions());
}

void MemoSection::listShowPremiumToast(not_null<DocumentData*> document) {
}

void MemoSection::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { context });
}

void MemoSection::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { context });
}

void MemoSection::listPaintEmpty(
	Painter &p,
	const Ui::ChatPaintContext &context) {
}

QString MemoSection::listElementAuthorRank(not_null<const Element*> view) {
	return {};
}

bool MemoSection::listElementHideTopicButton(not_null<const Element*> view) {
	return true;
}

History *MemoSection::listTranslateHistory() {
	return nullptr;
}

void MemoSection::listAddTranslatedItems(
	not_null<TranslateTracker*> tracker) {
}

} // namespace Memo
