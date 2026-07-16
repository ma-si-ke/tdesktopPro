/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_ip_check.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"

class QPainter;

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Intro::Employee {

class IpCheckPanel final : public Ui::RpWidget {
public:
	explicit IpCheckPanel(QWidget *parent);
	~IpCheckPanel();

	void setContentTop(int top);
	void setRetryCallback(Fn<void()> callback);
	void setContinueCallback(Fn<void()> callback);

	void showChecking();
	void showAnalyzing();
	void showPassed(Fn<void()> finished);
	void showBlocked(const IpCheckInfo &info);
	void hideAnimated(Fn<void()> finished);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	enum class State {
		Hidden,
		Checking,
		Passed,
		Blocked,
	};

	struct BlockedLayout {
		int left = 0;
		int gaugeTop = 0;
		int confidenceTop = 0;
		int titleTop = 0;
		int infoTop = 0;
		int gridTop = 0;
		int hintTop = 0;
		int buttonsTop = 0;
	};

	void switchState(State state);
	void animationFrame();
	void refreshButtonsVisibility();
	void updateButtonsGeometry();
	[[nodiscard]] BlockedLayout countBlockedLayout() const;

	void paintChecking(QPainter &p);
	void paintPassed(QPainter &p);
	void paintBlocked(QPainter &p, float64 contentShown);
	void paintCenterBadge(QPainter &p, float64 toPassed);

	State _state = State::Hidden;
	bool _shown = false;
	IpCheckInfo _info;
	QString _phaseText;
	int _contentTop = 0;

	Ui::Animations::Simple _shownAnimation;
	Ui::Animations::Simple _contentAnimation;
	Ui::Animations::Simple _tickAnimation;
	Ui::Animations::Simple _gaugeAnimation;
	Ui::Animations::Basic _radarAnimation;

	object_ptr<Ui::RoundButton> _retry;
	object_ptr<Ui::RoundButton> _continue;
	Fn<void()> _hiddenCallback;

};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
