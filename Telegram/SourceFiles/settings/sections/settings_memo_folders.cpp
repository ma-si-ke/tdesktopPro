/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_memo_folders.h"

#include "data/data_memo_messages.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

void EditNameBox(
		not_null<Ui::GenericBox*> box,
		const QString &current,
		Fn<void(QString, Fn<void()>)> submit) {
	box->setTitle(current.isEmpty()
		? tr::lng_memo_folder_new()
		: tr::lng_memo_folder_rename());
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_memo_folder_name(),
		current));
	box->setFocusCallback([=] {
		field->setFocusFast();
		field->selectAll();
	});
	const auto save = [=] {
		const auto value = field->getLastText().trimmed();
		if (value.isEmpty()) {
			field->showError();
			return;
		}
		submit(value, crl::guard(box, [=] { box->closeBox(); }));
	};
	field->submits() | rpl::on_next(save, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

class MemoFolders : public Section<MemoFolders> {
public:
	MemoFolders(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void showRowMenu(uint64 folderId);

	const not_null<Window::SessionController*> _controller;

};

MemoFolders::MemoFolders(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> MemoFolders::title() {
	return tr::lng_memo_manage_folders();
}

void MemoFolders::showRowMenu(uint64 folderId) {
	const auto memo = &_controller->session().data().memoMessages();
	const auto folder = memo->folder(folderId);
	if (!folder) {
		return;
	}
	const auto name = folder->title;
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	menu->addAction(tr::lng_memo_folder_rename(tr::now), [=] {
		_controller->show(Box(
			EditNameBox,
			name,
			Fn<void(QString, Fn<void()>)>([=](
					QString value,
					Fn<void()> close) {
				memo->renameFolder(folderId, value);
				close();
			})));
	}, &st::menuIconEdit);
	const auto remove = [=] {
		_controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_memo_folder_delete_sure(tr::now, lt_name, name),
			.confirmed = [=](Fn<void()> close) {
				memo->removeFolder(folderId);
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
			tr::lng_memo_folder_delete(tr::now),
			remove),
		&st::menuIconDeleteAttention,
		&st::menuIconDeleteAttention));
	menu->popup(QCursor::pos());
}

void MemoFolders::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto memo = &_controller->session().data().memoMessages();

	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_memo_manage_about());
	Ui::AddSkip(content);

	const auto addWrap = content->add(
		object_ptr<Ui::VerticalLayout>(content));
	const auto add = addWrap->add(object_ptr<Ui::SettingsButton>(
		addWrap,
		tr::lng_memo_folder_new(),
		st::settingsButtonNoIcon));
	add->setClickedCallback([=] {
		_controller->show(Box(
			EditNameBox,
			QString(),
			Fn<void(QString, Fn<void()>)>([=](
					QString value,
					Fn<void()> close) {
				memo->createFolder(value);
				close();
			})));
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	const auto inner = content->add(
		object_ptr<Ui::VerticalLayout>(content));
	const auto rebuild = [=] {
		while (inner->count()) {
			delete inner->widgetAt(0);
		}
		for (const auto &folder : memo->folders()) {
			const auto id = folder.id;
			const auto row = inner->add(object_ptr<Ui::SettingsButton>(
				inner,
				rpl::single(folder.title),
				st::settingsButtonNoIcon));
			row->setAcceptBoth(true);
			row->addClickHandler([=](Qt::MouseButton button) {
				showRowMenu(id);
			});
		}
		inner->resizeToWidth(content->width());
	};
	memo->foldersChanged(
	) | rpl::on_next(rebuild, content->lifetime());
	rebuild();

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type MemoFoldersId() {
	return MemoFolders::Id();
}

} // namespace Settings
