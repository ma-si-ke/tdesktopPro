/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/intro_step.h"
#include "intro/employee/employee_auth.h"
#include "intro/employee/employee_config.h"
#include "base/timer.h"

namespace Ui {
class InputField;
class PasswordInput;
class AbstractButton;
class FlatLabel;
} // namespace Ui

namespace Intro::Employee {

class EmployeeLoginStep final : public Intro::details::Step {
public:
	EmployeeLoginStep(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Intro::details::Data*> data);
	~EmployeeLoginStep();

	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupLayout();
	void openBackendMenu();
	void chooseBackend(BackendType backend);
	void refreshBackendLabel();
	void lockInputs(bool locked);
	void showLocalError(QString text);

	void startLogin();
	void onLoginSuccess(AuthSuccess result);
	void onLoginFailure(AuthFailure result);
	void injectAndFetchSelf(AuthSuccess result);
	void fetchSelf();
	void onSelfLoaded(const MTPUser &user);
	void onSelfFailed(const MTP::Error &err);
	void resetForRetry();

	object_ptr<Ui::InputField> _username;
	object_ptr<Ui::PasswordInput> _password;
	object_ptr<Ui::FlatLabel> _backendLabel;
	object_ptr<Ui::AbstractButton> _backendButton;
	object_ptr<Ui::FlatLabel> _errorLabel;

	BackendType _chosenBackend = BackendType::Customer;

	std::unique_ptr<AuthClient> _auth;
	mtpRequestId _getSelfRequestId = 0;
	base::Timer _mtpConnectTimeout;
	rpl::lifetime _mtpWatch;

};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
