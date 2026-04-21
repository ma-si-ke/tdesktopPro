/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "employee/employee_login_box.h"

#include "employee/employee_storage.h"
#include "core/application.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QSysInfo>
#include <QtCore/QUuid>

namespace Employee {
namespace {

const auto kDeviceIdFilename = u"employee_device_id.txt"_q;

[[nodiscard]] QString ObtainOrCreateDeviceId() {
	const auto dir = QStandardPaths::writableLocation(
		QStandardPaths::AppDataLocation);
	const auto path = dir + u"/"_q + kDeviceIdFilename;
	{
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			const auto content = QString::fromLatin1(file.readAll()).trimmed();
			if (content.size() >= 36) {
				return content;
			}
		}
	}
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QDir().mkpath(QFileInfo(path).absolutePath());
	{
		auto file = QFile(path);
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(id.toLatin1());
		}
	}
	return id;
}

[[nodiscard]] QString DeviceInfoString() {
	return u"tdesktop on %1 (%2)"_q
		.arg(QSysInfo::prettyProductName())
		.arg(QSysInfo::currentCpuArchitecture());
}

struct LoginState {
	AuthClient client;
	bool pending = false;
	Fn<void(bool)> submit;
};

// PasswordInput 基类是 QLineEdit 而非 Ui::RpWidget，不能直接给
// GenericBox::addRow(object_ptr<RpWidget>) 用。套一层 RpWidget 并
// 同步尺寸。
[[nodiscard]] Ui::PasswordInput *AddPasswordRow(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> placeholder) {
	auto wrap = object_ptr<Ui::RpWidget>(box);
	const auto wrapPtr = wrap.data();
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		wrapPtr,
		st::defaultInputField,
		std::move(placeholder));
	field->move(0, 0);
	field->heightValue(
	) | rpl::on_next([=](int height) {
		wrapPtr->resize(wrapPtr->width(), height);
	}, field->lifetime());
	wrapPtr->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(width, field->height());
	}, field->lifetime());
	box->addRow(std::move(wrap));
	return field;
}

void ShowClearCacheConfirm(not_null<Ui::GenericBox*> parentBox) {
	parentBox->getDelegate()->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"清除缓存"_q));
		const auto layout = box->verticalLayout();

		const auto addLine = [&](const QString &text) {
			layout->add(
				object_ptr<Ui::FlatLabel>(box, text, st::boxLabel),
				st::boxRowPadding);
		};
		addLine(u"确定要清除所有缓存数据吗？"_q);
		addLine(u"清除后软件将自动重启，您需要重新登录。"_q);

		box->addButton(rpl::single(u"确认清除"_q), [=] {
			ClearSession();
			// 清空整个 tdata 目录——tdesktop 重启后会把自己当作全新安装。
			QDir(cWorkingDir() + u"tdata"_q).removeRecursively();
			box->closeBox();
			Core::Restart();
		});
		box->addButton(rpl::single(u"取消"_q), [=] {
			box->closeBox();
		});
	}));
}

void ShowConflictBox(
		not_null<Ui::GenericBox*> parentBox,
		const LoginFailure &failure,
		Fn<void()> onForce) {
	parentBox->getDelegate()->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"账号已在其他设备登录"_q));
		const auto layout = box->verticalLayout();

		const auto addLine = [&](const QString &text) {
			layout->add(
				object_ptr<Ui::FlatLabel>(box, text, st::boxLabel),
				st::boxRowPadding);
		};
		if (!failure.conflictDeviceInfo.isEmpty()) {
			addLine(u"设备: "_q + failure.conflictDeviceInfo);
		}
		if (!failure.conflictLastActiveAt.isEmpty()) {
			addLine(u"最后活跃: "_q + failure.conflictLastActiveAt);
		}
		Ui::AddSkip(layout);
		addLine(u"是否强制登录并踢出其他设备？"_q);

		box->addButton(rpl::single(u"强制登录"_q), [=] {
			box->closeBox();
			onForce();
		});
		box->addButton(rpl::single(u"取消"_q), [=] {
			box->closeBox();
		});
	}));
}

} // namespace

void EmployeeLoginBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(LoginSuccess)> onLoggedIn) {
	box->setTitle(rpl::single(u"TelegramPro"_q));
	box->setWidth(st::boxWidth);

	const auto layout = box->verticalLayout();

	Ui::AddSkip(layout);
	layout->add(
		object_ptr<Ui::FlatLabel>(
			box,
			u"请使用工号和密码登录"_q,
			st::boxLabel),
		st::boxRowPadding);
	Ui::AddSkip(layout);

	layout->add(
		object_ptr<Ui::FlatLabel>(
			box,
			u"后端服务器"_q,
			st::boxLabel),
		st::boxRowPadding);
	const auto backendGroup = std::make_shared<
		Ui::RadioenumGroup<BackendType>>(kDefaultBackend);
	for (const auto &option : BackendOptions()) {
		layout->add(
			object_ptr<Ui::Radioenum<BackendType>>(
				box,
				backendGroup,
				option.type,
				option.label,
				st::defaultBoxCheckbox),
			st::boxRowPadding);
	}
	Ui::AddSkip(layout);

	const auto employeeIdField = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"工号"_q)));

	const auto passwordField = AddPasswordRow(box, rpl::single(u"密码"_q));

	const auto errorLabel = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::boxLabel));
	errorLabel->setTextColorOverride(st::boxTextFgError->c);
	errorLabel->hide();

	const auto state = box->lifetime().make_state<LoginState>();
	const auto deviceId = ObtainOrCreateDeviceId();

	state->submit = [=](bool forceLogin) {
		if (state->pending) {
			return;
		}
		const auto employeeId = employeeIdField->getLastText().trimmed();
		const auto password = passwordField->getLastText();
		if (employeeId.isEmpty() || password.isEmpty()) {
			errorLabel->setText(u"请输入工号和密码"_q);
			errorLabel->show();
			return;
		}

		errorLabel->hide();
		state->pending = true;

		auto request = LoginRequest();
		request.employeeId = employeeId;
		request.password = password;
		request.deviceId = deviceId;
		request.deviceInfo = DeviceInfoString();
		request.backend = backendGroup->current();
		request.forceLogin = forceLogin;

		state->client.submit(request, [=](LoginResult result) {
			state->pending = false;
			if (auto success = std::get_if<LoginSuccess>(&result)) {
				// Close the box first: the caller's onLoggedIn likely
				// restarts MTP and creates a Main::Session, which fires
				// rpl events that re-layer the window (destroying our
				// parent layer). Running onLoggedIn after closeBox (and
				// deferred to the next event-loop tick) lets this
				// callback unwind before tdesktop tears us down.
				auto login = std::move(*success);
				box->closeBox();
				crl::on_main(
					[cb = onLoggedIn, login = std::move(login)]() mutable {
						cb(std::move(login));
					});
				return;
			}
			const auto &failure = std::get<LoginFailure>(result);
			if (failure.error == LoginError::AlreadyOnline) {
				ShowConflictBox(box, failure, [=] {
					state->submit(true);
				});
				return;
			}
			errorLabel->setText(failure.message);
			errorLabel->show();
		});
	};

	box->addButton(rpl::single(u"登录"_q), [=] {
		state->submit(false);
	});
	box->addButton(rpl::single(u"清除缓存"_q), [=] {
		ShowClearCacheConfirm(box);
	});

	box->setFocusCallback([=] {
		employeeIdField->setFocusFast();
	});

	employeeIdField->submits(
	) | rpl::on_next([=] {
		passwordField->setFocus();
	}, box->lifetime());

	// PasswordInput 继承自 MaskedInputField，用经典 Qt 信号 submitted()，
	// 不是 rpl producer——直接用 QObject::connect。
	QObject::connect(
		passwordField,
		&Ui::MaskedInputField::submitted,
		box,
		[=](Qt::KeyboardModifiers) { state->submit(false); });
}

} // namespace Employee
