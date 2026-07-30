/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/memclean/memclean_plugin.h"

#include "base/debug_log.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "plugins/plugin_loader.h"
#include "ui/toast/toast.h"

namespace Plugins::MemClean {
namespace {

constexpr auto kAutoPrefKey = "memclean/auto_enabled";
constexpr auto kModePrefKey = "memclean/auto_mode";
constexpr auto kIntervalPrefKey = "memclean/auto_interval";
constexpr auto kThresholdPrefKey = "memclean/auto_threshold";

constexpr auto kDefaultInterval = 300;
constexpr auto kDefaultThreshold = 80;

// Mem Reduct's default mask: everything except the whole standby list
// and the modified page list, purging those makes the system laggy.
constexpr auto kMaskDefault = quint32(0xE7);

constexpr auto kManualCooldown = 15 * crl::time(1000);
constexpr auto kThresholdCooldown = 60 * crl::time(1000);
constexpr auto kThresholdPollDelay = 5 * crl::time(1000);

#ifdef Q_OS_WIN
#define MEMCLEAN_CALL __stdcall
#else // Q_OS_WIN
#define MEMCLEAN_CALL
#endif // Q_OS_WIN

// Mirrors MC_RESULT from memclean.h (see the dll/ subfolder).
struct McResult {
	quint64 freedBytes = 0;
	quint32 succeededMask = 0;
	quint32 failedMask = 0;
	quint32 skippedMask = 0;
	qint32 lastStatus = 0;
};

using IsElevatedFn = int (MEMCLEAN_CALL *)();
using InitializeFn = int (MEMCLEAN_CALL *)();
using SupportedMaskFn = quint32 (MEMCLEAN_CALL *)();
using MemoryPercentFn = quint32 (MEMCLEAN_CALL *)();
using CleanFn = int (MEMCLEAN_CALL *)(quint32 mask, McResult *result);

void AutoTick();

struct Instance {
	Instance();

	Loader loader;
	IsElevatedFn isElevated = nullptr;
	InitializeFn initialize = nullptr;
	MemoryPercentFn memoryPercent = nullptr;
	CleanFn clean = nullptr;

	bool elevated = false;
	bool initialized = false;
	bool initializeFailed = false;

	bool cleaning = false;
	crl::time lastCleanStarted = 0;

	base::Timer autoTimer;
};

Instance::Instance()
: loader(u"memclean"_q)
, autoTimer([] { AutoTick(); }) {
	if (!loader.loaded()) {
		return;
	}
	isElevated = loader.resolve<IsElevatedFn>("MC_IsElevated");
	initialize = loader.resolve<InitializeFn>("MC_Initialize");
	memoryPercent = loader.resolve<MemoryPercentFn>("MC_GetMemoryPercent");
	clean = loader.resolve<CleanFn>("MC_Clean");
	if (!loader.loaded()) {
		isElevated = nullptr;
		initialize = nullptr;
		memoryPercent = nullptr;
		clean = nullptr;
		return;
	}
	elevated = (isElevated() != 0);
	LOG(("MemClean: Plugin ready, elevated=%1.").arg(elevated ? 1 : 0));
}

[[nodiscard]] Instance &Inst() {
	static auto result = Instance();
	return result;
}

void UpdateScheduler() {
	auto &inst = Inst();
	inst.autoTimer.cancel();
	if (!inst.loader.loaded() || !inst.elevated || !AutoCleanEnabled()) {
		return;
	} else if (AutoCleanMode() == AutoMode::Interval) {
		inst.autoTimer.callEach(AutoCleanInterval() * crl::time(1000));
	} else {
		inst.autoTimer.callEach(kThresholdPollDelay);
	}
}

void StartClean(bool manual) {
	auto &inst = Inst();
	if (!inst.loader.loaded()) {
		return;
	} else if (!inst.elevated) {
		if (manual) {
			Ui::Toast::Show(u"内存清理需要以管理员身份运行本程序。"_q);
		}
		return;
	} else if (inst.initializeFailed) {
		if (manual) {
			Ui::Toast::Show(u"内存清理初始化失败，功能不可用。"_q);
		}
		return;
	} else if (inst.cleaning) {
		if (manual) {
			Ui::Toast::Show(u"正在清理中，请稍候…"_q);
		}
		return;
	}
	const auto left = CooldownSecondsLeft();
	if (left > 0) {
		if (manual) {
			Ui::Toast::Show(
				u"刚刚清理过，%1 秒后可再次清理。"_q.arg(left));
		}
		return;
	}
	inst.cleaning = true;
	inst.lastCleanStarted = crl::now();
	const auto initialize = inst.initialize;
	const auto clean = inst.clean;
	const auto needInitialize = !inst.initialized;
	crl::async([=] {
		if (needInitialize && !initialize()) {
			crl::on_main([] {
				auto &instance = Inst();
				instance.cleaning = false;
				instance.initializeFailed = true;
				LOG(("MemClean: MC_Initialize failed."));
				Ui::Toast::Show(u"内存清理初始化失败，功能不可用。"_q);
			});
			return;
		}
		auto result = McResult();
		const auto ok = (clean(kMaskDefault, &result) != 0);
		const auto freed = result.freedBytes;
		const auto failedMask = result.failedMask;
		const auto lastStatus = result.lastStatus;
		crl::on_main([=] {
			auto &instance = Inst();
			instance.cleaning = false;
			instance.initialized = true;
			if (ok) {
				if (failedMask) {
					LOG(("MemClean: Partial failure, mask %1, status %2."
						).arg(failedMask
						).arg(lastStatus));
				}
				const auto mb = double(freed) / (1024. * 1024.);
				Ui::Toast::Show(
					u"内存清理完成，释放了 %1 MB。"_q.arg(mb, 0, 'f', 1));
			} else {
				LOG(("MemClean: Clean failed, status %1.").arg(lastStatus));
				Ui::Toast::Show(u"内存清理失败。"_q);
			}
		});
	});
}

void AutoTick() {
	auto &inst = Inst();
	if (!AutoCleanEnabled() || !inst.elevated) {
		return;
	} else if (AutoCleanMode() == AutoMode::Interval) {
		StartClean(false);
		return;
	}
	// Threshold mode polls often, so it gets a longer cooldown of its
	// own - right after a clean the usage may still sit above the
	// threshold and we must not spin in a clean loop.
	if (inst.cleaning
		|| (inst.lastCleanStarted
			&& crl::now() - inst.lastCleanStarted < kThresholdCooldown)) {
		return;
	}
	const auto percent = inst.memoryPercent
		? int(inst.memoryPercent())
		: 0;
	if (percent >= AutoCleanThreshold()) {
		StartClean(false);
	}
}

} // namespace

bool Available() {
	return Inst().loader.loaded();
}

bool Elevated() {
	return Inst().elevated;
}

int MemoryPercent() {
	auto &inst = Inst();
	return (inst.loader.loaded() && inst.memoryPercent)
		? int(inst.memoryPercent())
		: 0;
}

bool AutoCleanEnabled() {
	return Core::App().settings().readPref<bool>(kAutoPrefKey, false);
}

void SetAutoCleanEnabled(bool enabled) {
	Core::App().settings().writePref<bool>(kAutoPrefKey, enabled);
	UpdateScheduler();
}

AutoMode AutoCleanMode() {
	const auto raw = Core::App().settings().readPref<int>(kModePrefKey, 0);
	return (raw == 1) ? AutoMode::Threshold : AutoMode::Interval;
}

void SetAutoCleanMode(AutoMode mode) {
	Core::App().settings().writePref<int>(kModePrefKey, int(mode));
	UpdateScheduler();
}

int AutoCleanInterval() {
	return std::clamp(
		Core::App().settings().readPref<int>(
			kIntervalPrefKey,
			kDefaultInterval),
		kMinIntervalSeconds,
		kMaxIntervalSeconds);
}

void SetAutoCleanInterval(int seconds) {
	Core::App().settings().writePref<int>(
		kIntervalPrefKey,
		std::clamp(seconds, kMinIntervalSeconds, kMaxIntervalSeconds));
	UpdateScheduler();
}

int AutoCleanThreshold() {
	return std::clamp(
		Core::App().settings().readPref<int>(
			kThresholdPrefKey,
			kDefaultThreshold),
		kMinThresholdPercent,
		kMaxThresholdPercent);
}

void SetAutoCleanThreshold(int percent) {
	Core::App().settings().writePref<int>(
		kThresholdPrefKey,
		std::clamp(percent, kMinThresholdPercent, kMaxThresholdPercent));
	UpdateScheduler();
}

void CleanNow() {
	StartClean(true);
}

bool Cleaning() {
	return Inst().cleaning;
}

int CooldownSecondsLeft() {
	const auto &inst = Inst();
	if (!inst.lastCleanStarted) {
		return 0;
	}
	const auto passed = crl::now() - inst.lastCleanStarted;
	return (passed >= kManualCooldown)
		? 0
		: int((kManualCooldown - passed + 999) / 1000);
}

void Start() {
	UpdateScheduler();
}

} // namespace Plugins::MemClean
