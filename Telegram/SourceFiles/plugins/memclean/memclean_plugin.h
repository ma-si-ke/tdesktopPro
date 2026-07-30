/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Plugins::MemClean {

inline constexpr auto kMinIntervalSeconds = 30;
inline constexpr auto kMaxIntervalSeconds = 86400;
inline constexpr auto kMinThresholdPercent = 20;
inline constexpr auto kMaxThresholdPercent = 95;

enum class AutoMode {
	Interval = 0,
	Threshold = 1,
};

// The feature exists only while memclean.dll lies next to the binary,
// cleaning itself additionally requires running elevated. Both are
// checked once at startup.
[[nodiscard]] bool Available();
[[nodiscard]] bool Elevated();
[[nodiscard]] int MemoryPercent();

[[nodiscard]] bool AutoCleanEnabled();
void SetAutoCleanEnabled(bool enabled);
[[nodiscard]] AutoMode AutoCleanMode();
void SetAutoCleanMode(AutoMode mode);
[[nodiscard]] int AutoCleanInterval();
void SetAutoCleanInterval(int seconds);
[[nodiscard]] int AutoCleanThreshold();
void SetAutoCleanThreshold(int percent);

// Runs a clean on a background thread and reports the result in a
// toast. Manual and automatic cleans share one global cooldown, extra
// clicks inside it only show the remaining time.
void CleanNow();
[[nodiscard]] bool Cleaning();
[[nodiscard]] int CooldownSecondsLeft();

// Arms the auto-clean scheduler, called once at startup.
void Start();

} // namespace Plugins::MemClean
