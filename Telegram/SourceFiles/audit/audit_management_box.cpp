/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "audit/audit_management_box.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "audit/audit_archive.h"
#include "audit/audit_reporter.h"
#include "boxes/peer_list_box.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "base/flat_set.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>

#include <algorithm>
#include <array>

namespace Audit {
namespace {

void ShowAuditHours(
	not_null<Window::SessionController*> window,
	not_null<ArchiveStore*> store,
	qint32 day);

[[nodiscard]] QString FormatDay(qint32 julian) {
	const auto date = QDate::fromJulianDay(julian);
	static const auto kWeekdays = std::array<QString, 7>{
		u"周一"_q, u"周二"_q, u"周三"_q, u"周四"_q,
		u"周五"_q, u"周六"_q, u"周日"_q,
	};
	const auto index = std::clamp(date.dayOfWeek(), 1, 7) - 1;
	return date.toString(u"yyyy-MM-dd"_q) + u" "_q + kWeekdays[index];
}

[[nodiscard]] QString FormatCountStatus(int count, qint64 chars, int uploaded) {
	auto result = u"%1 条 · %2 字"_q.arg(count).arg(chars);
	if (uploaded > 0) {
		result += u" · 已上传 %1"_q.arg(uploaded);
	}
	return result;
}

// A non-peer list row rendering a colored circle with a short numeric badge
// (day-of-month or hour), a title and a status line, reused for both levels.
class AuditBucketRow final : public PeerListRow {
public:
	AuditBucketRow(
		PeerListRowId id,
		uint64 colorSeed,
		QString badge,
		QString name,
		const QString &status);

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

private:
	Ui::EmptyUserpic _userpic;
	QString _badge;
	QString _name;

};

AuditBucketRow::AuditBucketRow(
	PeerListRowId id,
	uint64 colorSeed,
	QString badge,
	QString name,
	const QString &status)
: PeerListRow(id)
, _userpic(
	Ui::EmptyUserpic::UserpicColor(Ui::EmptyUserpic::ColorIndex(colorSeed)),
	QString())
, _badge(std::move(badge))
, _name(std::move(name)) {
	setCustomStatus(status);
}

QString AuditBucketRow::generateName() {
	return _name;
}

QString AuditBucketRow::generateShortName() {
	return _name;
}

PaintRoundImageCallback AuditBucketRow::generatePaintUserpicCallback(
		bool forceRound) {
	return [this](Painter &p, int x, int y, int outerWidth, int size) {
		_userpic.paintCircle(p, x, y, outerWidth, size);
		p.setPen(st::historyPeerUserpicFg);
		p.setFont(st::semiboldFont);
		p.drawText(QRect(x, y, size, size), Qt::AlignCenter, _badge);
	};
}

class AuditDaysController final : public PeerListController {
public:
	AuditDaysController(
		not_null<Window::SessionController*> window,
		not_null<ArchiveStore*> store);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	void refresh();

	const not_null<Window::SessionController*> _window;
	const not_null<ArchiveStore*> _store;

};

AuditDaysController::AuditDaysController(
	not_null<Window::SessionController*> window,
	not_null<ArchiveStore*> store)
: _window(window)
, _store(store) {
}

Main::Session &AuditDaysController::session() const {
	return _window->session();
}

void AuditDaysController::prepare() {
	setDescriptionText(u"按天查看本地留存的审计记录，点击某天查看各时段、上传或删除"_q);
	_store->changes() | rpl::on_next([this] {
		refresh();
	}, lifetime());
	refresh();
}

void AuditDaysController::refresh() {
	while (delegate()->peerListFullRowsCount() > 0) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
	}
	for (const auto &day : _store->days()) {
		const auto date = QDate::fromJulianDay(day.day);
		delegate()->peerListAppendRow(std::make_unique<AuditBucketRow>(
			PeerListRowId(day.day),
			uint64(day.day),
			QString::number(date.day()),
			FormatDay(day.day),
			FormatCountStatus(day.count, day.chars, day.uploaded)));
	}
	delegate()->peerListRefreshRows();
}

void AuditDaysController::rowClicked(not_null<PeerListRow*> row) {
	ShowAuditHours(_window, _store, qint32(row->id()));
}

class AuditHoursController final : public PeerListController {
public:
	AuditHoursController(
		not_null<Window::SessionController*> window,
		not_null<ArchiveStore*> store,
		qint32 day);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	void uploadSelected(not_null<PeerListBox*> box);
	void deleteSelected(not_null<PeerListBox*> box);

private:
	[[nodiscard]] base::flat_set<int> selectedHours(
		not_null<PeerListBox*> box) const;

	const not_null<Window::SessionController*> _window;
	const not_null<ArchiveStore*> _store;
	const qint32 _day = 0;

};

AuditHoursController::AuditHoursController(
	not_null<Window::SessionController*> window,
	not_null<ArchiveStore*> store,
	qint32 day)
: _window(window)
, _store(store)
, _day(day) {
}

Main::Session &AuditHoursController::session() const {
	return _window->session();
}

void AuditHoursController::prepare() {
	setDescriptionText(u"勾选时段后，可上传所选，或删除所选以释放本地空间"_q);
	for (const auto &hour : _store->dayHours(_day)) {
		delegate()->peerListAppendRow(std::make_unique<AuditBucketRow>(
			PeerListRowId(hour.hour),
			uint64(hour.hour + 100),
			QString::number(hour.hour),
			u"%1:00 - %1:59"_q.arg(hour.hour, 2, 10, QChar('0')),
			FormatCountStatus(hour.count, hour.chars, hour.uploaded)));
	}
	delegate()->peerListRefreshRows();
}

void AuditHoursController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

base::flat_set<int> AuditHoursController::selectedHours(
		not_null<PeerListBox*> box) const {
	auto hours = base::flat_set<int>();
	for (const auto id : box->collectSelectedIds()) {
		hours.emplace(int(id));
	}
	return hours;
}

void AuditHoursController::uploadSelected(not_null<PeerListBox*> box) {
	if (_store->uploading()) {
		box->showToast(u"上传进行中，请稍候"_q);
		return;
	}
	const auto hours = selectedHours(box);
	if (hours.empty()) {
		box->showToast(u"请先勾选要上传的时段"_q);
		return;
	}
	auto events = _store->eventsForHours(_day, hours);
	if (events.empty()) {
		box->showToast(u"所选时段没有记录"_q);
		return;
	}
	const auto count = int(events.size());
	const auto weak = QPointer<PeerListBox>(box.get());
	box->showToast(u"正在上传 %1 条…"_q.arg(count));
	_store->upload(std::move(events), [=](ArchiveUploadResult result) {
		const auto strong = weak.data();
		if (!strong) {
			return;
		}
		if (result.ok) {
			strong->showToast(u"上传成功：%1 条"_q.arg(result.accepted));
			strong->closeBox();
		} else {
			strong->showToast(result.error.isEmpty()
				? u"上传失败"_q
				: result.error);
		}
	});
}

void AuditHoursController::deleteSelected(not_null<PeerListBox*> box) {
	const auto hours = selectedHours(box);
	if (hours.empty()) {
		box->showToast(u"请先勾选要删除的时段"_q);
		return;
	}
	const auto count = int(_store->eventsForHours(_day, hours).size());
	if (!count) {
		box->showToast(u"所选时段没有记录"_q);
		return;
	}
	const auto store = _store;
	const auto day = _day;
	const auto weak = QPointer<PeerListBox>(box.get());
	box->uiShow()->showBox(Ui::MakeConfirmBox({
		.text = u"确定删除所选 %1 条审计记录？删除后本地不可恢复。"_q.arg(count),
		.confirmed = [=](Fn<void()> close) {
			const auto removed = store->deleteHours(day, hours);
			close();
			if (const auto strong = weak.data()) {
				strong->showToast(u"已删除 %1 条"_q.arg(removed));
				strong->closeBox();
			}
		},
		.confirmText = u"删除"_q,
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void ShowAuditHours(
		not_null<Window::SessionController*> window,
		not_null<ArchiveStore*> store,
		qint32 day) {
	auto controller = std::make_unique<AuditHoursController>(
		window,
		store,
		day);
	const auto raw = controller.get();
	window->show(Box<PeerListBox>(std::move(controller), [=](
			not_null<PeerListBox*> box) {
		box->setTitle(rpl::single(FormatDay(day)));
		box->addButton(rpl::single(u"上传所选"_q), [=] {
			raw->uploadSelected(box);
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
		box->addLeftButton(rpl::single(u"删除所选"_q), [=] {
			raw->deleteSelected(box);
		}, st::attentionBoxButton);
	}));
}

} // namespace

void ShowAuditManagement(not_null<Window::SessionController*> window) {
	const auto reporter = window->session().audit();
	if (!reporter) {
		return;
	}
	const auto store = reporter->archive();
	if (!store) {
		return;
	}
	auto controller = std::make_unique<AuditDaysController>(window, store);
	window->show(Box<PeerListBox>(std::move(controller), [=](
			not_null<PeerListBox*> box) {
		box->setTitle(rpl::single(u"本地审计数据管理"_q));
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
