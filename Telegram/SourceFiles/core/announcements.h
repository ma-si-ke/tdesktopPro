/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core::Announcements {

// Polls the private announcement endpoint while a session is active
// and shows every unseen announcement in a popup that can be closed
// only by its Ok button, after the minimal reading time has passed.
void Start();

} // namespace Core::Announcements
