/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_memclean.h"

#include "base/timer.h"
#include "plugins/memclean/memclean_plugin.h"
#include "settings/settings_common_session.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

namespace MC = Plugins::MemClean;

class MemCleanSettings : public Section<MemCleanSettings> {
public:
	MemCleanSettings(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

MemCleanSettings::MemCleanSettings(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MemCleanSettings::title() {
	return rpl::single(u"清理内存"_q);
}

void MemCleanSettings::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto available = MC::Available();
	const auto elevated = available && MC::Elevated();

	struct State {
		rpl::variable<QString> cleanText;
		rpl::variable<QString> statusText;
		base::Timer timer;
	};
	const auto state = content->lifetime().make_state<State>();

	const auto updateTexts = [=] {
		const auto left = MC::CooldownSecondsLeft();
		state->cleanText = MC::Cleaning()
			? u"正在清理…"_q
			: (left > 0)
			? u"立即清理（%1 秒后可用）"_q.arg(left)
			: u"立即清理"_q;
		state->statusText = !available
			? u"未检测到 memclean.dll，请将其放入程序目录后重启。"_q
			: !elevated
			? u"需要以管理员身份运行本程序才能清理内存。"_q
			: u"当前内存占用：%1%。清理期间 CPU 占用较高，"
			"低配电脑单次清理可能持续数秒。"_q.arg(MC::MemoryPercent());
	};
	updateTexts();
	state->timer.setCallback(updateTexts);
	state->timer.callEach(crl::time(1000));

	Ui::AddSkip(content);
	const auto clean = content->add(object_ptr<Ui::SettingsButton>(
		content,
		state->cleanText.value(),
		st::settingsButtonNoIcon));
	clean->setClickedCallback([=] {
		MC::CleanNow();
		updateTexts();
	});
	Ui::AddSkip(content);
	Ui::AddDividerText(content, state->statusText.value());

	Ui::AddSkip(content);
	const auto autoToggle = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(u"自动清理"_q),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(MC::AutoCleanEnabled()));

	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	wrap->toggle(MC::AutoCleanEnabled(), anim::type::instant);
	const auto inner = wrap->entity();

	autoToggle->toggledValue(
	) | rpl::filter([](bool checked) {
		return checked != MC::AutoCleanEnabled();
	}) | rpl::on_next([=](bool checked) {
		MC::SetAutoCleanEnabled(checked);
		wrap->toggle(checked, anim::type::normal);
	}, autoToggle->lifetime());

	Ui::AddSkip(inner);
	const auto group = std::make_shared<Ui::RadioenumGroup<MC::AutoMode>>(
		MC::AutoCleanMode());
	const auto addRadio = [&](MC::AutoMode value, const QString &label) {
		inner->add(
			object_ptr<Ui::Radioenum<MC::AutoMode>>(
				inner,
				group,
				value,
				label,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	addRadio(MC::AutoMode::Interval, u"定时清理"_q);
	addRadio(MC::AutoMode::Threshold, u"按内存占用清理"_q);
	Ui::AddSkip(inner);

	const auto intervalWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));
	const auto intervalField = intervalWrap->entity()->add(
		object_ptr<Ui::NumberInput>(
			intervalWrap->entity(),
			st::defaultInputField,
			rpl::single(u"清理间隔（秒，最少 30）"_q),
			QString::number(MC::AutoCleanInterval()),
			MC::kMaxIntervalSeconds),
		st::boxRowPadding);

	const auto thresholdWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));
	const auto thresholdField = thresholdWrap->entity()->add(
		object_ptr<Ui::NumberInput>(
			thresholdWrap->entity(),
			st::defaultInputField,
			rpl::single(u"内存占用阈值（%，20 - 95）"_q),
			QString::number(MC::AutoCleanThreshold()),
			MC::kMaxThresholdPercent),
		st::boxRowPadding);

	const auto updateModeWraps = [=](anim::type animated) {
		const auto interval = (group->current() == MC::AutoMode::Interval);
		intervalWrap->toggle(interval, animated);
		thresholdWrap->toggle(!interval, animated);
	};
	updateModeWraps(anim::type::instant);

	group->value(
	) | rpl::filter([=](MC::AutoMode value) {
		return value != MC::AutoCleanMode();
	}) | rpl::on_next([=](MC::AutoMode value) {
		MC::SetAutoCleanMode(value);
		updateModeWraps(anim::type::normal);
	}, inner->lifetime());

	QObject::connect(intervalField, &Ui::NumberInput::changed, content, [=] {
		const auto value = intervalField->getLastText().toInt();
		if (value > 0) {
			MC::SetAutoCleanInterval(value);
		}
	});
	QObject::connect(thresholdField, &Ui::NumberInput::changed, content, [=] {
		const auto value = thresholdField->getLastText().toInt();
		if (value > 0) {
			MC::SetAutoCleanThreshold(value);
		}
	});

	Ui::AddSkip(inner);
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::single(
			u"定时清理：每隔指定秒数清理一次，最少 30 秒。"
			u"按内存占用清理：每 5 秒检测一次占用，超过阈值即清理，"
			u"两次自动清理至少间隔 60 秒。低于下限的输入按下限生效。"_q));

	if (!elevated) {
		clean->setDisabled(true);
		autoToggle->setDisabled(true);
		wrap->setDisabled(true);
	}

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type MemCleanId() {
	return MemCleanSettings::Id();
}

} // namespace Settings
