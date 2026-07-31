/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_instance.h"

#include "base/debug_log.h"
#include "plugins/plugin_settings.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Plugins {
namespace {

constexpr auto kAbiVersion = 1;
constexpr auto kInitialBuffer = 8 * 1024;
constexpr auto kMaxBuffer = 1024 * 1024;

#ifdef Q_OS_WIN
#define PLUGIN_CALL __stdcall
#else // Q_OS_WIN
#define PLUGIN_CALL
#endif // Q_OS_WIN

using AbiFn = int (PLUGIN_CALL *)();
using InitFn = int (PLUGIN_CALL *)(const char*, char*, int);
using CallFn = int (PLUGIN_CALL *)(const char*, const char*, char*, int);
using ShutdownFn = void (PLUGIN_CALL *)();

// The library never allocates for us: it only fills a buffer we own,
// and asks for a bigger one by returning the size it needs.
template <typename Invoke>
[[nodiscard]] std::optional<QByteArray> WithBuffer(Invoke &&invoke) {
	auto size = kInitialBuffer;
	while (true) {
		auto buffer = QByteArray(size, Qt::Uninitialized);
		const auto written = invoke(buffer.data(), size);
		if (written >= 0) {
			if (written > size) {
				return std::nullopt;
			}
			buffer.resize(written);
			return buffer;
		}
		const auto needed = -written;
		if (needed <= size || needed > kMaxBuffer) {
			return std::nullopt;
		}
		size = needed;
	}
}

[[nodiscard]] CallResult ParseResult(const QByteArray &json) {
	auto result = CallResult();
	if (json.isEmpty()) {
		return result;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		LOG(("Plugins Error: Call answer is not an object."));
		return result;
	}
	const auto object = document.object();
	result.toast = object.value(u"toast"_q).toString();
	result.error = object.value(u"error"_q).toString();
	result.refresh = object.value(u"refresh"_q).toBool();
	const auto texts = object.value(u"text"_q);
	if (texts.isObject()) {
		const auto fields = texts.toObject();
		for (auto i = fields.begin(); i != fields.end(); ++i) {
			if (i.value().isString()) {
				result.texts.emplace(i.key(), i.value().toString());
			}
		}
	}
	return result;
}

[[nodiscard]] QByteArray MakeArgs(const Manifest &manifest) {
	auto object = QJsonObject();
	object.insert(u"settings"_q, Settings::Collect(manifest));
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

Instance::Instance(Manifest manifest, const QString &folder)
: _manifest(std::move(manifest))
, _loader(folder + '/' + _manifest.library)
, _tickTimer([=] { tick(); }) {
	if (!_loader.loaded()) {
		_reason = u"插件库加载失败：%1"_q.arg(_loader.error());
		return;
	}
	const auto abi = _loader.resolve<AbiFn>("Plugin_Abi");
	const auto init = _loader.resolve<InitFn>("Plugin_Init");
	_call = reinterpret_cast<void*>(_loader.resolve<CallFn>("Plugin_Call"));
	_shutdown = reinterpret_cast<void*>(
		_loader.resolve<ShutdownFn>("Plugin_Shutdown"));
	if (!_loader.loaded()) {
		_reason = u"插件库不完整：%1"_q.arg(_loader.error());
		_call = _shutdown = nullptr;
		return;
	} else if (abi() != kAbiVersion) {
		_reason = u"插件接口版本不兼容。"_q;
		_call = _shutdown = nullptr;
		return;
	}
	const auto args = MakeArgs(_manifest);
	const auto answer = WithBuffer([&](char *buffer, int size) {
		return init(args.constData(), buffer, size);
	});
	if (!answer) {
		_reason = u"插件初始化失败。"_q;
		_call = _shutdown = nullptr;
		return;
	}
	const auto document = QJsonDocument::fromJson(*answer);
	const auto object = document.object();
	if (!object.value(u"available"_q).toBool(true)) {
		_reason = object.value(u"reason"_q).toString();
		if (_reason.isEmpty()) {
			_reason = u"插件当前不可用。"_q;
		}
		return;
	}
	_available = true;
	LOG(("Plugins Info: '%1' ready.").arg(_manifest.name));
}

Instance::~Instance() {
	stopTicks();
	if (const auto shutdown = reinterpret_cast<ShutdownFn>(_shutdown)) {
		shutdown();
	}
}

int Instance::cooldownLeft(const QString &action) const {
	const auto i = _cooldowns.find(action);
	if (i == end(_cooldowns)) {
		return 0;
	}
	const auto now = crl::now();
	return (now >= i->second)
		? 0
		: int((i->second - now + 999) / 1000);
}

CallResult Instance::invoke(
		const QString &action,
		const QByteArray &args) {
	const auto call = reinterpret_cast<CallFn>(_call);
	if (!call) {
		return {};
	}
	const auto utf8 = action.toUtf8();
	const auto answer = WithBuffer([&](char *buffer, int size) {
		return call(utf8.constData(), args.constData(), buffer, size);
	});
	if (!answer) {
		LOG(("Plugins Error: Call '%1' of '%2' failed."
			).arg(action, _manifest.name));
		return { .error = u"插件调用失败。"_q };
	}
	return ParseResult(*answer);
}

void Instance::call(
		const QString &action,
		bool async,
		int cooldown,
		Fn<void(CallResult)> done) {
	if (!_available || !_call || _running) {
		return;
	} else if (cooldownLeft(action) > 0) {
		return;
	}
	if (cooldown > 0) {
		_cooldowns[action] = crl::now() + cooldown * crl::time(1000);
	}
	const auto args = MakeArgs(_manifest);
	if (!async) {
		const auto result = invoke(action, args);
		if (done) {
			done(result);
		}
		return;
	}
	_running = true;
	// Instances are owned by the manager and outlive every call, so the
	// raw pointer stays valid; `_running` keeps a second call away.
	const auto raw = this;
	crl::async([=] {
		auto result = raw->invoke(action, args);
		crl::on_main([=, result = std::move(result)] {
			raw->_running = false;
			if (done) {
				done(result);
			}
		});
	});
}

void Instance::tick() {
	if (!_available || _running) {
		return;
	}
	call(u"tick"_q, true, 0, [=](CallResult result) {
		_tickResults.fire(std::move(result));
	});
}

void Instance::startTicks() {
	if (!_available || !_manifest.tickSeconds || _ticking) {
		return;
	}
	_ticking = true;
	_tickTimer.callEach(_manifest.tickSeconds * crl::time(1000));
}

void Instance::stopTicks() {
	_ticking = false;
	_tickTimer.cancel();
}

rpl::producer<CallResult> Instance::tickResults() const {
	return _tickResults.events();
}

} // namespace Plugins
