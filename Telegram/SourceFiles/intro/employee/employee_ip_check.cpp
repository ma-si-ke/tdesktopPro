/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_ip_check.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"
#include "base/flat_map.h"
#include "lang/lang_keys.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Intro::Employee {
namespace {

constexpr auto kOwnIpTimeoutMs = 8 * 1000;
constexpr auto kQualityTimeoutMs = 10 * 1000;
constexpr auto kRiskThreshold = 65;

const auto kOwnIpUrl = u"https://api.ip.sb/ip"_q;
const auto kQualityUrlBase = u"https://proxycheck.io/v3/"_q;
const auto kQualityApiKey =
	u"4794484dd663feaa0055d06898be96c4b5cf0471c0639382b4d37eab4acbdab2"_q;

[[nodiscard]] const base::flat_map<QString, QString> &CountryNames() {
	static const auto result = base::flat_map<QString, QString>{
		{ u"CN"_q, u"中国"_q },
		{ u"HK"_q, u"中国香港"_q },
		{ u"TW"_q, u"中国台湾"_q },
		{ u"MO"_q, u"中国澳门"_q },
		{ u"US"_q, u"美国"_q },
		{ u"JP"_q, u"日本"_q },
		{ u"KR"_q, u"韩国"_q },
		{ u"SG"_q, u"新加坡"_q },
		{ u"MY"_q, u"马来西亚"_q },
		{ u"TH"_q, u"泰国"_q },
		{ u"VN"_q, u"越南"_q },
		{ u"PH"_q, u"菲律宾"_q },
		{ u"ID"_q, u"印度尼西亚"_q },
		{ u"KH"_q, u"柬埔寨"_q },
		{ u"MM"_q, u"缅甸"_q },
		{ u"LA"_q, u"老挝"_q },
		{ u"IN"_q, u"印度"_q },
		{ u"BD"_q, u"孟加拉国"_q },
		{ u"PK"_q, u"巴基斯坦"_q },
		{ u"NP"_q, u"尼泊尔"_q },
		{ u"LK"_q, u"斯里兰卡"_q },
		{ u"AE"_q, u"阿联酋"_q },
		{ u"SA"_q, u"沙特阿拉伯"_q },
		{ u"TR"_q, u"土耳其"_q },
		{ u"IL"_q, u"以色列"_q },
		{ u"KZ"_q, u"哈萨克斯坦"_q },
		{ u"RU"_q, u"俄罗斯"_q },
		{ u"UA"_q, u"乌克兰"_q },
		{ u"GB"_q, u"英国"_q },
		{ u"DE"_q, u"德国"_q },
		{ u"FR"_q, u"法国"_q },
		{ u"NL"_q, u"荷兰"_q },
		{ u"IT"_q, u"意大利"_q },
		{ u"ES"_q, u"西班牙"_q },
		{ u"PT"_q, u"葡萄牙"_q },
		{ u"SE"_q, u"瑞典"_q },
		{ u"NO"_q, u"挪威"_q },
		{ u"FI"_q, u"芬兰"_q },
		{ u"DK"_q, u"丹麦"_q },
		{ u"PL"_q, u"波兰"_q },
		{ u"CZ"_q, u"捷克"_q },
		{ u"RO"_q, u"罗马尼亚"_q },
		{ u"CH"_q, u"瑞士"_q },
		{ u"AT"_q, u"奥地利"_q },
		{ u"BE"_q, u"比利时"_q },
		{ u"IE"_q, u"爱尔兰"_q },
		{ u"CA"_q, u"加拿大"_q },
		{ u"MX"_q, u"墨西哥"_q },
		{ u"BR"_q, u"巴西"_q },
		{ u"AR"_q, u"阿根廷"_q },
		{ u"AU"_q, u"澳大利亚"_q },
		{ u"NZ"_q, u"新西兰"_q },
		{ u"ZA"_q, u"南非"_q },
		{ u"EG"_q, u"埃及"_q },
		{ u"NG"_q, u"尼日利亚"_q },
	};
	return result;
}

[[nodiscard]] QString CountryDisplayName(
		const QString &code,
		const QString &fallbackEnglish) {
	const auto &names = CountryNames();
	const auto i = names.find(code.toUpper());
	return (i != names.end()) ? i->second : fallbackEnglish;
}

} // namespace

IpCheckResult ParseIpCheckResponse(int httpStatus, const QByteArray &body) {
	if (httpStatus < 200 || httpStatus >= 300) {
		LOG(("Employee: ipcheck bad http status %1").arg(httpStatus));
		return IpCheckFailure{ IpCheckFailure::Kind::Network };
	}
	auto parseError = QJsonParseError{};
	const auto doc = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		LOG(("Employee: ipcheck bad json"));
		return IpCheckFailure{ IpCheckFailure::Kind::BadJson };
	}
	const auto root = doc.object();
	const auto status = root.value(u"status"_q).toString();
	if (status != u"ok"_q && status != u"warning"_q) {
		LOG(("Employee: ipcheck status=%1 message=%2"
			).arg(status
			).arg(root.value(u"message"_q).toString()));
		return IpCheckFailure{ IpCheckFailure::Kind::BadJson };
	}

	// The result object is keyed by the queried IP, so find the first
	// value that looks like an address entry instead of assuming the key.
	auto info = IpCheckInfo();
	auto entry = QJsonObject();
	for (auto it = root.begin(); it != root.end(); ++it) {
		if (it.value().isObject()) {
			const auto candidate = it.value().toObject();
			if (candidate.contains(u"detections"_q)) {
				info.ip = it.key();
				entry = candidate;
				break;
			}
		}
	}
	if (entry.isEmpty()) {
		LOG(("Employee: ipcheck response has no address entry"));
		return IpCheckFailure{ IpCheckFailure::Kind::BadJson };
	}

	const auto network = entry.value(u"network"_q).toObject();
	info.provider = network.value(u"provider"_q).toString();
	if (info.provider.isEmpty()) {
		info.provider = network.value(u"organisation"_q).toString();
	}
	info.networkType = network.value(u"type"_q).toString();

	const auto location = entry.value(u"location"_q).toObject();
	info.countryCode = location.value(u"country_code"_q).toString();
	info.countryName = location.value(u"country_name"_q).toString();
	info.regionName = location.value(u"region_name"_q).toString();
	info.cityName = location.value(u"city_name"_q).toString();

	const auto detections = entry.value(u"detections"_q).toObject();
	if (detections.isEmpty()) {
		LOG(("Employee: ipcheck response has no detections"));
		return IpCheckFailure{ IpCheckFailure::Kind::BadJson };
	}
	const auto flag = [&](const QString &key) {
		return detections.value(key).toBool();
	};
	info.detections[int(IpDetection::Proxy)] = flag(u"proxy"_q);
	info.detections[int(IpDetection::Vpn)] = flag(u"vpn"_q);
	info.detections[int(IpDetection::Compromised)] = flag(u"compromised"_q);
	info.detections[int(IpDetection::Scraper)] = flag(u"scraper"_q);
	info.detections[int(IpDetection::Tor)] = flag(u"tor"_q);
	info.detections[int(IpDetection::Hosting)] = flag(u"hosting"_q);
	info.detections[int(IpDetection::Anonymous)] = flag(u"anonymous"_q);
	info.risk = detections.value(u"risk"_q).toInt();
	info.confidence = detections.value(u"confidence"_q).toInt();

	LOG(("Employee: ipcheck parsed ip=%1 type=%2 risk=%3 confidence=%4"
		).arg(info.ip
		).arg(info.networkType
		).arg(info.risk
		).arg(info.confidence));
	return info;
}

bool IsRiskyIp(const IpCheckInfo &info) {
	for (const auto detected : info.detections) {
		if (detected) {
			return true;
		}
	}
	return (info.risk > kRiskThreshold);
}

QString DetectionLabel(IpDetection detection) {
	switch (detection) {
	case IpDetection::Proxy:
		return tr::lng_employee_ipcheck_proxy(tr::now);
	case IpDetection::Vpn:
		return tr::lng_employee_ipcheck_vpn(tr::now);
	case IpDetection::Compromised:
		return tr::lng_employee_ipcheck_compromised(tr::now);
	case IpDetection::Scraper:
		return tr::lng_employee_ipcheck_scraper(tr::now);
	case IpDetection::Tor:
		return tr::lng_employee_ipcheck_tor(tr::now);
	case IpDetection::Hosting:
		return tr::lng_employee_ipcheck_hosting(tr::now);
	case IpDetection::Anonymous:
		return tr::lng_employee_ipcheck_anonymous(tr::now);
	}
	Unexpected("Detection in Intro::Employee::DetectionLabel.");
}

QString NetworkTypeLabel(const QString &raw) {
	const auto lower = raw.toLower();
	if (lower == u"hosting"_q) {
		return tr::lng_employee_ipcheck_type_hosting(tr::now);
	} else if (lower == u"business"_q) {
		return tr::lng_employee_ipcheck_type_business(tr::now);
	} else if (lower == u"residential"_q) {
		return tr::lng_employee_ipcheck_type_residential(tr::now);
	} else if (lower == u"wireless"_q) {
		return tr::lng_employee_ipcheck_type_wireless(tr::now);
	} else if (lower == u"education"_q) {
		return tr::lng_employee_ipcheck_type_education(tr::now);
	} else if (lower == u"government"_q) {
		return tr::lng_employee_ipcheck_type_government(tr::now);
	} else if (lower == u"commercial"_q) {
		return tr::lng_employee_ipcheck_type_commercial(tr::now);
	}
	return raw.isEmpty() ? tr::lng_employee_ipcheck_type_unknown(tr::now) : raw;
}

QString LocationLabel(const IpCheckInfo &info) {
	auto parts = QStringList();
	const auto country = CountryDisplayName(
		info.countryCode,
		info.countryName);
	if (!country.isEmpty()) {
		parts.push_back(country);
	}
	if (!info.regionName.isEmpty()) {
		parts.push_back(info.regionName);
	}
	if (!info.cityName.isEmpty() && info.cityName != info.regionName) {
		parts.push_back(info.cityName);
	}
	return parts.isEmpty()
		? tr::lng_employee_ipcheck_type_unknown(tr::now)
		: parts.join(u" · "_q);
}

IpCheckClient::IpCheckClient(QObject *parent)
: QObject(parent)
, _nam(std::make_unique<QNetworkAccessManager>()) {
}

IpCheckClient::~IpCheckClient() {
	cancel();
}

void IpCheckClient::check(
		Fn<void()> analyzing,
		Fn<void(IpCheckResult)> done) {
	cancel();
	_analyzing = std::move(analyzing);
	_done = std::move(done);
	requestOwnIp();
}

void IpCheckClient::requestOwnIp() {
	auto request = QNetworkRequest(QUrl(kOwnIpUrl));
	request.setTransferTimeout(kOwnIpTimeoutMs);

	_reply = _nam->get(request);
	const auto reply = _reply.data();
	QObject::connect(reply, &QNetworkReply::finished, this, [=] {
		if (!reply || reply != _reply.data()) {
			return;
		}
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		const auto error = int(reply->error());
		if (error != QNetworkReply::NoError || status != 200) {
			LOG(("Employee: ipcheck own-ip failed error=%1 status=%2"
				).arg(error
				).arg(status));
			reply->deleteLater();
			_reply.clear();
			finishWith(IpCheckFailure{ IpCheckFailure::Kind::Network });
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();

		const auto ip = QString::fromUtf8(body).trimmed();
		if (QHostAddress(ip).isNull()) {
			LOG(("Employee: ipcheck own-ip response invalid"));
			finishWith(IpCheckFailure{ IpCheckFailure::Kind::BadJson });
			return;
		}
		LOG(("Employee: ipcheck own ip resolved"));
		if (const auto callback = _analyzing) {
			callback();
		}
		requestQuality(ip);
	});
}

void IpCheckClient::requestQuality(const QString &ip) {
	auto url = QUrl(kQualityUrlBase + ip);
	auto query = QUrlQuery();
	query.addQueryItem(u"key"_q, kQualityApiKey);
	url.setQuery(query);

	auto request = QNetworkRequest(url);
	request.setTransferTimeout(kQualityTimeoutMs);

	_reply = _nam->get(request);
	const auto reply = _reply.data();
	QObject::connect(reply, &QNetworkReply::finished, this, [=] {
		if (!reply || reply != _reply.data()) {
			return;
		}
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		const auto error = int(reply->error());
		if (error != QNetworkReply::NoError && status == 0) {
			LOG(("Employee: ipcheck quality network error=%1").arg(error));
			reply->deleteLater();
			_reply.clear();
			finishWith(IpCheckFailure{ IpCheckFailure::Kind::Network });
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		finishWith(ParseIpCheckResponse(status, body));
	});
}

void IpCheckClient::finishWith(IpCheckResult result) {
	_analyzing = nullptr;
	if (const auto callback = base::take(_done)) {
		callback(std::move(result));
	}
}

void IpCheckClient::cancel() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
	_analyzing = nullptr;
	_done = nullptr;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
