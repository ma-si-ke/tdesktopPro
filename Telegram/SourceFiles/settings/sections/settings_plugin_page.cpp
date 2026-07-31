/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_plugin_page.h"

#include "base/timer.h"
#include "plugins/plugin_instance.h"
#include "plugins/plugin_manifest.h"
#include "plugins/plugin_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

using namespace Plugins;

struct PageState {
	// Text shown by labels the manifest binds by id.
	base::flat_map<QString, rpl::variable<QString>> texts;
	rpl::event_stream<> settingsChanged;
	base::Timer cooldownTimer;
};

[[nodiscard]] rpl::producer<QString> BoundText(
		not_null<PageState*> state,
		const QString &bind,
		const QString &fallback) {
	auto i = state->texts.find(bind);
	if (i == end(state->texts)) {
		i = state->texts.emplace(bind, rpl::variable<QString>(fallback)).first;
	}
	return i->second.value();
}

void ApplyResult(
		not_null<Window::SessionController*> controller,
		not_null<PageState*> state,
		const CallResult &result) {
	for (const auto &[key, value] : result.texts) {
		auto i = state->texts.find(key);
		if (i == end(state->texts)) {
			state->texts.emplace(key, rpl::variable<QString>(value));
		} else {
			i->second = value;
		}
	}
	if (!result.error.isEmpty()) {
		controller->show(Ui::MakeInformBox(result.error));
	} else if (!result.toast.isEmpty()) {
		controller->showToast(result.toast);
	}
}

[[nodiscard]] bool RowVisible(
		not_null<Instance*> instance,
		const Row &row) {
	const auto &manifest = instance->manifest();
	for (const auto &condition : row.visibleWhen) {
		if (condition.isBool) {
			if (Plugins::Settings::ReadBool(manifest, condition.setting)
				!= condition.boolValue) {
				return false;
			}
		} else if (Plugins::Settings::ReadInt(manifest, condition.setting)
			!= condition.intValue) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] Ui::NumberInput *AddNumberField(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> placeholder,
		int value,
		int limit) {
	// NumberInput is a MaskedInputField, not an RpWidget, so it needs a
	// row of its own to live in.
	const auto wrap = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	const auto field = Ui::CreateChild<Ui::NumberInput>(
		wrap,
		st::defaultInputField,
		std::move(placeholder),
		QString::number(value),
		limit);
	wrap->resize(wrap->width(), field->height());
	wrap->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(width, field->height());
	}, wrap->lifetime());
	return field;
}

} // namespace

void FillPluginContent(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<Plugins::Instance*> instance) {
	const auto &manifest = instance->manifest();
	const auto usable = instance->available();
	const auto state = container->lifetime().make_state<PageState>();

	if (!usable) {
		Ui::AddSkip(container);
		Ui::AddDividerText(
			container,
			rpl::single(instance->unavailableReason()));
	}

	// A row is rebuilt when a setting it depends on changes, so the
	// wraps only have to follow the conditions they declare.
	const auto applyResult = [=](const CallResult &result) {
		ApplyResult(controller, state, result);
	};

	instance->tickResults(
	) | rpl::on_next([=](CallResult result) {
		applyResult(result);
	}, container->lifetime());

	for (const auto &row : manifest.rows) {
		const auto wrap = row.visibleWhen.empty()
			? nullptr
			: container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					container,
					object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap ? wrap->entity() : container.get();
		if (wrap) {
			wrap->toggle(RowVisible(instance, row), anim::type::instant);
			state->settingsChanged.events(
			) | rpl::on_next([=] {
				wrap->toggle(RowVisible(instance, row), anim::type::normal);
			}, wrap->lifetime());
		}
		switch (row.type) {
		case RowType::Button: {
			const auto action = row.action;
			const auto async = row.async;
			const auto cooldown = row.cooldown;
			const auto text = row.text;
			const auto value = inner->lifetime().make_state<
				rpl::variable<QString>>(text);
			const auto update = [=] {
				const auto left = instance->cooldownLeft(action);
				*value = (left > 0)
					? u"%1（%2 秒后可用）"_q.arg(text).arg(left)
					: text;
			};
			const auto button = inner->add(object_ptr<Ui::SettingsButton>(
				inner,
				value->value(),
				st::settingsButtonNoIcon));
			if (!usable) {
				button->setDisabled(true);
				button->setColorOverride(st::windowSubTextFg->c);
			} else {
				button->setClickedCallback([=] {
					if (instance->cooldownLeft(action) > 0
						|| instance->running()) {
						return;
					}
					instance->call(action, async, cooldown, [=](
							CallResult result) {
						applyResult(result);
						update();
					});
					update();
				});
				if (cooldown > 0) {
					state->cooldownTimer.setCallback([=] { update(); });
					state->cooldownTimer.callEach(crl::time(1000));
				}
			}
		} break;
		case RowType::Toggle: {
			const auto setting = row.setting;
			const auto toggle = inner->add(object_ptr<Ui::SettingsButton>(
				inner,
				rpl::single(row.text),
				st::settingsButtonNoIcon
			))->toggleOn(rpl::single(
				Plugins::Settings::ReadBool(manifest, setting)));
			if (!usable) {
				toggle->setDisabled(true);
				toggle->setColorOverride(st::windowSubTextFg->c);
				toggle->setToggleLocked(true);
			} else {
				toggle->toggledValue(
				) | rpl::filter([=](bool checked) {
					return checked
						!= Plugins::Settings::ReadBool(manifest, setting);
				}) | rpl::on_next([=](bool checked) {
					Plugins::Settings::WriteBool(manifest, setting, checked);
					state->settingsChanged.fire({});
				}, toggle->lifetime());
			}
		} break;
		case RowType::Radio: {
			const auto setting = row.setting;
			const auto group = std::make_shared<Ui::RadiobuttonGroup>(
				Plugins::Settings::ReadInt(manifest, setting));
			for (const auto &option : row.options) {
				inner->add(
					object_ptr<Ui::Radiobutton>(
						inner,
						group,
						option.value,
						option.text,
						st::settingsSendType),
					st::settingsSendTypePadding);
			}
			if (usable) {
				group->setChangedCallback([=](int value) {
					if (value
						!= Plugins::Settings::ReadInt(manifest, setting)) {
						Plugins::Settings::WriteInt(manifest, setting, value);
						state->settingsChanged.fire({});
					}
				});
			}
		} break;
		case RowType::Number: {
			const auto setting = row.setting;
			const auto definition = FindSetting(manifest, setting);
			if (!definition) {
				break;
			}
			const auto field = AddNumberField(
				inner,
				rpl::single(row.text),
				Plugins::Settings::ReadInt(manifest, setting),
				definition->max);
			if (!usable) {
				field->setEnabled(false);
			} else {
				QObject::connect(
					field,
					&Ui::NumberInput::changed,
					field,
					[=] {
						const auto value = field->getLastText().toInt();
						if (value > 0) {
							Plugins::Settings::WriteInt(
								manifest,
								setting,
								value);
						}
					});
			}
		} break;
		case RowType::Text:
		case RowType::Label: {
			auto text = row.bind.isEmpty()
				? rpl::single(row.text)
				: BoundText(state, row.bind, row.text);
			if (row.type == RowType::Label) {
				Ui::AddSkip(inner);
				Ui::AddDividerText(inner, std::move(text));
				Ui::AddSkip(inner);
			} else {
				inner->add(
					object_ptr<Ui::FlatLabel>(
						inner,
						std::move(text),
						st::boxLabel),
					st::boxRowPadding);
			}
		} break;
		case RowType::Divider:
			Ui::AddSkip(inner);
			Ui::AddDivider(inner);
			Ui::AddSkip(inner);
			break;
		}
	}

	// Labels bound to a value are filled by asking the plugin once the
	// page is open, the same answer shape a tick returns.
	if (usable) {
		instance->call(u"status"_q, false, 0, applyResult);
	}
}

namespace {

class PluginPage final : public AbstractSection {
public:
	PluginPage(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		QString name,
		Type id);

	[[nodiscard]] Type id() const override {
		return _id;
	}
	[[nodiscard]] rpl::producer<QString> title() override;

private:
	const not_null<Window::SessionController*> _controller;
	const QString _name;
	const Type _id;

};

class PluginPageFactory final : public AbstractSectionFactory {
public:
	explicit PluginPageFactory(QString name) : _name(std::move(name)) {
	}

	[[nodiscard]] object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue) const override;

	void setId(Type id) {
		_id = std::move(id);
	}

private:
	const QString _name;
	Type _id;

};

PluginPage::PluginPage(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	QString name,
	Type id)
: AbstractSection(parent, controller)
, _controller(controller)
, _name(std::move(name))
, _id(std::move(id)) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	if (const auto instance = Plugins::FindLoaded(_name)) {
		FillPluginContent(content, controller, instance);
	}
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> PluginPage::title() {
	const auto instance = Plugins::FindLoaded(_name);
	return rpl::single(instance ? instance->manifest().title : _name);
}

object_ptr<AbstractSection> PluginPageFactory::create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue) const {
	return object_ptr<PluginPage>(parent, controller, _name, _id);
}

} // namespace

Type PluginPageId(const QString &name) {
	static auto factories = base::flat_map<
		QString,
		std::shared_ptr<PluginPageFactory>>();
	const auto i = factories.find(name);
	if (i != end(factories)) {
		return i->second;
	}
	const auto factory = std::make_shared<PluginPageFactory>(name);
	factory->setId(factory);
	factories.emplace(name, factory);
	return factory;
}

} // namespace Settings
