/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "memo/memo_fill.h"

#include "api/api_common.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "apiwrap.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/message_field.h"
#include "core/mime_type.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_groups.h"
#include "data/data_memo_messages.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

#include <QtCore/QFile>

#include <cstring>

namespace Memo {
namespace {

constexpr auto kPrefKey = "memo/double_click_sends";

[[nodiscard]] rpl::variable<bool> &DoubleClickVar() {
	static auto result = rpl::variable<bool>(
		Core::App().settings().readPref<bool>(kPrefKey, false));
	return result;
}


void SetDraft(not_null<Data::Thread*> thread, TextWithTags textWithTags) {
	const auto history = thread->owningHistory();
	const auto topicRootId = thread->topicRootId();
	const auto monoforumPeerId = thread->monoforumPeerId();
	const auto cursor = MessageCursor{
		int(textWithTags.text.size()),
		int(textWithTags.text.size()),
		Ui::kQFixedMax
	};
	history->setLocalDraft(std::make_unique<Data::Draft>(
		textWithTags,
		FullReplyTo{
			.topicRootId = topicRootId,
			.monoforumPeerId = monoforumPeerId,
		},
		SuggestOptions(),
		cursor,
		Data::WebPageDraft()));
	history->clearLocalEditDraft(topicRootId, monoforumPeerId);
	history->session().changes().entryUpdated(
		thread,
		Data::EntryUpdate::Flag::LocalDraftSet);
}

[[nodiscard]] std::vector<not_null<HistoryItem*>> CollectGroup(
		not_null<HistoryItem*> item) {
	auto result = std::vector<not_null<HistoryItem*>>();
	if (const auto group = item->history()->owner().groups().find(item)) {
		for (const auto &entry : group->items) {
			result.push_back(entry);
		}
	} else {
		result.push_back(item);
	}
	return result;
}

void SendText(
		not_null<Data::Thread*> thread,
		const TextWithTags &text) {
	auto message = Api::MessageToSend(Api::SendAction(thread));
	message.textWithTags = text;
	message.action.clearDraft = false;
	thread->session().api().sendMessage(std::move(message));
}

void SendVoiceNow(
		not_null<Data::Thread*> thread,
		const QString &path,
		const QByteArray &packed,
		crl::time duration) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	auto waveform = VoiceWaveform();
	waveform.resize(packed.size());
	memcpy(waveform.data(), packed.constData(), packed.size());
	thread->session().api().sendVoiceMessage(
		file.readAll(),
		std::move(waveform),
		duration,
		false,
		Api::SendAction(thread));
}

void SendFilesNow(
		not_null<Data::Thread*> thread,
		Ui::PreparedList &&list,
		const TextWithTags &caption) {
	auto groups = Ui::DivideByGroups(
		std::move(list),
		Ui::SendFilesWay(),
		false);
	auto action = Api::SendAction(thread);
	action.clearDraft = false;
	auto &api = thread->session().api();
	auto first = true;
	for (auto &group : groups) {
		if (first && !caption.text.isEmpty() && !group.list.files.empty()) {
			group.list.files.front().caption = caption;
		}
		first = false;
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		api.sendFiles(
			std::move(group.list),
			SendMediaType::Photo,
			album,
			action);
	}
}

void FillWithMedia(
		not_null<Window::SessionController*> controller,
		not_null<Data::Thread*> thread,
		const std::vector<not_null<HistoryItem*>> &items) {
	const auto memo = &controller->session().data().memoMessages();
	auto paths = QStringList();
	auto captions = std::vector<TextWithTags>();
	for (const auto &item : items) {
		const auto message = memo->lookupMessage(item);
		if (!message || !message->media) {
			continue;
		}
		const auto folderId = memo->lookupFolder(item);
		const auto path = Data::MemoFilePath(
			&controller->session(),
			folderId,
			message->media->file);
		if (!QFile::exists(path)) {
			continue;
		}
		paths.push_back(path);
		captions.push_back(message->text);
	}
	if (paths.isEmpty()) {
		return;
	}

	auto list = Storage::PrepareMediaList(
		paths,
		st::sendMediaPreviewSize,
		controller->session().premium());
	if (list.error != Ui::PreparedList::Error::None) {
		return;
	}

	// The box moves the whole box caption onto the file that carries the
	// album caption, so the caption of that very file has to be passed as
	// the box caption, while every other caption stays with its own file.
	// Otherwise the box caption would silently overwrite one of them.
	auto boxCaption = TextWithTags();
	for (auto i = 0; i != int(list.files.size()); ++i) {
		if (i >= int(captions.size())) {
			break;
		} else if (boxCaption.text.isEmpty()
			&& !captions[i].text.isEmpty()) {
			boxCaption = captions[i];
		} else {
			list.files[i].caption = captions[i];
		}
	}

	if (DoubleClickSends()) {
		SendFilesNow(thread, std::move(list), boxCaption);
		return;
	}

	const auto show = controller->uiShow();
	const auto weak = base::make_weak(thread);
	auto box = Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = show,
		.list = std::move(list),
		.caption = boxCaption,
		.toPeer = thread->peer(),
		.limits = DefaultLimitsForPeer(thread->peer()),
		.check = DefaultCheckForPeer(show, thread->peer()),
		.sendType = Api::SendType::Normal,
		.confirmed = [=](
				std::shared_ptr<Ui::PreparedBundle> bundle,
				Api::SendOptions options,
				FullReplyTo replyTo) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto action = Api::SendAction(strong, options);
			action.replyTo = replyTo;
			const auto compress = bundle->way.sendImagesAsPhotos();
			const auto type = compress
				? SendMediaType::Photo
				: SendMediaType::File;
			auto &api = strong->session().api();
			for (auto &group : bundle->groups) {
				const auto album = (group.type != Ui::AlbumType::None)
					? std::make_shared<SendingAlbum>()
					: nullptr;
				api.sendFiles(
					std::move(group.list),
					type,
					album,
					action);
			}
		},
	});
	show->show(std::move(box));
}

void FillWithVoice(
		not_null<Window::SessionController*> controller,
		not_null<Data::Thread*> thread,
		not_null<HistoryItem*> item) {
	const auto memo = &controller->session().data().memoMessages();
	const auto message = memo->lookupMessage(item);
	if (!message || !message->media) {
		return;
	}
	const auto folderId = memo->lookupFolder(item);
	const auto path = Data::MemoFilePath(
		&controller->session(),
		folderId,
		message->media->file);
	const auto duration = message->media->duration;
	const auto packed = message->media->waveform;
	if (DoubleClickSends()) {
		SendVoiceNow(thread, path, packed, duration);
		return;
	}

	const auto weak = base::make_weak(thread);
	controller->uiShow()->show(Ui::MakeConfirmBox({
		.text = tr::lng_memo_fill_voice_sure(tr::now),
		.confirmed = [=](Fn<void()> close) {
			close();
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto file = QFile(path);
			if (!file.open(QIODevice::ReadOnly)) {
				return;
			}
			auto waveform = VoiceWaveform();
			waveform.resize(packed.size());
			memcpy(waveform.data(), packed.constData(), packed.size());
			strong->session().api().sendVoiceMessage(
				file.readAll(),
				std::move(waveform),
				duration,
				false,
				Api::SendAction(strong));
		},
	}));
}

} // namespace

bool DoubleClickSends() {
	return DoubleClickVar().current();
}

void SetDoubleClickSends(bool value) {
	if (DoubleClickVar().current() == value) {
		return;
	}
	Core::App().settings().writePref<bool>(kPrefKey, value);
	DoubleClickVar() = value;
}

rpl::producer<bool> DoubleClickSendsValue() {
	return DoubleClickVar().value();
}

void FillCurrentChat(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto thread = controller->activeChatCurrent().thread();
	if (!thread) {
		controller->showToast(tr::lng_memo_fill_no_chat(tr::now));
		return;
	}
	const auto memo = &controller->session().data().memoMessages();
	const auto message = memo->lookupMessage(item);
	if (!message) {
		return;
	} else if (!message->media) {
		if (DoubleClickSends()) {
			SendText(thread, message->text);
		} else {
			SetDraft(thread, message->text);
		}
		return;
	} else if (message->media->type == Data::MemoMedia::Type::Voice) {
		FillWithVoice(controller, thread, item);
		return;
	}
	FillWithMedia(controller, thread, CollectGroup(item));
}

} // namespace Memo
