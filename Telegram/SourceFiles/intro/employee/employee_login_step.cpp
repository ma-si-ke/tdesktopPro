/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_login_step.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_prefs.h"
#include "base/debug_log.h"
#include "lang/lang_keys.h"
#include "styles/style_intro.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/abstract_button.h"

namespace Intro::Employee {
namespace {

[[maybe_unused]] constexpr auto kMtpConnectTimeoutMs = crl::time(15 * 1000);

} // namespace

EmployeeLoginStep::EmployeeLoginStep(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Intro::details::Data*> data)
: Step(parent, account, data)
, _username(this, st::introPhone, tr::lng_employee_label_user())
, _password(this, st::introPassword, tr::lng_employee_label_pass())
, _backendLabel(this, QString(), st::introDescription)
, _backendButton(this)
, _errorLabel(this, QString(), st::introError)
, _chosenBackend(Prefs::LastBackend())
, _mtpConnectTimeout([=] {
	onSelfFailed(MTP::Error::Local(
		u"TIMEOUT"_q,
		u"employee mtp connect timeout"_q));
}) {
	setTitleText(tr::lng_employee_btn_login());
	setupLayout();
}

EmployeeLoginStep::~EmployeeLoginStep() {
	if (_auth) {
		_auth->cancel();
	}
	if (_getSelfRequestId) {
		api().request(_getSelfRequestId).cancel();
	}
	_mtpConnectTimeout.cancel();
}

void EmployeeLoginStep::setupLayout() {
	_username->setMaxLength(64);
	if (const auto remembered = Prefs::LastUsername(); !remembered.isEmpty()) {
		_username->setText(remembered);
	}
	_password->setMaxLength(64);

	_backendButton->setClickedCallback([=] { openBackendMenu(); });
	refreshBackendLabel();

	_errorLabel->setVisible(false);

	connect(
		_password,
		&Ui::PasswordInput::submitted,
		[=](Qt::KeyboardModifiers) { submit(); });

	_username->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers) {
		_password->setFocus();
	}, _username->lifetime());
}

void EmployeeLoginStep::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	const auto fieldWidth = st::introPhone.width;
	auto y = contentTop() + st::introStepFieldTop;

	_backendLabel->resizeToWidth(fieldWidth);
	_backendLabel->moveToLeft(contentLeft(), y);
	_backendButton->setGeometry(
		contentLeft(),
		y,
		fieldWidth,
		_backendLabel->height());
	y += _backendLabel->height() + st::introPhoneTop;

	_username->resize(fieldWidth, _username->height());
	_username->moveToLeft(contentLeft(), y);
	y += _username->height() + st::introPhoneTop;

	_password->resize(fieldWidth, _password->height());
	_password->moveToLeft(contentLeft(), y);
	y += _password->height() + st::introPhoneTop;

	_errorLabel->resizeToWidth(fieldWidth);
	_errorLabel->moveToLeft(contentLeft(), y);
}

rpl::producer<QString> EmployeeLoginStep::nextButtonText() const {
	return tr::lng_employee_btn_login();
}

void EmployeeLoginStep::openBackendMenu() {
	auto menu = base::make_unique_q<Ui::PopupMenu>(this);
	const auto &backends = AllBackends();
	for (auto i = 0; i != int(backends.size()); ++i) {
		const auto index = i;
		menu->addAction(backends[i].label, [=] {
			chooseBackend(BackendType(index));
		});
	}
	menu->popup(_backendButton->mapToGlobal(
		QPoint(0, _backendButton->height())));
	menu.release();
}

void EmployeeLoginStep::chooseBackend(BackendType backend) {
	_chosenBackend = backend;
	Prefs::SetLastBackend(backend);
	refreshBackendLabel();
}

void EmployeeLoginStep::refreshBackendLabel() {
	const auto &info = BackendInfoFor(_chosenBackend);
	_backendLabel->setText(
		tr::lng_employee_label_backend(tr::now) + u": "_q + info.label);
}

void EmployeeLoginStep::lockInputs(bool locked) {
	_username->setDisabled(locked);
	_password->setDisabled(locked);
	_backendButton->setDisabled(locked);
}

void EmployeeLoginStep::showLocalError(QString text) {
	_errorLabel->setText(text);
	_errorLabel->setVisible(!text.isEmpty());
}

void EmployeeLoginStep::submit() {
	DEBUG_LOG(("Employee: submit (skeleton)"));
}

void EmployeeLoginStep::startLogin() {
}

void EmployeeLoginStep::onLoginSuccess(AuthSuccess result) {
}

void EmployeeLoginStep::onLoginFailure(AuthFailure result) {
}

void EmployeeLoginStep::injectAndFetchSelf(AuthSuccess result) {
}

void EmployeeLoginStep::fetchSelf() {
}

void EmployeeLoginStep::onSelfLoaded(const MTPUser &user) {
}

void EmployeeLoginStep::onSelfFailed(const MTP::Error &err) {
}

void EmployeeLoginStep::resetForRetry() {
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
