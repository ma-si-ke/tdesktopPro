/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/announcements.h"

#include "base/debug_log.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_domain.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Core::Announcements {
namespace {

constexpr auto kVersionPrefKey = "announcement/last_seen_version";
constexpr auto kUrl = "https://td.kakaco.top/api/announcement/latest";

constexpr auto kStartupDelay = 2 * crl::time(1000);
constexpr auto kCheckEach = 15 * 60 * crl::time(1000);
constexpr auto kRequestTimeout = 20 * 1000;
constexpr auto kMaxResponseSize = 512 * 1024;
constexpr auto kMaxReadSeconds = 600;

struct Data {
	int version = 0;
	QString title;
	QString content;
	int minReadSeconds = 0;
	QString createdAt;
};

void Check();

struct Instance {
	Instance()
	: startupTimer([] { Check(); })
	, checkTimer([] { Check(); }) {
	}

	base::Timer startupTimer;
	base::Timer checkTimer;
	std::unique_ptr<QNetworkAccessManager> manager;
	QPointer<QNetworkReply> reply;
	bool started = false;
	bool hasSession = false;
	bool timersArmed = false;
	bool showing = false;
	rpl::lifetime lifetime;
};

[[nodiscard]] Instance &Inst() {
	static auto result = Instance();
	return result;
}

void ShowBox(const Data &data) {
	const auto window = Core::App().activeWindow();
	if (!window) {
		return;
	}
	Inst().showing = true;
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(data.title));
		box->setCloseByEscape(false);
		box->setCloseByOutsideClick(false);

		if (!data.createdAt.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(data.createdAt),
				st::boxDividerLabel));
		}
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(
				TextUtilities::ParseEntities(data.content, TextParseLinks)),
			st::boxLabel));
		label->setSelectable(true);

		struct State {
			rpl::variable<QString> text;
			base::Timer timer;
			int left = 0;
		};
		const auto state = box->lifetime().make_state<State>();
		state->left = std::clamp(data.minReadSeconds, 0, kMaxReadSeconds);

		const auto version = data.version;
		const auto button = box->addButton(state->text.value(), [=] {
			if (state->left > 0) {
				return;
			}
			Core::App().settings().writePref<int>(kVersionPrefKey, version);
			box->closeBox();
		});
		const auto update = [=] {
			state->text = (state->left > 0)
				? u"确定 (%1s)"_q.arg(state->left)
				: u"确定"_q;
			button->setDisabled(state->left > 0);
			if (state->left <= 0) {
				state->timer.cancel();
			}
		};
		update();
		if (state->left > 0) {
			state->timer.setCallback([=] {
				--state->left;
				update();
			});
			state->timer.callEach(crl::time(1000));
		}

		box->lifetime().add([] {
			Inst().showing = false;
		});
	}));
}

void GotResponse(const QByteArray &bytes) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Announcements: Failed to parse JSON."));
		return;
	}
	const auto object = document.object();
	auto data = Data();
	data.version = int(object.value(u"version"_q).toDouble());
	data.title = object.value(u"title"_q).toString();
	data.content = object.value(u"content"_q).toString();
	data.minReadSeconds = int(
		object.value(u"min_read_seconds"_q).toDouble());
	data.createdAt = object.value(u"created_at"_q).toString();
	if (data.version <= 0 || data.title.isEmpty()) {
		return;
	}
	const auto seen = Core::App().settings().readPref<int>(
		kVersionPrefKey,
		0);
	if (data.version <= seen || Inst().showing) {
		return;
	}
	ShowBox(data);
}

void Check() {
	auto &inst = Inst();
	if (!inst.hasSession
		|| inst.showing
		|| inst.reply
		|| Core::App().passcodeLocked()) {
		return;
	}
	if (!inst.manager) {
		inst.manager = std::make_unique<QNetworkAccessManager>();
	}
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kUrl)));
	request.setTransferTimeout(kRequestTimeout);
	const auto reply = inst.manager->get(request);
	inst.reply = reply;
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		Inst().reply = nullptr;
		const auto bytes = (reply->error() == QNetworkReply::NoError)
			? reply->readAll()
			: QByteArray();
		reply->deleteLater();
		if (bytes.isEmpty() || bytes.size() > kMaxResponseSize) {
			return;
		}
		GotResponse(bytes);
	});
}

} // namespace

void Start() {
	auto &inst = Inst();
	if (inst.started) {
		return;
	}
	inst.started = true;
	Core::App().domain().activeSessionValue(
	) | rpl::on_next([](Main::Session *session) {
		auto &instance = Inst();
		instance.hasSession = (session != nullptr);
		if (session && !instance.timersArmed) {
			instance.timersArmed = true;
			instance.startupTimer.callOnce(kStartupDelay);
			instance.checkTimer.callEach(kCheckEach);
		}
	}, inst.lifetime);
}

} // namespace Core::Announcements
