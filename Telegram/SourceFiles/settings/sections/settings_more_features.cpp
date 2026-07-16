/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_more_features.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "settings/settings_common_session.h"
#include "cloudmemo/cloud_memo.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "intro/employee/employee_ip_check.h"
#include "intro/employee/employee_injection_probe.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/rp_widget.h"
#include "ui/qt_object_factory.h"
#include "ui/widgets/checkbox.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

constexpr auto kPrefKey = "more_features/quick_copy_user_ids";

[[nodiscard]] rpl::variable<bool> &QuickCopyVar() {
	static auto value = rpl::variable<bool>(
		Core::App().settings().readPref<bool>(kPrefKey, false));
	return value;
}

[[nodiscard]] rpl::variable<QString> &CloudMemoUrlVar() {
	static auto value = rpl::variable<QString>(CloudMemo::BaseUrl());
	return value;
}

void EditCloudMemoUrlBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"共享备注服务器"_q));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(CloudMemo::DefaultBaseUrl()),
		CloudMemo::BaseUrl()));
	box->setFocusCallback([=] { field->setFocusFast(); });
	const auto save = [=] {
		CloudMemo::SetBaseUrl(field->getLastText());
		CloudMemoUrlVar() = CloudMemo::BaseUrl();
		box->closeBox();
	};
	field->submits() | rpl::on_next(save, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

class MoreFeatures : public Section<MoreFeatures> {
public:
	MoreFeatures(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;

};

MoreFeatures::MoreFeatures(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller) {
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

	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"共享备注"_q));
	const auto server = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(u"服务器地址"_q),
		st::settingsButtonNoIcon));
	server->setClickedCallback([=] {
		_controller->show(Box(EditCloudMemoUrlBox));
	});
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		CloudMemoUrlVar().value() | rpl::map([](const QString &url) {
			return u"当前："_q + url;
		}));

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"网络检测"_q));
	const auto periodic = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(u"定期检测 IP 风险"_q),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(Intro::Employee::PeriodicIpCheckEnabled()));
	periodic->toggledValue(
	) | rpl::filter([](bool checked) {
		return checked != Intro::Employee::PeriodicIpCheckEnabled();
	}) | rpl::on_next([](bool checked) {
		Intro::Employee::SetPeriodicIpCheckEnabled(checked);
	}, periodic->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::single(u"开启后每小时自动检测一次网络环境，发现风险时提醒。"_q));

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"调试"_q));
	const auto debugPanel = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(u"IP 正常也弹出面板"_q),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(Intro::Employee::DebugShowIpPanelEnabled()));
	debugPanel->toggledValue(
	) | rpl::filter([](bool checked) {
		return checked != Intro::Employee::DebugShowIpPanelEnabled();
	}) | rpl::on_next([](bool checked) {
		Intro::Employee::SetDebugShowIpPanelEnabled(checked);
	}, debugPanel->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::single(
			u"开启后登录和启动时即使 IP 正常也会展示检测面板，用于验证显示效果。"_q));

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"进程注入检测"_q));
	{
		using Level = Intro::Employee::ProbeLevel;
		const auto group = std::make_shared<Ui::RadioenumGroup<Level>>(
			Intro::Employee::InjectionProbeLevel());
		const auto addRadio = [&](Level value, const QString &label) {
			content->add(
				object_ptr<Ui::Radioenum<Level>>(
					content,
					group,
					value,
					label,
					st::settingsSendType),
				st::settingsSendTypePadding);
		};
		addRadio(Level::Off, u"关闭"_q);
		addRadio(Level::LogOnly, u"仅记录（不弹窗，观察误报/漏报）"_q);
		addRadio(Level::Lenient, u"宽松（仅强信号弹窗退出）"_q);
		addRadio(Level::Strict, u"严格（任意信号弹窗退出）"_q);
		group->value(
		) | rpl::filter([=](Level value) {
			return value != Intro::Employee::InjectionProbeLevel();
		}) | rpl::on_next([=](Level value) {
			Intro::Employee::SetInjectionProbeLevel(value);
		}, content->lifetime());
	}
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::single(
			u"检测进程内是否存在外部注入/API 挂钩。命中后按档位落盘日志或弹窗退出。"
			u"档位越高越灵敏、误报越多。"_q));

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
