#include "TrayController.h"
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>

TrayController::TrayController(MainWindow *window, QObject *parent)
    : QObject(parent), m_window(window)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_menu = new QMenu(m_window);

    QAction *openAct = m_menu->addAction(QStringLiteral("Открыть"));
    connect(openAct, &QAction::triggered, m_window, &MainWindow::showFromTray);

    QAction *refreshAct = m_menu->addAction(QStringLiteral("Обновить сейчас"));
    connect(refreshAct, &QAction::triggered, m_window, &MainWindow::refresh);

    QAction *settingsAct = m_menu->addAction(QStringLiteral("Настройки…"));
    connect(settingsAct, &QAction::triggered, m_window, &MainWindow::openSettings);

    QAction *deckAct = m_menu->addAction(QStringLiteral("Карточки Deck"));
    connect(deckAct, &QAction::triggered, m_window, &MainWindow::showDeck);

    QAction *aboutAct = m_menu->addAction(QStringLiteral("О программе"));
    connect(aboutAct, &QAction::triggered, m_window, &MainWindow::showAbout);

    m_menu->addSeparator();
    QAction *quitAct = m_menu->addAction(QStringLiteral("Закрыть"));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/nc-notify.png")), this);
    m_tray->setToolTip(QStringLiteral("Nextcloud Notify"));
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated,
            this, &TrayController::onActivated);
    m_tray->show();
}

void TrayController::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
        if (m_window->isVisible() && !m_window->isMinimized())
            m_window->hide();
        else
            m_window->showFromTray();
        break;
    default:
        break;
    }
}
