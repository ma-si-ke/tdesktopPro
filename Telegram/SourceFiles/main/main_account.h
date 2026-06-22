/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtp_instance.h"
#include "base/weak_ptr.h"

#ifdef TDESKTOP_EMPLOYEE_MODE
#include "intro/employee/employee_config.h"
#include "intro/employee/employee_hidden_folders.h"
#include "intro/employee/employee_hidden_folders_client.h"
#include "intro/employee/employee_permissions.h"
#include "intro/employee/employee_verify.h"
#include "base/timer.h"
#endif

namespace Storage {
class Account;
class Domain;
enum class StartResult : uchar;
} // namespace Storage

namespace MTP {
class AuthKey;
class Config;
} // namespace MTP

namespace Main {

class Domain;
class Session;
class SessionSettings;
class AppConfig;

class Account final : public base::has_weak_ptr {
public:
	Account(not_null<Domain*> domain, const QString &dataName, int index);
	~Account();

	[[nodiscard]] Domain &domain() const {
		return *_domain;
	}

	[[nodiscard]] Storage::Domain &domainLocal() const;

	[[nodiscard]] Storage::StartResult legacyStart(
		const QByteArray &passcode);
	[[nodiscard]] std::unique_ptr<MTP::Config> prepareToStart(
		std::shared_ptr<MTP::AuthKey> localKey);
	void prepareToStartAdded(
		std::shared_ptr<MTP::AuthKey> localKey);
	void start(std::unique_ptr<MTP::Config> config);

#ifdef TDESKTOP_EMPLOYEE_MODE
	void applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId,
		QString employeeName,
		QString token,
		Intro::Employee::PermissionValues permissions,
		Intro::Employee::BackendType backend,
		QString openTime);
	void applyEmployeeReset();

	[[nodiscard]] const Intro::Employee::Permissions &
		employeePermissions() const;
	[[nodiscard]] const Intro::Employee::HiddenFolders &
		employeeHiddenFolders() const;
	[[nodiscard]] Intro::Employee::BackendType employeeBackend() const {
		return _employeeBackend;
	}
	[[nodiscard]] rpl::producer<bool> employeeTimeLockedValue() const;
	[[nodiscard]] bool employeeTimeLocked() const;
	[[nodiscard]] QString employeeOpenTime() const {
		return _employeeOpenTime;
	}
#endif

	[[nodiscard]] uint64 willHaveSessionUniqueId(MTP::Config *config) const;
	void createSession(
		const MTPUser &user,
		std::unique_ptr<SessionSettings> settings = nullptr);
	void createSession(
		UserId id,
		QByteArray serialized,
		int streamVersion,
		std::unique_ptr<SessionSettings> settings);

	void logOut();
	void forcedLogOut();
	[[nodiscard]] bool loggingOut() const;

	[[nodiscard]] AppConfig &appConfig() const {
		Expects(_appConfig != nullptr);

		return *_appConfig;
	}

	[[nodiscard]] Storage::Account &local() const {
		return *_local;
	}

	[[nodiscard]] bool sessionExists() const;
	[[nodiscard]] Session &session() const;
	[[nodiscard]] Session *maybeSession() const;
	[[nodiscard]] rpl::producer<Session*> sessionValue() const;
	[[nodiscard]] rpl::producer<Session*> sessionChanges() const;

	[[nodiscard]] MTP::Instance &mtp() const {
		return *_mtp;
	}
	[[nodiscard]] rpl::producer<not_null<MTP::Instance*>> mtpValue() const;

	// Each time the main session changes a new copy of the pointer is fired.
	// This allows to resend the requests that were not requiring auth, and
	// which could be forgotten without calling .done() or .fail() because
	// of the main dc changing.
	[[nodiscard]] auto mtpMainSessionValue() const
		-> rpl::producer<not_null<MTP::Instance*>>;

	// Set from legacy storage.
	void setLegacyMtpKey(std::shared_ptr<MTP::AuthKey> key);

	void setMtpMainDcId(MTP::DcId mainDcId);
	void setSessionUserId(UserId userId);
	void setSessionFromStorage(
		std::unique_ptr<SessionSettings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion);
	[[nodiscard]] SessionSettings *getSessionSettings();
	[[nodiscard]] rpl::producer<> mtpNewSessionCreated() const;
	[[nodiscard]] rpl::producer<MTPUpdates> mtpUpdates() const;

	// Serialization.
	[[nodiscard]] QByteArray serializeMtpAuthorization() const;
	void setMtpAuthorization(const QByteArray &serialized);

	void suggestMainDcId(MTP::DcId mainDcId);
	void destroyStaleAuthorizationKeys();

	void setHandleLoginCode(Fn<void(QString)> callback);
	void handleLoginCode(const QString &code) const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	static constexpr auto kDefaultSaveDelay = crl::time(1000);
	enum class DestroyReason {
		Quitting,
		LoggedOut,
	};

	void startMtp(std::unique_ptr<MTP::Config> config);
	void createSession(
		const MTPUser &user,
		QByteArray serialized,
		int streamVersion,
		std::unique_ptr<SessionSettings> settings);
	void watchProxyChanges();
	void watchSessionChanges();
	bool checkForUpdates(const MTP::Response &message);
	bool checkForNewSession(const MTP::Response &message);

	void destroyMtpKeys(MTP::AuthKeysList &&keys);
	void resetAuthorizationKeys();

	void loggedOut();
	void destroySession(DestroyReason reason);

	const not_null<Domain*> _domain;
	const std::unique_ptr<Storage::Account> _local;

	std::unique_ptr<MTP::Instance> _mtp;
	rpl::variable<MTP::Instance*> _mtpValue;
	std::unique_ptr<MTP::Instance> _mtpForKeysDestroy;
	rpl::event_stream<MTPUpdates> _mtpUpdates;
	rpl::event_stream<> _mtpNewSessionCreated;

	std::unique_ptr<AppConfig> _appConfig;

	std::unique_ptr<Session> _session;
	rpl::variable<Session*> _sessionValue;

	Fn<void(QString)> _handleLoginCode = nullptr;

	UserId _sessionUserId = 0;
	QByteArray _sessionUserSerialized;
	int32 _sessionUserStreamVersion = 0;
	std::unique_ptr<SessionSettings> _storedSessionSettings;
	MTP::Instance::Fields _mtpFields;
	MTP::AuthKeysList _mtpKeysToDestroy;
	bool _loggingOut = false;

#ifdef TDESKTOP_EMPLOYEE_MODE
	void kickOffEmployeeVerifyIfAuthorized();
	void onEmployeeVerifyInvalidToken();
	void handleEmployeeAuthMissingOrCorrupt();
	void startEmployeeVerifyTimer();
	void stopEmployeeVerifyTimer();
	void onEmployeeVerifyTimerTick();
	void fetchEmployeeHiddenFolders();
	void persistEmployeeAuthSnapshot();
	void recomputeEmployeeTimeLock();

	std::unique_ptr<Intro::Employee::Permissions> _employeePermissions;
	std::unique_ptr<Intro::Employee::HiddenFolders> _employeeHiddenFolders;
	std::unique_ptr<Intro::Employee::VerifyClient> _employeeVerify;
	std::unique_ptr<Intro::Employee::HiddenFoldersClient>
		_employeeHiddenFoldersClient;
	Intro::Employee::BackendType _employeeBackend =
		Intro::Employee::BackendType::Customer;
	base::Timer _employeeVerifyTimer;
	int _employeeVerifyConsecutiveFailures = 0;
	QString _employeeOpenTime;
	base::Timer _employeeOpenTimeTimer;
	rpl::variable<bool> _employeeTimeLocked = false;
#endif

	rpl::lifetime _lifetime;

};

} // namespace Main
