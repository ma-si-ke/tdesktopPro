/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_ip_check_panel.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/call_delayed.h"
#include "lang/lang_keys.h"
#include "styles/style_basic.h"
#include "styles/style_intro.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"

#include <QtGui/QPainterPath>

namespace Intro::Employee {
namespace {

constexpr auto kFadeDuration = crl::time(220);
constexpr auto kGaugeDuration = crl::time(650);
constexpr auto kTickDuration = crl::time(420);
constexpr auto kPassedTotal = crl::time(950);
constexpr auto kRadarPeriod = crl::time(1400);
constexpr auto kRadarRings = 3;

[[nodiscard]] QColor ConfidenceColor(int confidence) {
	if (confidence >= 90) {
		return st::boxTextFgGood->c;
	} else if (confidence >= 60) {
		return anim::color(st::boxTextFgGood, st::attentionButtonFg, 0.5);
	}
	return st::attentionButtonFg->c;
}

} // namespace

IpCheckPanel::IpCheckPanel(QWidget *parent)
: RpWidget(parent)
, _radarAnimation([=](crl::time) {
	update();
	return true;
})
, _retry(
	this,
	tr::lng_employee_ipcheck_retry(),
	st::employeeIpCheckRetryButton)
, _continue(
	this,
	tr::lng_employee_ipcheck_continue(),
	st::employeeIpCheckContinueButton) {
	_retry->hide();
	_continue->hide();
	hide();
}

IpCheckPanel::~IpCheckPanel() = default;

void IpCheckPanel::setContentTop(int top) {
	_contentTop = top;
	updateButtonsGeometry();
	update();
}

void IpCheckPanel::setRetryCallback(Fn<void()> callback) {
	_retry->setClickedCallback(std::move(callback));
}

void IpCheckPanel::setContinueCallback(Fn<void()> callback) {
	_continue->setClickedCallback(std::move(callback));
}

void IpCheckPanel::showChecking() {
	_phaseText = tr::lng_employee_ipcheck_phase_ip(tr::now);
	if (!_shown) {
		_shown = true;
		show();
		raise();
		_shownAnimation.start(
			[=] { animationFrame(); },
			0.,
			1.,
			kFadeDuration);
	}
	switchState(State::Checking);
}

void IpCheckPanel::showAnalyzing() {
	_phaseText = tr::lng_employee_ipcheck_phase_quality(tr::now);
	update();
}

void IpCheckPanel::showPassed(Fn<void()> finished) {
	switchState(State::Passed);
	_tickAnimation.stop();
	_tickAnimation.start([=] { update(); }, 0., 1., kTickDuration);
	base::call_delayed(kPassedTotal, this, std::move(finished));
}

void IpCheckPanel::showBlocked(const IpCheckInfo &info) {
	_info = info;
	if (!_shown) {
		_shown = true;
		show();
		raise();
		_shownAnimation.start(
			[=] { animationFrame(); },
			0.,
			1.,
			kFadeDuration);
	}
	switchState(State::Blocked);
	_gaugeAnimation.stop();
	_gaugeAnimation.start([=] { update(); }, 0., 1., kGaugeDuration);
	updateButtonsGeometry();
}

void IpCheckPanel::hideAnimated(Fn<void()> finished) {
	if (!_shown) {
		if (finished) {
			finished();
		}
		return;
	}
	_shown = false;
	_hiddenCallback = std::move(finished);
	_radarAnimation.stop();
	refreshButtonsVisibility();
	_shownAnimation.start([=] {
		update();
		if (!_shownAnimation.animating() && !_shown) {
			_state = State::Hidden;
			hide();
			if (const auto callback = base::take(_hiddenCallback)) {
				callback();
			}
		}
	}, 1., 0., kFadeDuration);
}

void IpCheckPanel::switchState(State state) {
	_state = state;
	if (state == State::Checking) {
		if (!_radarAnimation.animating()) {
			_radarAnimation.start();
		}
	} else if (state != State::Passed) {
		_radarAnimation.stop();
	}
	refreshButtonsVisibility();
	_contentAnimation.stop();
	_contentAnimation.start([=] { animationFrame(); }, 0., 1., kFadeDuration);
	update();
}

void IpCheckPanel::animationFrame() {
	update();
	refreshButtonsVisibility();
}

void IpCheckPanel::refreshButtonsVisibility() {
	const auto visible = (_state == State::Blocked)
		&& _shown
		&& !_shownAnimation.animating()
		&& !_contentAnimation.animating();
	_retry->setVisible(visible);
	_continue->setVisible(visible);
}

void IpCheckPanel::updateButtonsGeometry() {
	const auto layout = countBlockedLayout();
	const auto gap = st::employeeIpCheckButtonsGap;
	const auto totalWidth = _retry->width() + gap + _continue->width();
	const auto left = (width() - totalWidth) / 2;
	_retry->moveToLeft(left, layout.buttonsTop);
	_continue->moveToLeft(
		left + _retry->width() + gap,
		layout.buttonsTop);
}

IpCheckPanel::BlockedLayout IpCheckPanel::countBlockedLayout() const {
	auto result = BlockedLayout();
	result.left = (width() - st::employeeIpCheckWidth) / 2;
	auto y = _contentTop;
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
	y += 2 * st::normalFont->height + st::employeeIpCheckButtonsSkip;
	result.buttonsTop = y;
	return result;
}

void IpCheckPanel::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	updateButtonsGeometry();
}

void IpCheckPanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto shown = _shownAnimation.value(_shown ? 1. : 0.);
	if (shown <= 0.) {
		return;
	}
	p.setOpacity(shown);
	p.fillRect(rect(), st::windowBg);

	const auto contentShown = _contentAnimation.value(1.);
	p.setOpacity(shown * contentShown);
	switch (_state) {
	case State::Hidden:
		break;
	case State::Checking:
		paintChecking(p);
		break;
	case State::Passed:
		p.setOpacity(shown);
		paintPassed(p);
		break;
	case State::Blocked:
		paintBlocked(p, contentShown);
		break;
	}
}

void IpCheckPanel::paintChecking(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	const auto center = QPointF(
		width() / 2.,
		_contentTop + st::employeeIpCheckRadarCenterTop);
	const auto baseOpacity = p.opacity();
	const auto minRadius = float64(st::employeeIpCheckRadarRadius);
	const auto maxRadius = float64(st::employeeIpCheckRadarRingMax);
	const auto elapsed = crl::now() - _radarAnimation.started();
	p.setBrush(Qt::NoBrush);
	for (auto k = 0; k != kRadarRings; ++k) {
		auto phase = (elapsed % kRadarPeriod) / float64(kRadarPeriod)
			+ k / float64(kRadarRings);
		phase -= std::floor(phase);
		const auto radius = minRadius + phase * (maxRadius - minRadius);
		p.setOpacity(baseOpacity * (1. - phase) * 0.35);
		p.setPen(QPen(
			st::windowBgActive->c,
			st::employeeIpCheckRingStroke));
		p.drawEllipse(center, radius, radius);
	}
	p.setOpacity(baseOpacity);
	paintCenterBadge(p, 0.);

	p.setPen(st::introDescriptionFg);
	p.setFont(st::normalFont);
	p.drawText(
		QRect(
			0,
			_contentTop + st::employeeIpCheckPhaseTop,
			width(),
			st::normalFont->height),
		Qt::AlignHCenter | Qt::AlignVCenter,
		_phaseText);
}

void IpCheckPanel::paintPassed(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	const auto morph = _contentAnimation.value(1.);
	paintCenterBadge(p, morph);

	p.setOpacity(p.opacity() * morph);
	p.setPen(st::boxTextFgGood);
	p.setFont(st::semiboldFont);
	p.drawText(
		QRect(
			0,
			_contentTop + st::employeeIpCheckPhaseTop,
			width(),
			st::semiboldFont->height),
		Qt::AlignHCenter | Qt::AlignVCenter,
		tr::lng_employee_ipcheck_passed(tr::now));
}

void IpCheckPanel::paintCenterBadge(QPainter &p, float64 toPassed) {
	const auto center = QPointF(
		width() / 2.,
		_contentTop + st::employeeIpCheckRadarCenterTop);
	const auto radius = float64(st::employeeIpCheckRadarRadius);
	const auto baseOpacity = p.opacity();

	p.setPen(Qt::NoPen);
	p.setBrush(anim::color(st::windowBgActive, st::callAnswerBg, toPassed));
	p.drawEllipse(center, radius, radius);

	p.save();
	p.translate(center);
	if (toPassed < 1.) {
		const auto s = radius * 1.1;
		auto shield = QPainterPath();
		shield.moveTo(0., -0.52 * s);
		shield.cubicTo(
			0.18 * s, -0.44 * s,
			0.34 * s, -0.40 * s,
			0.44 * s, -0.40 * s);
		shield.cubicTo(
			0.44 * s, -0.02 * s,
			0.36 * s, 0.28 * s,
			0., 0.52 * s);
		shield.cubicTo(
			-0.36 * s, 0.28 * s,
			-0.44 * s, -0.02 * s,
			-0.44 * s, -0.40 * s);
		shield.cubicTo(
			-0.34 * s, -0.40 * s,
			-0.18 * s, -0.44 * s,
			0., -0.52 * s);
		p.setOpacity(baseOpacity * (1. - toPassed));
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowFgActive);
		p.drawPath(shield);
	}
	const auto tick = _tickAnimation.value((toPassed > 0.) ? 1. : 0.);
	if (tick > 0.) {
		const auto r = radius * 0.9;
		const auto a = QPointF(-0.42 * r, 0.04 * r);
		const auto b = QPointF(-0.10 * r, 0.36 * r);
		const auto c = QPointF(0.46 * r, -0.26 * r);
		const auto firstLength = QLineF(a, b).length();
		const auto secondLength = QLineF(b, c).length();
		const auto drawn = (firstLength + secondLength) * tick;
		auto path = QPainterPath();
		path.moveTo(a);
		if (drawn <= firstLength) {
			path.lineTo(QLineF(a, b).pointAt(drawn / firstLength));
		} else {
			path.lineTo(b);
			path.lineTo(QLineF(b, c).pointAt(
				std::min((drawn - firstLength) / secondLength, 1.)));
		}
		p.setOpacity(baseOpacity);
		p.setPen(QPen(
			st::windowFgActive->c,
			st::employeeIpCheckTickStroke,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}
	p.restore();
	p.setOpacity(baseOpacity);
}

void IpCheckPanel::paintBlocked(QPainter &p, float64 contentShown) {
	auto hq = PainterHighQualityEnabler(p);
	const auto layout = countBlockedLayout();
	const auto shift = (1. - contentShown) * st::employeeIpCheckSlideShift;
	p.save();
	p.translate(0., shift);

	const auto gaugeSize = st::employeeIpCheckGaugeSize;
	const auto stroke = st::employeeIpCheckGaugeStroke;
	const auto gaugeRect = QRectF(
		(width() - gaugeSize) / 2. + stroke / 2.,
		layout.gaugeTop + stroke / 2.,
		gaugeSize - stroke,
		gaugeSize - stroke);
	const auto risk = std::clamp(_info.risk, 0, 100);
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
	const auto sweep = _gaugeAnimation.value(1.);
	if (sweep * risk > 0.) {
		p.setPen(QPen(riskColor, stroke, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(
			gaugeRect,
			225 * 16,
			int(std::round(-270. * 16 * sweep * risk / 100.)));
	}
	p.setPen(riskColor);
	p.setFont(st::employeeIpCheckRiskFont);
	p.drawText(
		QRectF(
			(width() - gaugeSize) / 2.,
			layout.gaugeTop,
			gaugeSize,
			gaugeSize),
		Qt::AlignCenter,
		QString::number(int(std::round(risk * sweep))));

	p.setPen(ConfidenceColor(_info.confidence));
	p.setFont(st::normalFont);
	p.drawText(
		QRect(0, layout.confidenceTop, width(), st::normalFont->height),
		Qt::AlignHCenter | Qt::AlignVCenter,
		tr::lng_employee_ipcheck_confidence(
			tr::now,
			lt_percent,
			QString::number(std::clamp(_info.confidence, 0, 100))));

	p.setPen(st::attentionButtonFg);
	p.setFont(st::employeeIpCheckTitleFont);
	p.drawText(
		QRect(
			0,
			layout.titleTop,
			width(),
			st::employeeIpCheckTitleFont->height),
		Qt::AlignHCenter | Qt::AlignVCenter,
		tr::lng_employee_ipcheck_title(tr::now));

	const auto left = layout.left;
	const auto contentWidth = st::employeeIpCheckWidth;
	const auto labelWidth = st::employeeIpCheckInfoLabelWidth;
	const auto rowHeight = st::employeeIpCheckInfoRowHeight;
	const auto unknown = tr::lng_employee_ipcheck_type_unknown(tr::now);
	const auto infoRows = std::array<std::pair<QString, QString>, 4>{ {
		{ tr::lng_employee_ipcheck_label_ip(tr::now), _info.ip },
		{
			tr::lng_employee_ipcheck_label_location(tr::now),
			LocationLabel(_info),
		},
		{
			tr::lng_employee_ipcheck_label_provider(tr::now),
			_info.provider.isEmpty() ? unknown : _info.provider,
		},
		{
			tr::lng_employee_ipcheck_label_type(tr::now),
			NetworkTypeLabel(_info.networkType),
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
		const auto hit = _info.detections[i];
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

	p.restore();
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
