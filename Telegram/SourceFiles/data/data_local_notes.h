/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

class PeerData;

namespace Data {

// Per-peer notes stored only in the local encrypted account storage
// (Storage::Account prefs), never sent to the server. Not related to
// the official contacts.updateContactNote API. Note images are kept
// as plain files under tdata/local_notes/<user id>/, referenced by
// name from the serialized note payload.

struct LocalNoteData {
	QString text;
	QStringList images;

	[[nodiscard]] bool empty() const {
		return text.isEmpty() && images.isEmpty();
	}
};

[[nodiscard]] LocalNoteData LocalNote(not_null<PeerData*> peer);
void SetLocalNote(not_null<PeerData*> peer, LocalNoteData data);
[[nodiscard]] rpl::producer<LocalNoteData> LocalNoteValue(
	not_null<PeerData*> peer);

[[nodiscard]] QString LocalNoteImagePath(
	not_null<PeerData*> peer,
	const QString &name);
[[nodiscard]] QString SaveLocalNoteImage(
	not_null<PeerData*> peer,
	const QImage &image);
[[nodiscard]] QString CopyLocalNoteImage(
	not_null<PeerData*> peer,
	const QString &sourcePath);

} // namespace Data
