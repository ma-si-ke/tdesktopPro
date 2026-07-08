/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/quick_copy_ids_box.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "boxes/peer_list_box.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "ui/text/format_values.h"
#include "ui/painter.h"
#include "base/unixtime.h"
#include "styles/style_dialogs.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace QuickCopy {
namespace {

class UserRow final : public PeerListRow {
public:
	UserRow(not_null<PeerData*> peer, const QString &dateText);

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

private:
	QString _dateText;
	int _dateWidth = 0;

};

UserRow::UserRow(not_null<PeerData*> peer, const QString &dateText)
: PeerListRow(peer)
, _dateText(dateText)
, _dateWidth(dateText.isEmpty() ? 0 : st::dialogsDateFont->width(dateText)) {
}

QSize UserRow::rightActionSize() const {
	return _dateWidth
		? QSize(_dateWidth, st::peerListBox.item.height)
		: QSize();
}

QMargins UserRow::rightActionMargins() const {
	return QMargins(0, 0, st::boxRowPadding.right(), 0);
}

void UserRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (!_dateWidth) {
		return;
	}
	p.setFont(st::dialogsDateFont);
	p.setPen(st::dialogsDateFg);
	p.drawText(
		QRect(x, y, _dateWidth, st::peerListBox.item.height),
		Qt::AlignRight | Qt::AlignVCenter,
		_dateText);
}

class Controller final : public PeerListController {
public:
	explicit Controller(not_null<Main::Session*> session);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	const not_null<Main::Session*> _session;

};

Controller::Controller(not_null<Main::Session*> session)
: _session(session) {
}

Main::Session &Controller::session() const {
	return *_session;
}

void Controller::prepare() {
	setDescriptionText(u"选择用户后点击复制，将其数字ID逐行复制到剪贴板"_q);
	for (const auto &row : _session->data().chatsList()->indexed()->all()) {
		const auto history = row->history();
		if (!history || !history->peer->isUser()) {
			continue;
		}
		const auto timeId = history->chatListTimeId();
		const auto dateText = timeId
			? Ui::FormatDialogsDate(base::unixtime::parse(timeId))
			: QString();
		delegate()->peerListAppendRow(
			std::make_unique<UserRow>(history->peer, dateText));
	}
	delegate()->peerListRefreshRows();
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

} // namespace

void ShowUserIdsBox(not_null<Window::SessionController*> window) {
	auto controller = std::make_unique<Controller>(&window->session());
	window->show(Box<PeerListBox>(std::move(controller), [=](
			not_null<PeerListBox*> box) {
		box->setTitle(rpl::single(u"复制用户ID"_q));
		box->addButton(rpl::single(u"复制"_q), [=] {
			const auto rows = box->collectSelectedRows();
			if (rows.empty()) {
				box->showToast(u"请先选择用户"_q);
				return;
			}
			auto ids = QStringList();
			ids.reserve(rows.size());
			for (const auto &peer : rows) {
				ids.append(QString::number(peerToUser(peer->id).bare));
			}
			QGuiApplication::clipboard()->setText(ids.join(QChar('\n')));
			box->showToast(u"已复制 %1 个用户ID"_q.arg(rows.size()));
			box->closeBox();
		});
		box->addLeftButton(rpl::single(u"取消"_q), [=] {
			box->closeBox();
		});
	}));
}

} // namespace QuickCopy

#endif // TDESKTOP_EMPLOYEE_MODE
