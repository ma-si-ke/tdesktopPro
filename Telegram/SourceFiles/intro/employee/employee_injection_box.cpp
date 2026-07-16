/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_injection_box.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "window/window_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Intro::Employee {
namespace {

bool ShownAlready = false;

void InjectionDetectedBox(not_null<Ui::GenericBox*> box) {
	box->setWidth(st::boxWideWidth);
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);

	auto icon = Settings::CreateLottieIcon(
		box,
		{
			.name = u"ban"_q,
			.sizeOverride = st::dialogsSuggestionDeniedAuthLottie,
		},
		st::dialogsSuggestionDeniedAuthLottieMargins);
	const auto animate = std::move(icon.animate);
	Settings::AddLottieIconWithCircle(
		box->verticalLayout(),
		std::move(icon.widget),
		st::settingsBlockedListIconPadding,
		st::dialogsSuggestionDeniedAuthLottieCircle);
	box->setShowFinishedCallback([=] {
		animate(anim::repeat::once);
	});

	Ui::AddSkip(box->verticalLayout());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_employee_injection_title(),
			st::boostCenteredTitle),
		st::showOrTitlePadding,
		style::al_top);

	Ui::AddSkip(box->verticalLayout());
	const auto warning = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_employee_injection_text(),
			st::boostText),
		st::showOrAboutPadding
			+ QMargins(st::boostTextSkip, 0, st::boostTextSkip, 0),
		style::al_top);
	warning->setTextColorOverride(st::attentionButtonFg->c);
	const auto warningBg = Ui::CreateChild<Ui::RpWidget>(
		box->verticalLayout());
	warning->geometryValue() | rpl::on_next([=](QRect r) {
		warningBg->setGeometry(r + Margins(st::boostTextSkip));
	}, warningBg->lifetime());
	warningBg->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::attentionButtonBgOver);
		p.drawRoundedRect(
			warningBg->rect(),
			st::buttonRadius,
			st::buttonRadius);
	});
	warningBg->show();
	warning->raise();
	warningBg->stackUnder(warning);

	box->addButton(
		tr::lng_employee_injection_quit(),
		[] { Core::Quit(); },
		st::attentionBoxButton);
}

} // namespace

bool ShowInjectionDetectedBox() {
	if (ShownAlready) {
		return true;
	}
	const auto window = Core::App().activeWindow();
	if (!window) {
		LOG(("Employee: injection box has no active window, deferring"));
		return false;
	}
	ShownAlready = true;
	LOG(("Employee: SHOWING injection-detected box"));
	window->show(Box(InjectionDetectedBox));
	return true;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
