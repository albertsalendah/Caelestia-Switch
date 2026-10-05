#include "resultwindow.h"

#include <QCloseEvent>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace csgui {

ResultWindow::ResultWindow(const cs::ResultView &view, QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Caelestia Switch"));

    m_title = new QLabel(view.present ? view.title : QStringLiteral("Caelestia Switch"));
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.4);
    m_title->setFont(titleFont);

    m_text = new QLabel(view.present ? view.text : QStringLiteral("No switch result is waiting. This is the current state:"));
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_status = new QPlainTextEdit;
    m_status->setReadOnly(true);
    m_status->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_status->setLineWrapMode(QPlainTextEdit::NoWrap);

    m_close = new QPushButton(QStringLiteral("Close"));
    m_close->setDefault(true);
    connect(m_close, &QPushButton::clicked, this, &QWidget::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_title);
    layout->addWidget(m_text);
    if (view.present) {
        layout->addWidget(new QLabel(QStringLiteral("Current state:")));
    }
    layout->addWidget(m_status, 1);
    layout->addWidget(m_close, 0, Qt::AlignRight);

    resize(620, 420);
}

void ResultWindow::setStatusText(const QString &text)
{
    m_status->setPlainText(text);
}

QString ResultWindow::shownTitle() const
{
    return m_title->text();
}

QString ResultWindow::shownText() const
{
    return m_text->text();
}

QString ResultWindow::shownStatus() const
{
    return m_status->toPlainText();
}

void ResultWindow::closeEvent(QCloseEvent *event)
{
    if (!m_closed) {
        m_closed = true;
        if (onClose) {
            onClose();
        }
    }
    event->accept();
}

} // namespace csgui
