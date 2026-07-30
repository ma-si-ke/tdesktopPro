/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_plugins.h"

#include "lang/lang_keys.h"
#include "plugins/plugin_market.h"
#include "plugins/plugin_registry.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

[[nodiscard]] QString FormatSize(int64 size) {
	if (size >= 1024 * 1024) {
		return u"%1 MB"_q.arg(size / (1024. * 1024.), 0, 'f', 1);
	}
	return u"%1 KB"_q.arg(std::max(size / 1024, int64(1)));
}

class PluginsSettings : public Section<PluginsSettings> {
public:
	PluginsSettings(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void refreshInstalled();
	void refreshMarket();
	void requestMarket();
	void showInstalledMenu(const QString &name);
	void download(const Plugins::MarketEntry &entry, bool asUpdate);

	const not_null<Window::SessionController*> _controller;
	Ui::VerticalLayout *_installed = nullptr;
	Ui::VerticalLayout *_market = nullptr;
	std::vector<Plugins::MarketEntry> _entries;
	QString _marketStatus;
	QString _downloading;
	bool _requesting = false;

};

PluginsSettings::PluginsSettings(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> PluginsSettings::title() {
	return rpl::single(u"插件管理"_q);
}

void PluginsSettings::showInstalledMenu(const QString &name) {
	const auto entry = Plugins::Find(name);
	if (!entry) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	if (entry->deleted) {
		menu->addAction(u"撤销删除"_q, [=] {
			Plugins::MarkDeleted(name, false);
		}, &st::menuIconRestore);
	} else {
		const auto remove = [=] {
			_controller->show(Ui::MakeConfirmBox({
				.text = u"删除插件 %1？插件将在下次启动时移除。"_q.arg(name),
				.confirmed = [=](Fn<void()> close) {
					Plugins::MarkDeleted(name, true);
					close();
				},
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		};
		menu->addAction(base::make_unique_q<Ui::Menu::Action>(
			menu->menu(),
			st::menuWithIconsAttention,
			Ui::Menu::CreateAction(
				menu->menu().get(),
				tr::lng_box_delete(tr::now),
				remove),
			&st::menuIconDeleteAttention,
			&st::menuIconDeleteAttention));
	}
	menu->popup(QCursor::pos());
}

void PluginsSettings::refreshInstalled() {
	if (!_installed) {
		return;
	}
	_installed->clear();
	const auto &list = Plugins::List();
	if (list.empty()) {
		_installed->add(
			object_ptr<Ui::FlatLabel>(
				_installed,
				rpl::single(u"尚未安装任何插件。"_q),
				st::boxDividerLabel),
			st::defaultBoxDividerLabelPadding);
	}
	for (const auto &entry : list) {
		const auto name = entry.name;
		const auto status = entry.deleted
			? u"已标记删除，重启后移除"_q
			: entry.pendingVersion
			? u"已下载 v%1，重启后生效"_q.arg(entry.pendingVersion)
			: u"版本 v%1"_q.arg(entry.version);
		const auto row = _installed->add(object_ptr<Ui::SettingsButton>(
			_installed,
			rpl::single(name + u"  —  "_q + status),
			st::settingsButtonNoIcon));
		if (entry.deleted) {
			row->setColorOverride(st::windowSubTextFg->c);
		}
		row->setClickedCallback([=] {
			showInstalledMenu(name);
		});
	}
	if (const auto width = this->width()) {
		_installed->resizeToWidth(width);
	}
}

void PluginsSettings::download(
		const Plugins::MarketEntry &entry,
		bool asUpdate) {
	if (!_downloading.isEmpty()) {
		return;
	}
	_downloading = entry.name;
	refreshMarket();

	const auto weak = base::make_weak(this);
	Plugins::Download(entry, asUpdate, nullptr, [=](
			Plugins::DownloadResult result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->_downloading = QString();
		strong->refreshMarket();
		if (!result.ok) {
			strong->_controller->show(Ui::MakeInformBox(result.error));
		} else {
			strong->_controller->showToast(result.asUpdate
				? u"下载完成，重启后更新生效。"_q
				: u"安装完成，重启后生效。"_q);
		}
	});
}

void PluginsSettings::refreshMarket() {
	if (!_market) {
		return;
	}
	_market->clear();
	if (!_marketStatus.isEmpty()) {
		_market->add(
			object_ptr<Ui::FlatLabel>(
				_market,
				rpl::single(_marketStatus),
				st::boxDividerLabel),
			st::defaultBoxDividerLabelPadding);
	}
	for (const auto &entry : _entries) {
		const auto installed = Plugins::Find(entry.name);
		const auto pending = installed
			? std::max(installed->pendingVersion, installed->version)
			: 0;
		const auto asUpdate = (installed != nullptr);
		const auto status = (_downloading == entry.name)
			? u"下载中…"_q
			: !installed
			? u"可安装 v%1 · %2"_q.arg(entry.version).arg(
				FormatSize(entry.size))
			: (entry.version > pending)
			? u"可更新至 v%1 · %2"_q.arg(entry.version).arg(
				FormatSize(entry.size))
			: u"已安装"_q;
		const auto row = _market->add(object_ptr<Ui::SettingsButton>(
			_market,
			rpl::single(entry.name + u"  —  "_q + status),
			st::settingsButtonNoIcon));
		const auto usable = _downloading.isEmpty()
			&& (!installed || entry.version > pending);
		if (!usable) {
			row->setColorOverride(st::windowSubTextFg->c);
			row->setDisabled(true);
		} else {
			const auto copy = entry;
			row->setClickedCallback([=] { download(copy, asUpdate); });
		}
		if (!entry.description.isEmpty()) {
			_market->add(
				object_ptr<Ui::FlatLabel>(
					_market,
					rpl::single(entry.description),
					st::boxDividerLabel),
				st::defaultBoxDividerLabelPadding);
		}
	}
	if (const auto width = this->width()) {
		_market->resizeToWidth(width);
	}
}

void PluginsSettings::requestMarket() {
	if (_requesting) {
		return;
	}
	_requesting = true;
	_marketStatus = u"正在获取插件列表…"_q;
	refreshMarket();

	const auto weak = base::make_weak(this);
	Plugins::RequestMarket([=](Plugins::MarketList list) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->_requesting = false;
		strong->_entries = std::move(list.entries);
		strong->_marketStatus = list.failed
			? u"获取插件列表失败，请稍后重试。"_q
			: strong->_entries.empty()
			? u"暂无可用插件。"_q
			: QString();
		strong->refreshMarket();
	});
}

void PluginsSettings::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"已安装插件"_q));
	_installed = content->add(object_ptr<Ui::VerticalLayout>(content));

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"插件市场"_q));
	_market = content->add(object_ptr<Ui::VerticalLayout>(content));

	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::single(
			u"插件保存在程序目录的 plugins 文件夹内，"
			u"安装、更新和删除都在下次启动时生效。"
			u"手动放入的文件不会被加载，启动时会被清理。"_q));

	Plugins::Changes(
	) | rpl::on_next([=] {
		refreshInstalled();
		refreshMarket();
	}, content->lifetime());

	refreshInstalled();
	requestMarket();

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type PluginsId() {
	return PluginsSettings::Id();
}

} // namespace Settings
