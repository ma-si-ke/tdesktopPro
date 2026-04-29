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
#include "main/main_account.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_auth_key.h"
#include "styles/style_intro.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/abstract_button.h"

namespace Intro::Employee {
namespace {

constexpr auto kMtpConnectTimeoutMs = crl::time(15 * 1000);

} // namespace

EmployeeLoginStep::EmployeeLoginStep(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Intro::details::Data*> data)
: Step(parent, account, data)
, _username(this, st::introPassword, tr::lng_employee_label_user())
, _password(this, st::introPassword, tr::lng_employee_label_pass())
, _backendLabel(this, QString(), st::introDescription)
, _backendButton(this)
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
	const auto fieldWidth = st::introPassword.width;
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
	// 使用基类 Step::showError —— 位置由 st::introErrorTop 控制，
	// 和 next 按钮的布局互不干扰。
	showError(rpl::single(text));
}

void EmployeeLoginStep::submit() {
	if (_auth || _bootstrapInFlight) {
		// HTTP is in flight OR HTTP succeeded and we're mid-bootstrap
		// (MTP rebuilt, waiting for users.GetUsers). A second entry
		// would tear down the active _mtp under the in-flight request
		// → UAF. Ignore until the flow completes or resetForRetry runs.
		return;
	}
	startLogin();
}

void EmployeeLoginStep::startLogin() {
	const auto username = _username->getLastText().trimmed();
	const auto password = _password->getLastText();
	if (username.isEmpty() || password.isEmpty()) {
		showLocalError(tr::lng_employee_err_empty(tr::now));
		return;
	}

	showLocalError(QString());
	lockInputs(true);

	const auto &info = BackendInfoFor(_chosenBackend);
	LOG(("Employee: login attempt backend=%1 user=%2"
		).arg(info.label
		).arg(username));

	_auth = std::make_unique<AuthClient>(this);
	_auth->login(
		_chosenBackend,
		username,
		password,
		crl::guard(this, [this](AuthResult result) {
			if (auto *success = std::get_if<AuthSuccess>(&result)) {
				onLoginSuccess(*success);
			} else {
				onLoginFailure(std::get<AuthFailure>(result));
			}
		}));
}

void EmployeeLoginStep::onLoginFailure(AuthFailure result) {
	LOG(("Employee: login failed kind=%1").arg(int(result.kind)));
	_auth.reset();
	lockInputs(false);
	showLocalError(result.message);
}

void EmployeeLoginStep::onLoginSuccess(AuthSuccess result) {
	LOG(("Employee: login ok dcId=%1 userId=%2"
		).arg(result.dcId
		).arg(result.userId.bare));
	_auth.reset();
	_bootstrapInFlight = true;  // stays true until finish() or resetForRetry
	injectAndFetchSelf(result);
}

void EmployeeLoginStep::injectAndFetchSelf(AuthSuccess result) {
	Expects(result.authKey.size() == int(MTP::AuthKey::kSize));

	// Copy the 256 raw bytes into MTP::AuthKey::Data
	// (std::array<gsl::byte, 256>), mirroring the pattern used in
	// storage/details/storage_settings_scheme.cpp:158 and
	// main/main_account.cpp:404.
	auto data = MTP::AuthKey::Data{ { gsl::byte{} } };
	memcpy(
		data.data(),
		result.authKey.constData(),
		MTP::AuthKey::kSize);
	// Diagnostic: first 4 bytes of raw key (hex) — cheap sanity check
	// that the data block is not all zeros, all FFs, or obviously byte-
	// reversed vs what a real Telegram authKey looks like.
	const auto rawHead = result.authKey.left(4).toHex();
	const auto rawTail = result.authKey.right(4).toHex();
	LOG(("Employee: authkey raw bytes head=%1 tail=%2 size=%3"
		).arg(QString::fromLatin1(rawHead)
		).arg(QString::fromLatin1(rawTail)
		).arg(result.authKey.size()));

	auto key = std::make_shared<MTP::AuthKey>(
		MTP::AuthKey::Type::ReadFromFile,
		result.dcId,
		data);
	// keyId is sha1(authKey)[-8..] as uint64 — 0 or all-bits-set would
	// indicate bad data; real keys are large random-looking values.
	LOG(("Employee: authkey keyId=%1 dcId=%2 type=%3"
		).arg(key->keyId()
		).arg(key->dcId()
		).arg(int(key->type())));

	LOG(("Employee: step=before_applyBootstrap"));
	account().applyEmployeeBootstrap(
		result.dcId,
		std::move(key),
		result.userId,
		result.employeeName,
		result.token,
		result.permissions,
		_chosenBackend);
	LOG(("Employee: step=after_applyBootstrap"));

	// Watch for the first main-session emission: signals that MTP is up
	// with our injected key. rpl::take(1) so we don't re-fetch on later
	// main-DC changes.
	_mtpWatch.destroy();
	account().mtpMainSessionValue(
	) | rpl::take(1) | rpl::on_next([=](not_null<MTP::Instance*>) {
		LOG(("Employee: step=mtp_session_emitted"));
		fetchSelf();
	}, _mtpWatch);
	LOG(("Employee: step=subscribed_to_mtp"));

	_mtpConnectTimeout.callOnce(kMtpConnectTimeoutMs);
	LOG(("Employee: step=timer_armed"));
}

void EmployeeLoginStep::fetchSelf() {
	LOG(("Employee: step=fetchSelf_enter"));
	auto &sender = api();
	LOG(("Employee: step=fetchSelf_after_api"));
	auto builder = sender.request(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())));
	LOG(("Employee: step=fetchSelf_after_build"));
	_getSelfRequestId = std::move(builder).done([=](
			const MTPVector<MTPUser> &users) {
		LOG(("Employee: step=getUsers_done users=%1").arg(users.v.size()));
		_getSelfRequestId = 0;
		_mtpConnectTimeout.cancel();
		if (users.v.size() != 1) {
			onSelfFailed(MTP::Error::Local(
				u"BAD_RESPONSE"_q,
				u"expected 1 user in GetUsers response"_q));
			return;
		}
		onSelfLoaded(users.v.front());
	}).fail([=](const MTP::Error &err) {
		LOG(("Employee: step=getUsers_fail type=%1").arg(err.type()));
		_getSelfRequestId = 0;
		_mtpConnectTimeout.cancel();
		onSelfFailed(err);
	}).send();
	LOG(("Employee: step=getUsers_sent id=%1").arg(_getSelfRequestId));
}

void EmployeeLoginStep::onSelfLoaded(const MTPUser &user) {
	LOG(("Employee: step=onSelfLoaded_enter type=%1").arg(user.type()));
	Prefs::SetLastBackend(_chosenBackend);
	Prefs::SetLastUsername(_username->getLastText().trimmed());
	if (user.type() != mtpc_user) {
		LOG(("Employee: step=onSelfLoaded_not_user"));
		showLocalError(tr::lng_employee_err_bad_json(tr::now));
		return;
	}
	const auto &data = user.c_user();
	LOG(("Employee: step=onSelfLoaded_ok isSelf=%1 hasFirstName=%2 id=%3"
		).arg(data.is_self() ? "Y" : "N"
		).arg(data.vfirst_name() ? "Y" : "N"
		).arg(data.vid().v));
	LOG(("Employee: bootstrap complete, calling finish(user)"));
	finish(user);
	// This line may not execute — finish() can trigger Step destruction
	// through setupMain(). If you see it, finish() returned normally.
	LOG(("Employee: step=finish_returned"));
}

void EmployeeLoginStep::onSelfFailed(const MTP::Error &err) {
	LOG(("Employee: getUsers failed type=%1").arg(err.type()));
	resetForRetry();

	if (err.type().startsWith(u"FLOOD_WAIT_"_q)) {
		const auto seconds = err.type().mid(
			QString(u"FLOOD_WAIT_"_q).size()).toInt();
		showLocalError(tr::lng_employee_err_flood(
			tr::now,
			lt_seconds,
			QString::number(seconds)));
		return;
	} else if (err.type() == u"TIMEOUT"_q) {
		showLocalError(tr::lng_employee_err_mtp_timeout(tr::now));
		return;
	}
	showLocalError(tr::lng_employee_err_mtp_auth(tr::now));
}

void EmployeeLoginStep::resetForRetry() {
	_mtpWatch.destroy();
	_mtpConnectTimeout.cancel();
	if (_getSelfRequestId) {
		api().request(_getSelfRequestId).cancel();
		_getSelfRequestId = 0;
	}
	account().applyEmployeeReset();
	_bootstrapInFlight = false;
	lockInputs(false);
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
