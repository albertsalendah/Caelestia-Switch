#include "mainwindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace csgui {

namespace {

QLabel *wrapped(const QString &text = QString())
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QString plasmashellLine(const cs::Readings &r)
{
    if (r.plasmashellUnit.masked()) {
        return r.plasmashellRunning ? QStringLiteral("masked, but still running") : QStringLiteral("masked and stopped");
    }
    return r.plasmashellRunning ? QStringLiteral("running") : QStringLiteral("stopped");
}

QString backupLabel(const cs::BackupInfo &b)
{
    return QStringLiteral("%1  (%2; Caelestia %3, Plasma %4)")
        .arg(b.created.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")),
             b.trigger.isEmpty() ? QStringLiteral("backup") : b.trigger,
             b.caelestiaVersion.isEmpty() ? QStringLiteral("?") : b.caelestiaVersion,
             b.plasmaVersion.isEmpty() ? QStringLiteral("?") : b.plasmaVersion);
}

} // namespace

MainWindow::MainWindow(const cs::ResultView &pending, QWidget *parent) : QWidget(parent), m_hasPending(pending.present)
{
    setWindowTitle(QStringLiteral("Caelestia Switch"));

    // Banner: the result of the last switch.
    m_banner = new QFrame;
    m_banner->setFrameShape(QFrame::StyledPanel);
    m_bannerTitle = new QLabel(pending.title);
    QFont titleFont = m_bannerTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    m_bannerTitle->setFont(titleFont);
    m_bannerText = wrapped(pending.text);
    m_dismiss = new QPushButton(QStringLiteral("Dismiss"));
    auto *bannerLayout = new QVBoxLayout(m_banner);
    bannerLayout->addWidget(m_bannerTitle);
    bannerLayout->addWidget(m_bannerText);
    bannerLayout->addWidget(m_dismiss, 0, Qt::AlignRight);
    m_banner->setVisible(pending.present);
    connect(m_dismiss, &QPushButton::clicked, this, [this]() {
        m_banner->hide();
        markSeen();
    });

    // Pages.
    m_stack = new QStackedWidget;

    auto *loading = new QWidget;   // Page::Loading
    auto *loadingLayout = new QVBoxLayout(loading);
    loadingLayout->addStretch(1);
    auto *loadingLabel = new QLabel(QStringLiteral("Reading the current state..."));
    loadingLabel->setAlignment(Qt::AlignCenter);
    auto *bar = new QProgressBar;
    bar->setRange(0, 0);
    bar->setTextVisible(false);
    loadingLayout->addWidget(loadingLabel);
    loadingLayout->addWidget(bar);
    loadingLayout->addStretch(1);
    m_stack->addWidget(loading);

    auto *blockedPage = new QWidget;   // Page::Blocked
    auto *blockedLayout = new QVBoxLayout(blockedPage);
    m_blocked = wrapped();
    blockedLayout->addWidget(m_blocked);
    blockedLayout->addStretch(1);
    m_stack->addWidget(blockedPage);

    auto *needPage = new QWidget;   // Page::NeedBackup (Screen A)
    auto *needLayout = new QVBoxLayout(needPage);
    auto *needTitle = new QLabel(QStringLiteral("A backup is needed"));
    needTitle->setFont(titleFont);
    m_needBackup = wrapped();
    m_backup = new QPushButton(QStringLiteral("Create Caelestia backup"));
    m_backupStatus = wrapped();
    needLayout->addWidget(needTitle);
    needLayout->addWidget(m_needBackup);
    needLayout->addWidget(m_backup, 0, Qt::AlignLeft);
    needLayout->addWidget(m_backupStatus);
    needLayout->addStretch(1);
    m_stack->addWidget(needPage);
    connect(m_backup, &QPushButton::clicked, this, [this]() {
        if (onBackup) {
            onBackup();
        }
    });

    auto *switchPage = new QWidget;   // Page::Switch (Screen B)
    auto *switchLayout = new QVBoxLayout(switchPage);
    m_info = wrapped();
    m_direction = new QLabel;
    m_direction->setFont(titleFont);
    auto *comboRow = new QHBoxLayout;
    comboRow->addWidget(new QLabel(QStringLiteral("Restore this backup:")));
    m_combo = new QComboBox;
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    comboRow->addWidget(m_combo, 1);
    m_mask = new QCheckBox(QStringLiteral("Also disable plasmashell"));
    m_maskNote = wrapped();
    m_note = wrapped();
    m_switch = new QPushButton(QStringLiteral("Switch"));
    m_switch->setEnabled(false);   // batch 2a: switching from this window arrives with batch 2b
    m_switch->setToolTip(QStringLiteral("Switching from this window arrives in the next update. For now use 'caelestia-switch on' or 'off' in a terminal."));
    switchLayout->addWidget(m_info);
    switchLayout->addWidget(m_direction);
    switchLayout->addLayout(comboRow);
    switchLayout->addWidget(m_mask);
    switchLayout->addWidget(m_maskNote);
    switchLayout->addWidget(m_note);
    switchLayout->addWidget(m_switch, 0, Qt::AlignRight);
    switchLayout->addStretch(1);
    m_stack->addWidget(switchPage);

    // Details: the readings, collapsed by default (the screen is small on the test laptop).
    m_detailsToggle = new QToolButton;
    m_detailsToggle->setText(QStringLiteral("Details"));
    m_detailsToggle->setCheckable(true);
    m_detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_detailsToggle->setArrowType(Qt::RightArrow);
    m_details = new QPlainTextEdit;
    m_details->setReadOnly(true);
    m_details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_details->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_details->setVisible(false);
    connect(m_detailsToggle, &QToolButton::toggled, this, [this](bool on) {
        m_details->setVisible(on);
        m_detailsToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });

    auto *close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, this, &QWidget::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_banner);
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_detailsToggle, 0, Qt::AlignLeft);
    layout->addWidget(m_details, 1);
    layout->addWidget(close, 0, Qt::AlignRight);

    resize(620, 440);
    showLoading();
}

void MainWindow::showLoading()
{
    m_stack->setCurrentIndex(static_cast<int>(Page::Loading));
}

void MainWindow::setBackupStatus(const QString &text)
{
    m_backupStatus->setText(text);
}

void MainWindow::showModel(const cs::ScreenModel &m, const QString &readingsText)
{
    m_details->setPlainText(readingsText);
    m_backupStatus->clear();

    switch (m.kind) {
    case cs::ScreenModel::Kind::Blocked:
        m_blocked->setText(m.message);
        m_stack->setCurrentIndex(static_cast<int>(Page::Blocked));
        return;
    case cs::ScreenModel::Kind::NeedBackup:
        m_needBackup->setText(m.message);
        m_backup->setVisible(m.canBackup);
        m_stack->setCurrentIndex(static_cast<int>(Page::NeedBackup));
        return;
    case cs::ScreenModel::Kind::Switch:
        break;
    }

    const cs::Readings &r = m.readings;
    const QString mode = r.provider == cs::Provider::Caelestia ? QStringLiteral("Caelestia") : QStringLiteral("stock Plasma");
    m_info->setText(QStringLiteral("Current mode: %1\nplasmashell: %2\nCaelestia: %3, %4")
                        .arg(mode, plasmashellLine(r), r.install.source, r.install.version));
    m_direction->setText(QStringLiteral("Switch to %1").arg(cs::targetLabel(m.direction)));

    m_combo->clear();
    for (const cs::BackupInfo &b : m.backups) {
        m_combo->addItem(backupLabel(b), b.ref());   // the newest is first and therefore preselected
    }
    m_combo->setEnabled(!m.backups.isEmpty());

    m_mask->setVisible(m.maskShown);
    m_mask->setChecked(false);
    m_mask->setEnabled(m.maskAllowed);
    QString maskNote;
    if (m.maskShown && !m.maskAllowed) {
        maskNote = QStringLiteral("plasmashell cannot be disabled: something depends on it (%1).").arg(m.maskBlockers.join(QStringLiteral(", ")));
    } else if (m.maskShown && !m.maskWeak.isEmpty()) {
        maskNote = QStringLiteral("Disabling plasmashell is allowed. It is only weakly wanted by %1; that does not stop a masked plasmashell.")
                       .arg(m.maskWeak.join(QStringLiteral(", ")));
    }
    m_maskNote->setText(maskNote);
    m_maskNote->setVisible(!maskNote.isEmpty());

    m_note->setText(m.message);
    m_note->setVisible(!m.message.isEmpty());
    m_stack->setCurrentIndex(static_cast<int>(Page::Switch));
}

MainWindow::Page MainWindow::page() const
{
    return static_cast<Page>(m_stack->currentIndex());
}

bool MainWindow::bannerShown() const { return !m_banner->isHidden(); }
QString MainWindow::bannerTitle() const { return m_bannerTitle->text(); }
QString MainWindow::bannerText() const { return m_bannerText->text(); }
QString MainWindow::blockedText() const { return m_blocked->text(); }
QString MainWindow::needBackupText() const { return m_needBackup->text(); }
QString MainWindow::backupStatus() const { return m_backupStatus->text(); }
QString MainWindow::infoText() const { return m_info->text(); }
QString MainWindow::directionText() const { return m_direction->text(); }
QString MainWindow::maskNote() const { return m_maskNote->text(); }
QString MainWindow::noteText() const { return m_note->text(); }
QString MainWindow::detailsText() const { return m_details->toPlainText(); }

void MainWindow::markSeen()
{
    if (m_hasPending && !m_seenCalled) {
        m_seenCalled = true;
        if (onSeen) {
            onSeen();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    markSeen();   // the result was on screen; a failed or unfinished switch keeps showing until repaired
    event->accept();
}

} // namespace csgui
