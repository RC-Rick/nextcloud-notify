#pragma once

#include <QDialog>
#include <QStringList>

#include "Event.h"

class QLineEdit;
class QListWidget;
class QSpinBox;
class QPushButton;
class QLabel;
class QComboBox;
class QCheckBox;
class CalDavClient;
class DeckClient;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const QString &currentPassword, QWidget *parent = nullptr);
    QString password() const;

signals:
    void testNotification();

private slots:
    void discover();
    void onCalendars(const QList<CalendarInfo> &cals);
    void onError(const QString &ctx, const QString &msg);
    void accept() override;

private:
    void updateAuthUi();

    QComboBox *m_authType = nullptr;
    QLineEdit *m_url = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_pass = nullptr;
    QListWidget *m_cals = nullptr;
    QSpinBox *m_interval = nullptr;
    QSpinBox *m_horizon = nullptr;
    QPushButton *m_discoverBtn = nullptr;
    QLineEdit *m_caFile = nullptr;
    QPushButton *m_caBrowse = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_keyringHint = nullptr;
    CalDavClient *m_client = nullptr;
    QString m_currentPassword;
    QStringList m_selectedUrls;

    // вкладка «Уведомления»
    QCheckBox *m_notifyEnabled = nullptr;
    QComboBox *m_notifyMode = nullptr;
    QCheckBox *m_notifySound = nullptr;
    QLineEdit *m_thresholds = nullptr;
    QPushButton *m_testBtn = nullptr;
    QCheckBox *m_autostart = nullptr;
    QCheckBox *m_startMin = nullptr;
    QCheckBox *m_deckEnabled = nullptr;
    QCheckBox *m_deckNoDue = nullptr;
    QPushButton *m_deckFind = nullptr;
    QListWidget *m_deckBoards = nullptr;
    DeckClient *m_deckClient = nullptr;
};
