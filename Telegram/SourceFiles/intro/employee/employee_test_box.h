/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtGlobal>

// A tiny box that only ever shows "测试弹窗A". It never appears on its own:
// it is invoked either from the debug button in More Features settings, or by
// a simulated attacker who injects code and calls it to prove the process is
// compromised and can be made to do something observable — the injection probe
// is then verified against that real effect.
//
// Exported (so it lands in the export table and can be resolved by an injected
// module via GetProcAddress) and never-inlined, which also guarantees it
// survives Release optimization. TEST-ONLY: gate or remove before shipping.

#ifdef Q_OS_WIN
#define EMPLOYEE_TEST_API extern "C" __declspec(dllexport)
#else
#define EMPLOYEE_TEST_API extern "C" __attribute__((visibility("default")))
#endif

EMPLOYEE_TEST_API Q_NEVER_INLINE void ShowEmployeeTestBoxA();

#endif // TDESKTOP_EMPLOYEE_MODE
