/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_more_features.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "settings/settings_common_session.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/rp_widget.h"
#include "ui/qt_object_factory.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kPrefKey = "more_features/quick_copy_user_ids";

[[nodiscard]] rpl::variable<bool> &QuickCopyVar() {
	static auto value = rpl::variable<bool>(
		Core::App().settings().readPref<bool>(kPrefKey, false));
	return value;
}

class MoreFeatures : public Section<MoreFeatures> {
public:
	MoreFeatures(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

MoreFeatures::MoreFeatures(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MoreFeatures::title() {
	return rpl::single(u"更多功能"_q);
}

void MoreFeatures::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	const auto button = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(u"快速复制多个用户ID"_q),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(QuickCopyUserIds()));
	button->toggledValue(
	) | rpl::filter([](bool checked) {
		return checked != QuickCopyUserIds();
	}) | rpl::on_next([](bool checked) {
		SetQuickCopyUserIds(checked);
	}, button->lifetime());
	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace

bool QuickCopyUserIds() {
	return QuickCopyVar().current();
}

void SetQuickCopyUserIds(bool enabled) {
	if (QuickCopyVar().current() == enabled) {
		return;
	}
	Core::App().settings().writePref<bool>(kPrefKey, enabled);
	QuickCopyVar() = enabled;
}

rpl::producer<bool> QuickCopyUserIdsValue() {
	return QuickCopyVar().value();
}

Type MoreFeaturesId() {
	return MoreFeatures::Id();
}

} // namespace Settings

#endif // TDESKTOP_EMPLOYEE_MODE
