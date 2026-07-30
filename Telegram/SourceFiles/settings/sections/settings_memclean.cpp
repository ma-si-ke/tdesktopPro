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

// NumberInput is based on MaskedInputField which is not an RpWidget,
// so it cannot go into a VerticalLayout directly - wrap it the way
// OpenServiceNotificationsBox wraps its PasswordInput.
[[nodiscard]] Ui::NumberInput *AddNumberField(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> placeholder,
		int value,
		int limit) {
	const auto fieldWrap = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	const auto field = Ui::CreateChild<Ui::NumberInput>(
		fieldWrap,
		st::defaultInputField,
		std::move(placeholder),
		QString::number(value),
		limit);
	fieldWrap->resize(fieldWrap->width(), field->height());
	fieldWrap->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(width, field->height());
	}, fieldWrap->lifetime());
	return field;
}

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
			? u"未检测到 memclean.dll，请将其放入程序目录的 plugins 文件夹后重启。"_q
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
	const auto intervalField = AddNumberField(
		intervalWrap->entity(),
		rpl::single(u"清理间隔（秒，最少 30）"_q),
		MC::AutoCleanInterval(),
		MC::kMaxIntervalSeconds);

	const auto thresholdWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));
	const auto thresholdField = AddNumberField(
		thresholdWrap->entity(),
		rpl::single(u"内存占用阈值（%，20 - 95）"_q),
		MC::AutoCleanThreshold(),
		MC::kMaxThresholdPercent);

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
		clean->setColorOverride(st::windowSubTextFg->c);
		autoToggle->setDisabled(true);
		autoToggle->setColorOverride(st::windowSubTextFg->c);
		autoToggle->setToggleLocked(true);
		wrap->setDisabled(true);
	}

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type MemCleanId() {
	return MemCleanSettings::Id();
}

} // namespace Settings
