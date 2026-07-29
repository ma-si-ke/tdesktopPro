/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_memo_folders.h"

#include "core/file_utilities.h"
#include "data/data_memo_messages.h"
#include "data/data_memo_storage.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "memo/memo_archive.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

void EditNameBox(
		not_null<Ui::GenericBox*> box,
		const QString &current,
		Fn<void(QString, Fn<void()>)> submit) {
	box->setTitle(current.isEmpty()
		? tr::lng_memo_folder_new()
		: tr::lng_memo_folder_rename());
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_memo_folder_name(),
		current));
	box->setFocusCallback([=] {
		field->setFocusFast();
		field->selectAll();
	});
	field->changes() | rpl::on_next([=] {
		const auto was = field->getLastText();
		const auto now = Data::FilterMemoFolderTitle(was);
		if (now != was) {
			field->setText(now);
		}
	}, field->lifetime());
	const auto save = [=] {
		const auto value = field->getLastText().trimmed();
		if (!Data::GoodMemoFolderTitle(value)) {
			field->showError();
			return;
		}
		submit(value, crl::guard(box, [=] { box->closeBox(); }));
	};
	field->submits() | rpl::on_next(save, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

class MemoFolders : public Section<MemoFolders> {
public:
	MemoFolders(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void promptRename(uint64 folderId);
	void promptExport();
	void promptImport();
	void runExport(std::vector<uint64> folderIds, QString path);
	void showRowMenu(uint64 folderId);

	const not_null<Window::SessionController*> _controller;
	rpl::variable<bool> _busy = false;

};

MemoFolders::MemoFolders(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> MemoFolders::title() {
	return tr::lng_memo_manage_folders();
}

void MemoFolders::promptRename(uint64 folderId) {
	const auto memo = &_controller->session().data().memoMessages();
	const auto folder = memo->folder(folderId);
	if (!folder) {
		return;
	}
	_controller->show(Box(
		EditNameBox,
		folder->title,
		Fn<void(QString, Fn<void()>)>([=](
				QString value,
				Fn<void()> close) {
			memo->renameFolder(folderId, value);
			close();
		})));
}

void MemoFolders::showRowMenu(uint64 folderId) {
	const auto memo = &_controller->session().data().memoMessages();
	const auto folder = memo->folder(folderId);
	if (!folder) {
		return;
	}
	const auto name = folder->title;
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	menu->addAction(tr::lng_memo_folder_rename(tr::now), [=] {
		promptRename(folderId);
	}, &st::menuIconEdit);
	const auto remove = [=] {
		_controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_memo_folder_delete_sure(tr::now, lt_name, name),
			.confirmed = [=](Fn<void()> close) {
				memo->removeFolder(folderId);
				close();
			},
			.confirmText = tr::lng_box_delete(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	menu->addAction(base::make_unique_q<Ui::Menu::Action>(
		menu->menu(),
		st::menuWithIconsAttention,
		Ui::Menu::CreateAction(
			menu->menu().get(),
			tr::lng_memo_folder_delete(tr::now),
			remove),
		&st::menuIconDeleteAttention,
		&st::menuIconDeleteAttention));
	menu->popup(QCursor::pos());
}

void MemoFolders::runExport(std::vector<uint64> folderIds, QString path) {
	auto folders = _controller->session().data().memoMessages(
		).collectForArchive(folderIds);
	if (folders.empty()) {
		return;
	}
	_busy = true;
	const auto weak = base::make_weak(this);
	Memo::Pack(std::move(folders), path, [=](QString error) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->_busy = false;
		if (!error.isEmpty()) {
			strong->_controller->show(Ui::MakeInformBox(error));
		} else {
			strong->_controller->showToast(
				tr::lng_memo_export_done(tr::now));
			File::ShowInFolder(path);
		}
	});
}

void MemoFolders::promptExport() {
	const auto memo = &_controller->session().data().memoMessages();
	if (memo->folders().empty() || _busy.current()) {
		return;
	}
	const auto selected = std::make_shared<base::flat_set<uint64>>();
	for (const auto &folder : memo->folders()) {
		selected->emplace(folder.id);
	}
	_controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_memo_export_title());
		for (const auto &folder : memo->folders()) {
			const auto id = folder.id;
			const auto row = box->addRow(
				object_ptr<Ui::Checkbox>(
					box,
					folder.title,
					true,
					st::defaultBoxCheckbox),
				style::margins(
					st::boxRowPadding.left(),
					st::boxLittleSkip,
					st::boxRowPadding.right(),
					st::boxLittleSkip));
			row->checkedChanges() | rpl::on_next([=](bool checked) {
				if (checked) {
					selected->emplace(id);
				} else {
					selected->remove(id);
				}
			}, row->lifetime());
		}
		box->addButton(tr::lng_memo_export_confirm(), [=] {
			if (selected->empty()) {
				return;
			}
			auto ids = std::vector<uint64>();
			for (const auto &folder : memo->folders()) {
				if (selected->contains(folder.id)) {
					ids.push_back(folder.id);
				}
			}
			const auto single = (ids.size() == 1)
				? memo->folder(ids.front())
				: nullptr;
			const auto suggested = (single
				? single->title
				: (u"memo-"_q
					+ QDate::currentDate().toString(u"yyyy-MM-dd"_q)))
				+ u".zip"_q;
			box->closeBox();
			FileDialog::GetWritePath(
				this,
				tr::lng_memo_export_title(tr::now),
				u"Zip (*.zip)"_q,
				suggested,
				crl::guard(this, [=](QString &&path) {
					runExport(ids, path);
				}));
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void MemoFolders::promptImport() {
	if (_busy.current()) {
		return;
	}
	FileDialog::GetOpenPath(
		this,
		tr::lng_memo_import_title(tr::now),
		u"Zip (*.zip)"_q,
		crl::guard(this, [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			const auto memo = &_controller->session().data().memoMessages();
			const auto temporary = Data::MemoRootDir(
				&_controller->session()) + u".import/"_q;
			_busy = true;
			const auto weak = base::make_weak(this);
			Memo::Unpack(result.paths.front(), temporary, [=](
					std::vector<Memo::UnpackedFolder> folders,
					QString error) {
				const auto strong = weak.get();
				if (!strong) {
					Memo::ClearTemporary(temporary);
					return;
				}
				strong->_busy = false;
				auto count = 0;
				for (const auto &folder : folders) {
					if (memo->importFolder(folder)) {
						++count;
					}
				}
				Memo::ClearTemporary(temporary);
				if (!error.isEmpty()) {
					strong->_controller->show(Ui::MakeInformBox(error));
				} else {
					strong->_controller->showToast(
						tr::lng_memo_import_done(
							tr::now,
							lt_count,
							float64(count)));
				}
			});
		}));
}

void MemoFolders::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto memo = &_controller->session().data().memoMessages();

	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_memo_manage_about());
	Ui::AddSkip(content);

	const auto addWrap = content->add(
		object_ptr<Ui::VerticalLayout>(content));
	const auto add = addWrap->add(object_ptr<Ui::SettingsButton>(
		addWrap,
		tr::lng_memo_folder_new(),
		st::settingsButtonNoIcon));
	add->setClickedCallback([=] {
		_controller->show(Box(
			EditNameBox,
			QString(),
			Fn<void(QString, Fn<void()>)>([=](
					QString value,
					Fn<void()> close) {
				memo->createFolder(value);
				close();
			})));
	});

	const auto importRow = addWrap->add(object_ptr<Ui::SettingsButton>(
		addWrap,
		tr::lng_memo_import_title(),
		st::settingsButtonNoIcon));
	importRow->setClickedCallback([=] { promptImport(); });

	const auto exportRow = addWrap->add(object_ptr<Ui::SettingsButton>(
		addWrap,
		tr::lng_memo_export_title(),
		st::settingsButtonNoIcon));
	exportRow->setClickedCallback([=] { promptExport(); });
	{
		using namespace Info::Statistics;
		const auto loading = InfiniteRadialAnimationWidget(
			exportRow,
			st::settingsButtonNoIcon.height / 2);
		AddChildToWidgetCenter(exportRow, loading);
		loading->showOn(_busy.value());
	}

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	const auto inner = content->add(
		object_ptr<Ui::VerticalLayout>(content));
	const auto rebuild = [=] {
		inner->clear();
		for (const auto &folder : memo->folders()) {
			const auto id = folder.id;
			const auto row = inner->add(object_ptr<Ui::SettingsButton>(
				inner,
				rpl::single(folder.title),
				st::settingsButtonNoIcon));
			row->setAcceptBoth(true);
			row->addClickHandler([=](Qt::MouseButton button) {
				if (button == Qt::RightButton) {
					showRowMenu(id);
				} else {
					promptRename(id);
				}
			});
		}
		if (const auto width = content->width()) {
			inner->resizeToWidth(width);
		}
	};
	memo->foldersChanged(
	) | rpl::on_next(rebuild, content->lifetime());
	rebuild();

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type MemoFoldersId() {
	return MemoFolders::Id();
}

} // namespace Settings
