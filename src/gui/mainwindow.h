#pragma once

#include <QWidget>

#include <functional>

#include "screens.h"
#include "switch.h"

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace csgui {

// The app's one window (Phase A4, architecture D23): an optional banner with the result of the last switch,
// then one of four pages (loading, blocked, Screen A "backup needed", Screen B "switch"), and the readings
// under a Details toggle. A thin view: what to show comes from cs::describeResult (banner) and
// cs::buildScreenModel (page); the caller does the loading and the backup.
class MainWindow : public QWidget
{
public:
    enum class Page { Loading, Blocked, NeedBackup, Switch };

    // `pending` is the result of the last switch (present = false: no banner).
    explicit MainWindow(const cs::ResultView &pending, QWidget *parent = nullptr);

    void showLoading();
    void showModel(const cs::ScreenModel &model, const QString &readingsText);
    void setBackupStatus(const QString &text);   // Screen A: the outcome of the Backup button

    Page page() const;

    // Banner
    bool bannerShown() const;
    QString bannerTitle() const;
    QString bannerText() const;
    QPushButton *dismissButton() const { return m_dismiss; }

    // Pages
    QString blockedText() const;
    QString needBackupText() const;
    QString backupStatus() const;
    QPushButton *backupButton() const { return m_backup; }
    QString infoText() const;
    QString directionText() const;
    QComboBox *backupCombo() const { return m_combo; }
    QCheckBox *maskCheck() const { return m_mask; }
    QString maskNote() const;
    QString noteText() const;
    QPushButton *switchButton() const { return m_switch; }
    QString detailsText() const;

    // Called when the Backup button is pressed (Screen A).
    std::function<void()> onBackup;
    // Called once when the pending result has been shown: the banner was dismissed or the window closed.
    std::function<void()> onSeen;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void markSeen();

    QFrame *m_banner = nullptr;
    QLabel *m_bannerTitle = nullptr;
    QLabel *m_bannerText = nullptr;
    QPushButton *m_dismiss = nullptr;

    QStackedWidget *m_stack = nullptr;
    QLabel *m_blocked = nullptr;
    QLabel *m_needBackup = nullptr;
    QPushButton *m_backup = nullptr;
    QLabel *m_backupStatus = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_direction = nullptr;
    QComboBox *m_combo = nullptr;
    QCheckBox *m_mask = nullptr;
    QLabel *m_maskNote = nullptr;
    QLabel *m_note = nullptr;
    QPushButton *m_switch = nullptr;

    QToolButton *m_detailsToggle = nullptr;
    QPlainTextEdit *m_details = nullptr;

    bool m_hasPending = false;
    bool m_seenCalled = false;
};

} // namespace csgui
