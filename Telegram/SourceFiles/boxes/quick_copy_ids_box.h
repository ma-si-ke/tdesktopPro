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

namespace QuickCopy {

// Opens a multi-select list of the user's private chats (most-recent first,
// last-message time on the right) and copies the selected users' numeric
// Telegram IDs to the clipboard, one per line.
void ShowUserIdsBox(not_null<Window::SessionController*> controller);

} // namespace QuickCopy

#endif // TDESKTOP_EMPLOYEE_MODE
