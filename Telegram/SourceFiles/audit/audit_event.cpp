/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "audit/audit_event.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "history/history.h"
#include "history/history_item.h"
#include "data/data_media_types.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "ui/text/text_entity.h"

#include <QtCore/QJsonArray>

namespace Audit {
namespace {

[[nodiscard]] QString EntityTypeName(EntityType type) {
	switch (type) {
	case EntityType::Url: return u"url"_q;
	case EntityType::CustomUrl: return u"text_url"_q;
	case EntityType::Email: return u"email"_q;
	case EntityType::Hashtag: return u"hashtag"_q;
	case EntityType::Cashtag: return u"cashtag"_q;
	case EntityType::Mention: return u"mention"_q;
	case EntityType::MentionName: return u"mention_name"_q;
	case EntityType::CustomEmoji: return u"custom_emoji"_q;
	case EntityType::BotCommand: return u"bot_command"_q;
	case EntityType::Phone: return u"phone"_q;
	case EntityType::BankCard: return u"bank_card"_q;
	case EntityType::Bold: return u"bold"_q;
	case EntityType::Semibold: return u"bold"_q;
	case EntityType::Italic: return u"italic"_q;
	case EntityType::Underline: return u"underline"_q;
	case EntityType::StrikeOut: return u"strikethrough"_q;
	case EntityType::Code: return u"code"_q;
	case EntityType::Pre: return u"pre"_q;
	case EntityType::Blockquote: return u"blockquote"_q;
	case EntityType::Spoiler: return u"spoiler"_q;
	case EntityType::Subscript: return u"subscript"_q;
	case EntityType::Superscript: return u"superscript"_q;
	default: return QString();
	}
}

[[nodiscard]] QJsonArray EntitiesToJson(const EntitiesInText &entities) {
	auto result = QJsonArray();
	for (const auto &entity : entities) {
		const auto name = EntityTypeName(entity.type());
		if (name.isEmpty()) {
			continue;
		}
		auto object = QJsonObject();
		object.insert(u"type"_q, name);
		object.insert(u"offset"_q, entity.offset());
		object.insert(u"length"_q, entity.length());
		if (!entity.data().isEmpty()) {
			object.insert(u"data"_q, entity.data());
		}
		result.append(object);
	}
	return result;
}

[[nodiscard]] QString DocumentKind(not_null<DocumentData*> document) {
	if (document->sticker()) {
		return u"sticker"_q;
	} else if (document->isVoiceMessage()) {
		return u"voice"_q;
	} else if (document->isVideoMessage()) {
		return u"video_message"_q;
	} else if (document->isVideoFile()) {
		return u"video"_q;
	} else if (document->isAnimation()) {
		return u"gif"_q;
	} else if (document->isAudioFile()) {
		return u"audio"_q;
	}
	return u"document"_q;
}

[[nodiscard]] QJsonObject DocumentToJson(not_null<DocumentData*> document) {
	auto object = QJsonObject();
	object.insert(u"kind"_q, DocumentKind(document));
	if (const auto mime = document->mimeString(); !mime.isEmpty()) {
		object.insert(u"mimeType"_q, mime);
	}
	if (const auto name = document->filename(); !name.isEmpty()) {
		object.insert(u"fileName"_q, name);
	}
	object.insert(u"size"_q, double(document->size));
	if (const auto dimensions = document->dimensions; !dimensions.isEmpty()) {
		object.insert(u"width"_q, dimensions.width());
		object.insert(u"height"_q, dimensions.height());
	}
	if (const auto duration = document->duration(); duration > 0) {
		object.insert(u"durationSeconds"_q, double(duration) / 1000.);
	}
	return object;
}

[[nodiscard]] QJsonObject PhotoToJson(not_null<PhotoData*> photo) {
	auto object = QJsonObject();
	object.insert(u"kind"_q, u"photo"_q);
	if (photo->width() > 0 && photo->height() > 0) {
		object.insert(u"width"_q, photo->width());
		object.insert(u"height"_q, photo->height());
	}
	return object;
}

} // namespace

QJsonObject ContentToJson(const TextWithEntities &text, Data::Media *media) {
	auto content = QJsonObject();
	content.insert(u"text"_q, text.text);
	if (const auto entities = EntitiesToJson(text.entities);
			!entities.isEmpty()) {
		content.insert(u"entities"_q, entities);
	}
	auto list = QJsonArray();
	if (media) {
		if (const auto document = media->document()) {
			list.append(DocumentToJson(document));
		} else if (const auto photo = media->photo()) {
			list.append(PhotoToJson(photo));
		} else {
			auto object = QJsonObject();
			object.insert(u"kind"_q, u"other"_q);
			list.append(object);
		}
	}
	if (!list.isEmpty()) {
		content.insert(u"media"_q, list);
	}
	return content;
}

QJsonObject PeerToJson(not_null<PeerData*> peer) {
	auto object = QJsonObject();
	auto bare = BareId(0);
	if (peer->isUser()) {
		object.insert(u"type"_q, u"user"_q);
		bare = peerToUser(peer->id).bare;
	} else if (peer->isChannel()) {
		object.insert(u"type"_q, u"channel"_q);
		bare = peerToChannel(peer->id).bare;
	} else if (peer->isChat()) {
		object.insert(u"type"_q, u"chat"_q);
		bare = peerToChat(peer->id).bare;
	} else {
		bare = peer->id.value;
	}
	object.insert(u"id"_q, QString::number(bare));
	return object;
}

QJsonObject BuildSentEvent(not_null<HistoryItem*> item) {
	auto event = QJsonObject();
	event.insert(u"type"_q, u"message_sent"_q);
	event.insert(u"peer"_q, PeerToJson(item->history()->peer));

	auto payload = QJsonObject();
	payload.insert(u"origin"_q, u"local"_q);
	payload.insert(u"messageId"_q, QString::number(item->id.bare));
	payload.insert(u"outgoing"_q, item->out());
	payload.insert(u"content"_q, ContentToJson(item->originalText(), item->media()));
	event.insert(u"payload"_q, payload);
	return event;
}

QJsonObject BuildEditedEvent(
		not_null<HistoryItem*> item,
		const TextWithEntities &before,
		const TextWithEntities &after) {
	auto event = QJsonObject();
	event.insert(u"type"_q, u"message_edited"_q);
	event.insert(u"peer"_q, PeerToJson(item->history()->peer));

	auto payload = QJsonObject();
	payload.insert(u"messageId"_q, QString::number(item->id.bare));
	payload.insert(u"before"_q, ContentToJson(before, item->media()));
	payload.insert(u"after"_q, ContentToJson(after, item->media()));
	event.insert(u"payload"_q, payload);
	return event;
}

QJsonObject BuildDeletedEvent(not_null<HistoryItem*> item, bool revoke) {
	auto event = QJsonObject();
	event.insert(u"type"_q, u"message_deleted"_q);
	event.insert(u"peer"_q, PeerToJson(item->history()->peer));

	auto original = QJsonObject();
	original.insert(u"outgoing"_q, item->out());
	original.insert(u"originalSentAt"_q, double(item->date()) * 1000.);
	original.insert(u"content"_q, ContentToJson(item->originalText(), item->media()));

	auto payload = QJsonObject();
	payload.insert(u"messageId"_q, QString::number(item->id.bare));
	payload.insert(u"revoke"_q, revoke);
	payload.insert(u"original"_q, original);
	event.insert(u"payload"_q, payload);
	return event;
}

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
