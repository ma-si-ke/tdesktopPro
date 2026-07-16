/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_ip_check_box.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_ip_check_card.h"
#include "base/debug_log.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "styles/style_intro.h"
#include "ui/effects/animations.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

namespace Intro::Employee {
namespace {

constexpr auto kGaugeDuration = crl::time(650);

class IpCheckCardWidget final : public Ui::RpWidget {
public:
	IpCheckCardWidget(QWidget *parent, const IpCheckInfo &info);

	void setInfo(const IpCheckInfo &info);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	IpCheckInfo _info;
	Ui::Animations::Simple _gaugeAnimation;

};

IpCheckCardWidget::IpCheckCardWidget(
	QWidget *parent,
	const IpCheckInfo &info)
: RpWidget(parent)
, _info(info) {
	_gaugeAnimation.start([=] { update(); }, 0., 1., kGaugeDuration);
}

void IpCheckCardWidget::setInfo(const IpCheckInfo &info) {
	_info = info;
	_gaugeAnimation.stop();
	_gaugeAnimation.start([=] { update(); }, 0., 1., kGaugeDuration);
	update();
}

int IpCheckCardWidget::resizeGetHeight(int newWidth) {
	const auto layout = CountIpCheckCardLayout(
		newWidth,
		st::employeeIpCheckBoxTop);
	return layout.bottom + st::employeeIpCheckBoxBottom;
}

void IpCheckCardWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto layout = CountIpCheckCardLayout(
		width(),
		st::employeeIpCheckBoxTop);
	PaintIpCheckCard(p, layout, width(), _info, _gaugeAnimation.value(1.));
}

} // namespace

void IpCheckResultBox(not_null<Ui::GenericBox*> box, const IpCheckInfo &info) {
	const auto card = box->addRow(object_ptr<IpCheckCardWidget>(box, info));
	const auto client = box->lifetime().make_state<IpCheckClient>();
	const auto rechecking = box->lifetime().make_state<bool>(false);
	box->addButton(tr::lng_employee_ipcheck_confirm(), [=] {
		box->closeBox();
	});
	box->addButton(tr::lng_employee_ipcheck_recheck(), [=] {
		if (*rechecking) {
			return;
		}
		*rechecking = true;
		client->check(nullptr, crl::guard(box, [=](IpCheckResult result) {
			*rechecking = false;
			if (const auto fresh = std::get_if<IpCheckInfo>(&result)) {
				card->setInfo(*fresh);
			} else {
				box->uiShow()->showToast(
					tr::lng_employee_ipcheck_failed(tr::now));
			}
		}));
	});
}

base::weak_qptr<Ui::GenericBox> ShowIpCheckResultBox(
		const IpCheckInfo &info) {
	const auto window = Core::App().activeWindow();
	if (!window) {
		LOG(("Employee: ipcheck result box has no active window"));
		return nullptr;
	}
	return window->show(Box(IpCheckResultBox, info));
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
