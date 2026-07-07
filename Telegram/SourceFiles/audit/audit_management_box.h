/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

namespace Window {
class SessionController;
} // namespace Window

namespace Audit {

// Opens the "local audit data management" UI: a day list drilling into an
// hour list per day, with multi-select and manual upload to the import
// endpoint. Reached from the main menu sidebar.
void ShowAuditManagement(not_null<Window::SessionController*> window);

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
