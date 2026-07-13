/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_local_notes.h"

#include "base/random.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "settings.h"
#include "storage/storage_account.h"

#include <rpl/event_stream.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QImage>

namespace Data {
namespace {

struct LocalNoteChange {
	Main::Session *session = nullptr;
	PeerId peerId;
	LocalNoteData data;
};

[[nodiscard]] rpl::event_stream<LocalNoteChange> &LocalNoteChanges() {
	static auto result = rpl::event_stream<LocalNoteChange>();
	return result;
}

[[nodiscard]] std::string LocalNoteKey(not_null<PeerData*> peer) {
	return "local_note_" + std::to_string(peer->id.value);
}

[[nodiscard]] QString LocalNotesDir(not_null<PeerData*> peer) {
	return cWorkingDir()
		+ u"tdata/local_notes/"_q
		+ QString::number(peer->session().userId().bare);
}

[[nodiscard]] QString SerializeLocalNote(const LocalNoteData &data) {
	auto images = QJsonArray();
	for (const auto &name : data.images) {
		images.append(name);
	}
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{ u"text"_q, data.text },
		{ u"images"_q, images },
	}).toJson(QJsonDocument::Compact));
}

[[nodiscard]] LocalNoteData DeserializeLocalNote(const QString &value) {
	const auto document = QJsonDocument::fromJson(value.toUtf8());
	if (!document.isObject()) {
		return { .text = value };
	}
	const auto object = document.object();
	auto result = LocalNoteData{ .text = object[u"text"_q].toString() };
	for (const auto entry : object[u"images"_q].toArray()) {
		if (const auto name = entry.toString(); !name.isEmpty()) {
			result.images.push_back(name);
		}
	}
	return result;
}

[[nodiscard]] QString GenerateImageName(const QString &extension) {
	return QString::number(base::RandomValue<uint64>(), 16)
		+ '.'
		+ extension;
}

} // namespace

LocalNoteData LocalNote(not_null<PeerData*> peer) {
	const auto value = peer->session().local().readPref<QString>(
		LocalNoteKey(peer));
	return value.isEmpty() ? LocalNoteData() : DeserializeLocalNote(value);
}

void SetLocalNote(not_null<PeerData*> peer, LocalNoteData data) {
	const auto old = LocalNote(peer);
	for (const auto &name : old.images) {
		if (!data.images.contains(name)) {
			QFile::remove(LocalNoteImagePath(peer, name));
		}
	}
	const auto key = LocalNoteKey(peer);
	if (data.empty()) {
		peer->session().local().clearPref(key);
	} else {
		peer->session().local().writePref<QString>(
			key,
			SerializeLocalNote(data));
	}
	LocalNoteChanges().fire({ &peer->session(), peer->id, std::move(data) });
}

rpl::producer<LocalNoteData> LocalNoteValue(not_null<PeerData*> peer) {
	const auto session = &peer->session();
	const auto peerId = peer->id;
	return rpl::single(
		LocalNote(peer)
	) | rpl::then(LocalNoteChanges().events(
	) | rpl::filter([=](const LocalNoteChange &change) {
		return (change.session == session) && (change.peerId == peerId);
	}) | rpl::map([](const LocalNoteChange &change) {
		return change.data;
	}));
}

QString LocalNoteImagePath(not_null<PeerData*> peer, const QString &name) {
	return LocalNotesDir(peer) + '/' + name;
}

QString SaveLocalNoteImage(not_null<PeerData*> peer, const QImage &image) {
	if (image.isNull() || !QDir().mkpath(LocalNotesDir(peer))) {
		return QString();
	}
	const auto name = GenerateImageName(u"png"_q);
	return image.save(LocalNoteImagePath(peer, name), "PNG")
		? name
		: QString();
}

QString CopyLocalNoteImage(
		not_null<PeerData*> peer,
		const QString &sourcePath) {
	if (!QDir().mkpath(LocalNotesDir(peer))) {
		return QString();
	}
	const auto suffix = QFileInfo(sourcePath).suffix().toLower();
	const auto name = GenerateImageName(
		suffix.isEmpty() ? u"png"_q : suffix);
	return QFile::copy(sourcePath, LocalNoteImagePath(peer, name))
		? name
		: QString();
}

} // namespace Data
