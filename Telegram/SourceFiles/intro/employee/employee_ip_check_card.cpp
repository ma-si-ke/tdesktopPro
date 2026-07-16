/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_ip_check_card.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "lang/lang_keys.h"
#include "styles/style_basic.h"
#include "styles/style_intro.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"

namespace Intro::Employee {
namespace {

[[nodiscard]] QColor ConfidenceColor(int confidence) {
	if (confidence >= 90) {
		return st::boxTextFgGood->c;
	} else if (confidence >= 60) {
		return anim::color(st::boxTextFgGood, st::attentionButtonFg, 0.5);
	}
	return st::attentionButtonFg->c;
}

} // namespace

IpCheckCardLayout CountIpCheckCardLayout(int width, int top) {
	auto result = IpCheckCardLayout();
	result.contentWidth = std::min(int(st::employeeIpCheckWidth), width);
	result.left = (width - result.contentWidth) / 2;
	auto y = top;
	result.gaugeTop = y;
	y += st::employeeIpCheckGaugeSize + st::employeeIpCheckConfidenceSkip;
	result.confidenceTop = y;
	y += st::normalFont->height + st::employeeIpCheckTitleSkip;
	result.titleTop = y;
	y += st::employeeIpCheckTitleFont->height + st::employeeIpCheckInfoSkip;
	result.infoTop = y;
	y += 4 * st::employeeIpCheckInfoRowHeight + st::employeeIpCheckGridSkip;
	result.gridTop = y;
	y += 4 * st::employeeIpCheckGridRowHeight + st::employeeIpCheckHintSkip;
	result.hintTop = y;
	y += 2 * st::normalFont->height;
	result.bottom = y;
	return result;
}

void PaintIpCheckCard(
		QPainter &p,
		const IpCheckCardLayout &layout,
		int width,
		const IpCheckInfo &info,
		float64 gaugeProgress) {
	auto hq = PainterHighQualityEnabler(p);

	const auto risky = IsRiskyIp(info);
	const auto gaugeSize = st::employeeIpCheckGaugeSize;
	const auto stroke = st::employeeIpCheckGaugeStroke;
	const auto gaugeRect = QRectF(
		(width - gaugeSize) / 2. + stroke / 2.,
		layout.gaugeTop + stroke / 2.,
		gaugeSize - stroke,
		gaugeSize - stroke);
	const auto risk = std::clamp(info.risk, 0, 100);
	const auto riskColor = anim::color(
		st::boxTextFgGood,
		st::attentionButtonFg,
		risk / 100.);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(
		st::windowBgOver->c,
		stroke,
		Qt::SolidLine,
		Qt::RoundCap));
	p.drawArc(gaugeRect, 225 * 16, -270 * 16);
	if (gaugeProgress * risk > 0.) {
		p.setPen(QPen(riskColor, stroke, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(
			gaugeRect,
			225 * 16,
			int(std::round(-270. * 16 * gaugeProgress * risk / 100.)));
	}
	p.setPen(riskColor);
	p.setFont(st::employeeIpCheckRiskFont);
	p.drawText(
		QRectF(
			(width - gaugeSize) / 2.,
			layout.gaugeTop,
			gaugeSize,
			gaugeSize),
		Qt::AlignCenter,
		QString::number(int(std::round(risk * gaugeProgress))));

	p.setPen(ConfidenceColor(info.confidence));
	p.setFont(st::normalFont);
	p.drawText(
		QRect(0, layout.confidenceTop, width, st::normalFont->height),
		Qt::AlignHCenter | Qt::AlignVCenter,
		tr::lng_employee_ipcheck_confidence(
			tr::now,
			lt_percent,
			QString::number(std::clamp(info.confidence, 0, 100))));

	p.setPen(risky ? st::attentionButtonFg : st::boxTextFgGood);
	p.setFont(st::employeeIpCheckTitleFont);
	p.drawText(
		QRect(
			0,
			layout.titleTop,
			width,
			st::employeeIpCheckTitleFont->height),
		Qt::AlignHCenter | Qt::AlignVCenter,
		risky
			? tr::lng_employee_ipcheck_title(tr::now)
			: tr::lng_employee_ipcheck_passed(tr::now));

	const auto left = layout.left;
	const auto contentWidth = layout.contentWidth;
	const auto labelWidth = st::employeeIpCheckInfoLabelWidth;
	const auto rowHeight = st::employeeIpCheckInfoRowHeight;
	const auto unknown = tr::lng_employee_ipcheck_type_unknown(tr::now);
	const auto infoRows = std::array<std::pair<QString, QString>, 4>{ {
		{ tr::lng_employee_ipcheck_label_ip(tr::now), info.ip },
		{
			tr::lng_employee_ipcheck_label_location(tr::now),
			LocationLabel(info),
		},
		{
			tr::lng_employee_ipcheck_label_provider(tr::now),
			info.provider.isEmpty() ? unknown : info.provider,
		},
		{
			tr::lng_employee_ipcheck_label_type(tr::now),
			NetworkTypeLabel(info.networkType),
		},
	} };
	p.setFont(st::normalFont);
	for (auto i = 0; i != int(infoRows.size()); ++i) {
		const auto y = layout.infoTop + i * rowHeight;
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, y, labelWidth, rowHeight),
			Qt::AlignLeft | Qt::AlignVCenter,
			infoRows[i].first);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(
				left + labelWidth,
				y,
				contentWidth - labelWidth,
				rowHeight),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(
				infoRows[i].second,
				contentWidth - labelWidth));
	}

	const auto cellWidth = contentWidth / 2;
	const auto gridRowHeight = st::employeeIpCheckGridRowHeight;
	const auto dotRadius = st::employeeIpCheckDotRadius;
	for (auto i = 0; i != kIpDetectionCount; ++i) {
		const auto column = i % 2;
		const auto row = i / 2;
		const auto hit = info.detections[i];
		const auto cellLeft = left + column * cellWidth;
		const auto y = layout.gridTop + row * gridRowHeight;
		const auto centerY = y + gridRowHeight / 2.;
		const auto color = hit
			? st::attentionButtonFg->c
			: st::boxTextFgGood->c;
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(
			QPointF(cellLeft + dotRadius, centerY),
			dotRadius,
			dotRadius);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(
				cellLeft + 2 * dotRadius + st::employeeIpCheckDotSkip,
				y,
				cellWidth,
				gridRowHeight),
			Qt::AlignLeft | Qt::AlignVCenter,
			DetectionLabel(IpDetection(i)));
		p.setPen(color);
		p.drawText(
			QRect(
				cellLeft,
				y,
				cellWidth - st::employeeIpCheckCellPadding,
				gridRowHeight),
			Qt::AlignRight | Qt::AlignVCenter,
			hit
				? tr::lng_employee_ipcheck_hit(tr::now)
				: tr::lng_employee_ipcheck_clear(tr::now));
	}

	if (risky) {
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont);
		p.drawText(
			QRect(
				left,
				layout.hintTop,
				contentWidth,
				2 * st::normalFont->height),
			Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
			tr::lng_employee_ipcheck_hint(tr::now));
	}
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
