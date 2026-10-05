#pragma once

#include <QWidget>

#include <functional>

#include "switch.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace csgui {

// The "switch finished" window (Phase A4, first batch): the result of the last switch (or a neutral heading
// when none is waiting), the current readings underneath, and a Close button. A thin view: the wording comes
// from cs::describeResult, the readings from the caller.
class ResultWindow : public QWidget
{
public:
    explicit ResultWindow(const cs::ResultView &view, QWidget *parent = nullptr);

    void setStatusText(const QString &text);

    QString shownTitle() const;
    QString shownText() const;
    QString shownStatus() const;
    QPushButton *closeButton() const { return m_close; }

    // Called once, when the window is closed (by the button or the window manager).
    std::function<void()> onClose;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QLabel *m_title = nullptr;
    QLabel *m_text = nullptr;
    QPlainTextEdit *m_status = nullptr;
    QPushButton *m_close = nullptr;
    bool m_closed = false;
};

} // namespace csgui
