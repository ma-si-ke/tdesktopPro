/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

#include <rpl/producer.h>

namespace Window {
class SessionController;
} // namespace Window

namespace Memo {

// Puts the contents of a memo message into the currently opened chat
// without sending anything: plain text goes to the compose field draft,
// media opens a pre-filled SendFilesBox, a voice message asks for a
// confirmation first, because there is no way to keep a voice draft.

void FillCurrentChat(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

// When this is on a double click sends the memo to the current chat at
// once, instead of putting it into the compose field or asking first.
[[nodiscard]] bool DoubleClickSends();
void SetDoubleClickSends(bool value);
[[nodiscard]] rpl::producer<bool> DoubleClickSendsValue();

} // namespace Memo
