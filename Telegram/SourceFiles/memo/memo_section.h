/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_list_widget.h"
#include "window/section_memento.h"
#include "window/section_widget.h"

namespace Ui {
struct PreparedList;
class PopupMenu;
class ElasticScroll;
class PlainShadow;
class SubTabs;
class ChatTheme;
} // namespace Ui

namespace HistoryView {
class ComposeControls;
} // namespace HistoryView

namespace Memo {

class MemoMemento final : public Window::SectionMemento {
public:
	MemoMemento() = default;

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

};

class MemoSection final
	: public Window::SectionWidget
	, private HistoryView::WindowListDelegate {
public:
	MemoSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~MemoSection();

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool forceAnimateBack() const override {
		return true;
	}
	bool hasTopBarShadow() const override {
		return false;
	}

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	// ListDelegate interface.
	HistoryView::Context listContext() override;
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override;
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(HistoryView::SelectedItems &&items) override;
	void listMarkReadTill(not_null<HistoryItem*> item) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	HistoryView::MessagesBarData listMessagesBar(
		const std::vector<not_null<HistoryView::Element*>> &elements,
		bool markLastAsRead) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<HistoryView::Element*> view) override;
	bool listElementHideReply(
		not_null<const HistoryView::Element*> view) override;
	bool listElementShownUnread(
		not_null<const HistoryView::Element*> view) override;
	bool listIsGoodForAroundPosition(
		not_null<const HistoryView::Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(
		const QString &query,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<Ui::ChatTheme*> listChatTheme() override;
	HistoryView::CopyRestrictionType listCopyRestrictionType(
		HistoryItem *item) override;
	HistoryView::CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) override;
	HistoryView::CopyRestrictionType listSelectRestrictionType() override;
	auto listAllowedReactionsValue()
		-> rpl::producer<Data::AllowedReactions> override;
	void listShowPremiumToast(not_null<DocumentData*> document) override;
	void listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) override;
	void listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) override;
	void listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) override;
	QString listElementAuthorRank(
		not_null<const HistoryView::Element*> view) override;
	bool listElementHideTopicButton(
		not_null<const HistoryView::Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<HistoryView::TranslateTracker*> tracker) override;
	bool listDoubleClicked(not_null<HistoryItem*> item) override;
	bool listFillContextMenu(
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item) override;

protected:
	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;
	void checkActivation() override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void setupTabs();
	void setupComposeControls();
	void refreshTabs();
	void updateControlsGeometry();
	void updateInnerVisibleArea();
	void showAtEnd();
	void chooseAttach(std::optional<bool> overrideCompress);
	bool confirmSendingFiles(not_null<const QMimeData*> data) override;
	void confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel = QString());
	void send();
	void edit(not_null<HistoryItem*> item);
	void promptNewFolder();
	void showFolderMenu(uint64 folderId);

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<History*> _history;

	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::SubTabs> _tabs;
	object_ptr<Ui::PlainShadow> _tabsShadow;
	object_ptr<Ui::ElasticScroll> _scroll;
	QPointer<HistoryView::ListWidget> _inner;
	std::unique_ptr<HistoryView::ComposeControls> _composeControls;

	rpl::variable<uint64> _folderId = 0;
	bool _choosingAttach = false;

};

} // namespace Memo
