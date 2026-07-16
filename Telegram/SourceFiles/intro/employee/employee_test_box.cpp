/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_test_box.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"
#include "core/application.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_controller.h"

EMPLOYEE_TEST_API Q_NEVER_INLINE void ShowEmployeeTestBoxA() {
	LOG(("Employee: TEST box A invoked"));
	// This may be called from an injected, non-GUI thread — that is exactly
	// the probe-test scenario — so marshal the Qt UI work onto the main
	// thread. Touching widgets from a foreign thread would crash.
	crl::on_main([] {
		const auto window = Core::App().activeWindow();
		if (!window) {
			return;
		}
		window->show(Ui::MakeInformBox(u"测试弹窗A"_q));
	});
}

#endif // TDESKTOP_EMPLOYEE_MODE
