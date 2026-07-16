/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_ip_check.h"

class QPainter;

namespace Intro::Employee {

struct IpCheckCardLayout {
	int left = 0;
	int contentWidth = 0;
	int gaugeTop = 0;
	int confidenceTop = 0;
	int titleTop = 0;
	int infoTop = 0;
	int gridTop = 0;
	int hintTop = 0;
	int bottom = 0;
};

[[nodiscard]] IpCheckCardLayout CountIpCheckCardLayout(int width, int top);
void PaintIpCheckCard(
	QPainter &p,
	const IpCheckCardLayout &layout,
	int width,
	const IpCheckInfo &info,
	float64 gaugeProgress);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
