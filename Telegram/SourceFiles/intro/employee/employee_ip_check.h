/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <variant>

class QNetworkAccessManager;
class QNetworkReply;

namespace Intro::Employee {

inline constexpr auto kIpDetectionCount = 7;

enum class IpDetection {
	Proxy = 0,
	Vpn,
	Compromised,
	Scraper,
	Tor,
	Hosting,
	Anonymous,
};

struct IpCheckInfo {
	QString ip;
	QString provider;
	QString networkType;
	QString countryCode;
	QString countryName;
	QString regionName;
	QString cityName;
	std::array<bool, kIpDetectionCount> detections = { {} };
	int risk = 0;
	int confidence = 0;
};

struct IpCheckFailure {
	enum class Kind {
		Network,
		BadJson,
	};
	Kind kind = Kind::Network;
};

using IpCheckResult = std::variant<IpCheckInfo, IpCheckFailure>;

[[nodiscard]] IpCheckResult ParseIpCheckResponse(
	int httpStatus,
	const QByteArray &body);
[[nodiscard]] bool IsRiskyIp(const IpCheckInfo &info);
[[nodiscard]] QString DetectionLabel(IpDetection detection);
[[nodiscard]] QString NetworkTypeLabel(const QString &raw);
[[nodiscard]] QString LocationLabel(const IpCheckInfo &info);

[[nodiscard]] bool PeriodicIpCheckEnabled();
void SetPeriodicIpCheckEnabled(bool enabled);
[[nodiscard]] bool DebugShowIpPanelEnabled();
void SetDebugShowIpPanelEnabled(bool enabled);

class IpCheckClient final : public QObject {
public:
	explicit IpCheckClient(QObject *parent = nullptr);
	~IpCheckClient();

	void check(Fn<void()> analyzing, Fn<void(IpCheckResult)> done);
	void cancel();

private:
	void requestOwnIp();
	void requestQuality(const QString &ip);
	void finishWith(IpCheckResult result);

	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;
	Fn<void()> _analyzing;
	Fn<void(IpCheckResult)> _done;

};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
