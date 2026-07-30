/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Plugins {

struct MarketEntry {
	QString name;
	int version = 0;
	QString link;
	QString sha256;
	int64 size = 0;
	QString releasedAt;
	QString description;
};

// An empty list is a valid answer, `failed` tells the two apart.
struct MarketList {
	std::vector<MarketEntry> entries;
	bool failed = false;
};

void RequestMarket(Fn<void(MarketList)> done);

struct DownloadResult {
	bool ok = false;
	bool asUpdate = false;
	QString error;
};

// Downloads into the plugins folder, verifying size and sha256. If the
// plugin is already installed the file is kept aside as an update, the
// running library cannot be replaced before a restart.
void Download(
	const MarketEntry &entry,
	bool asUpdate,
	Fn<void(int)> progress,
	Fn<void(DownloadResult)> done);

} // namespace Plugins
