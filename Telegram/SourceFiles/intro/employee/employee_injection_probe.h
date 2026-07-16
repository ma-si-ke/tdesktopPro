/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/timer.h"

namespace Intro::Employee {

enum class ProbeLevel : int {
	Off = 0,      // 关闭:完全不扫描
	LogOnly = 1,  // 仅记录:扫描并落盘,永不弹窗(观察误报/漏报)
	Lenient = 2,  // 宽松:仅强信号(指向私有内存)才弹窗退出
	Strict = 3,   // 严格:任意信号(含 EDR 挂钩)都弹窗退出
};

[[nodiscard]] ProbeLevel InjectionProbeLevel();
void SetInjectionProbeLevel(ProbeLevel level);

enum class FindingKind {
	PrivateExecMemory,  // 信号1:私有可执行内存
	InlineHook,         // 信号2:关键 API 被 inline hook
};

struct ProbeFinding {
	FindingKind kind = FindingKind::PrivateExecMemory;
	bool strong = false;  // RWX 私有内存 / 挂钩目标落在私有内存
	QString detail;
};

// 纯扫描,可离线推理。非 Windows 平台返回空。
[[nodiscard]] std::vector<ProbeFinding> ScanForInjection();

class InjectionProbe final {
public:
	InjectionProbe();

	void startupCheck();
	void startPeriodic();
	void stop();

private:
	void fire();

	base::Timer _startupTimer;
	base::Timer _periodicTimer;
	bool _triggered = false;

};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
