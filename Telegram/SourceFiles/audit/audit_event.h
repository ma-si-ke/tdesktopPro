/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QJsonObject>

struct TextWithEntities;
class HistoryItem;
class PeerData;

namespace Data {
class Media;
} // namespace Data

namespace Audit {

[[nodiscard]] QJsonObject ContentToJson(
	const TextWithEntities &text,
	Data::Media *media);

[[nodiscard]] QJsonObject PeerToJson(not_null<PeerData*> peer);

// The Reporter adds eventId / seq / occurredAt before persisting.
[[nodiscard]] QJsonObject BuildSentEvent(not_null<HistoryItem*> item);
[[nodiscard]] QJsonObject BuildEditedEvent(
	not_null<HistoryItem*> item,
	const TextWithEntities &before,
	const TextWithEntities &after);
[[nodiscard]] QJsonObject BuildDeletedEvent(
	not_null<HistoryItem*> item,
	bool revoke);

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
