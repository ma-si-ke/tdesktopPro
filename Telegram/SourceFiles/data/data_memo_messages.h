/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_memo_storage.h"
#include "data/data_messages.h"
#include "history/history_item.h"

class History;
struct FilePrepareResult;

namespace Ui {
struct PreparedBundle;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

// Local memo messages. They are kept only on this device, never sent to
// the server. Items are hosted by the Saved Messages history, but carry
// MessageFlag::FakeHistoryItem so they never become history entries and
// never show up in the Saved Messages chat, exactly like the business
// shortcut messages do.

class MemoMessages final {
public:
	explicit MemoMessages(not_null<Session*> owner);
	~MemoMessages();

	[[nodiscard]] not_null<History*> history() const;

	[[nodiscard]] const std::vector<MemoFolder> &folders() const;
	[[nodiscard]] rpl::producer<> foldersChanged() const;
	[[nodiscard]] const MemoFolder *folder(uint64 folderId) const;
	[[nodiscard]] uint64 defaultFolder();

	uint64 createFolder(const QString &title);
	void renameFolder(uint64 folderId, const QString &title);
	void removeFolder(uint64 folderId);
	void applyFolderOrder(const std::vector<uint64> &order);

	[[nodiscard]] rpl::producer<> updates(uint64 folderId);
	[[nodiscard]] MessagesSlice list(
		uint64 folderId,
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter);

	void sendText(uint64 folderId, TextWithTags text);
	void sendFiles(
		uint64 folderId,
		const std::shared_ptr<Ui::PreparedBundle> &bundle);
	void sendVoice(
		uint64 folderId,
		QByteArray bytes,
		VoiceWaveform waveform,
		crl::time duration);
	void editText(not_null<HistoryItem*> item, TextWithTags text);
	void deleteMessage(not_null<const HistoryItem*> item);

	[[nodiscard]] uint64 lookupFolder(not_null<const HistoryItem*> item) const;
	[[nodiscard]] const MemoMessage *lookupMessage(
		not_null<const HistoryItem*> item) const;

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;

	// The manifest of a folder is fully kept in memory, it is only a list
	// of small structs. History items are built lazily, for the window of
	// messages that the list widget asks for, and are kept in `items` by
	// their memo message id once built.
	struct List {
		MemoManifest manifest;
		base::flat_map<uint64, OwnedItem> items;
		bool loaded = false;
	};

	void ensureLoaded(uint64 folderId);
	[[nodiscard]] HistoryItem *materialize(
		uint64 folderId,
		const MemoMessage &message);
	void registerMedia(
		const FilePrepareResult &result,
		const QString &path);
	[[nodiscard]] MemoMessage *appendMessage(
		uint64 folderId,
		const QString &fileName,
		const QString &originalName,
		const FilePrepareResult &result,
		TextWithTags caption,
		uint64 groupId,
		bool spoiler,
		crl::time duration,
		const VoiceWaveform &waveform);
	not_null<HistoryItem*> createItem(
		uint64 folderId,
		const MemoMessage &message,
		const MTPMessageMedia &media);
	[[nodiscard]] MsgId nextItemId();
	void save(uint64 folderId);
	void remove(not_null<const HistoryItem*> item);
	void notify(uint64 folderId);
	void saveFolders();

	const not_null<Session*> _owner;
	const not_null<Main::Session*> _session;
	const not_null<History*> _history;

	std::vector<MemoFolder> _folders;
	base::flat_map<uint64, List> _data;
	base::flat_map<not_null<const HistoryItem*>, uint64> _itemToFolder;
	base::flat_map<not_null<const HistoryItem*>, uint64> _itemToMessage;

	MsgId _lastItemId = MemoMinMsgId;
	uint64 _removingFolder = 0;

	rpl::event_stream<uint64> _updates;
	rpl::event_stream<> _foldersChanged;

	rpl::lifetime _lifetime;

};

} // namespace Data
