/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "settings/settings_type.h"

#include <rpl/producer.h>

namespace Settings {

[[nodiscard]] Type MoreFeaturesId();

// Persistent, reactive "quick copy multiple user IDs" toggle. Stored in the
// generic settings preferences map, so it survives restarts without touching
// the fixed settings serialization.
[[nodiscard]] bool QuickCopyUserIds();
void SetQuickCopyUserIds(bool enabled);
[[nodiscard]] rpl::producer<bool> QuickCopyUserIdsValue();

} // namespace Settings

#endif // TDESKTOP_EMPLOYEE_MODE
