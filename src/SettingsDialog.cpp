#include "SettingsDialog.h"
#include "NotificationManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "CalDavClient.h"
#include "DeckClient.h"
#include "Autostart.h"

SettingsDialog::SettingsDialog(const QString &currentPassword, QWidget *parent)
    : QDialog(parent), m_currentPassword(currentPassword)
{
    setWindowTitle(QStringLiteral("Настройки nc-notify"));
    resize(580, 640);

    QSettings s;
    const bool hasPassword = !m_currentPassword.isEmpty();
    const QString authType = s.value(QStringLiteral("account/authType"),
                                     QStringLiteral("password")).toString();

    // ---------- вкладка «Аккаунт» ----------
    auto *accountTab = new QWidget(this);

    m_authType = new QComboBox(accountTab);
    m_authType->addItem(QStringLiteral("Логин и пароль (пароль приложения)"),
                        QStringLiteral("password"));
    m_authType->addItem(QStringLiteral("Сквозная авторизация (Kerberos/AD)"),
                        QStringLiteral("kerberos"));
    m_authType->setCurrentIndex(authType == QLatin1String("kerberos") ? 1 : 0);

    m_url = new QLineEdit(s.value(QStringLiteral("account/serverUrl")).toString(), accountTab);
    m_url->setPlaceholderText(QStringLiteral("https://cloud.example.com"));
    m_user = new QLineEdit(s.value(QStringLiteral("account/user")).toString(), accountTab);
    m_pass = new QLineEdit(accountTab);
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setPlaceholderText(hasPassword
        ? QStringLiteral("сохранён — заполняйте только для замены")
        : QStringLiteral("пароль приложения Nextcloud"));

    m_keyringHint = new QLabel(accountTab);
    m_keyringHint->setWordWrap(true);
    if (hasPassword) {
        m_keyringHint->setText(QStringLiteral(
            "Пароль сохранён в связке ключей (gnome-keyring). "
            "Повторно вводить не нужно — только если хотите заменить."));
        m_keyringHint->setStyleSheet(QStringLiteral("color:#2e7d32;"));
    } else {
        m_keyringHint->setText(QStringLiteral(
            "Пароль ещё не сохранён — введите пароль приложения Nextcloud."));
        m_keyringHint->setStyleSheet(QStringLiteral("color:#b00020;"));
    }

    m_caFile = new QLineEdit(s.value(QStringLiteral("account/caFile")).toString(), accountTab);
    m_caFile->setPlaceholderText(QStringLiteral("необязательно: /etc/ssl/certs/domain-root-ca.pem"));
    m_caBrowse = new QPushButton(QStringLiteral("Обзор…"), accountTab);
    auto *caRow = new QHBoxLayout;
    caRow->addWidget(m_caFile, 1);
    caRow->addWidget(m_caBrowse);
    connect(m_caBrowse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, QStringLiteral("Корневой сертификат (CA)"),
            QStringLiteral("/etc/ssl/certs"),
            QStringLiteral("Сертификаты (*.pem *.crt);;Все файлы (*)"));
        if (!f.isEmpty())
            m_caFile->setText(f);
    });

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Тип входа:"), m_authType);
    form->addRow(QStringLiteral("Адрес сервера:"), m_url);
    form->addRow(QStringLiteral("Логин:"), m_user);
    form->addRow(QStringLiteral("Пароль приложения:"), m_pass);
    form->addRow(QStringLiteral("CA-сертификат (Kerberos):"), caRow);

    m_discoverBtn = new QPushButton(QStringLiteral("Найти календари"), accountTab);
    m_cals = new QListWidget(accountTab);
    m_hint = new QLabel(accountTab);
    m_hint->setWordWrap(true);

    const QStringList saved = s.value(QStringLiteral("account/calendars")).toStringList();
    for (const QString &e : saved) {
        const CalendarInfo ci = decodeCalendar(e);
        m_selectedUrls << ci.url;
        auto *it = new QListWidgetItem(ci.name.isEmpty() ? ci.url : ci.name, m_cals);
        it->setData(Qt::UserRole, ci.url);
        it->setToolTip(ci.url);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }

    m_interval = new QSpinBox(accountTab);
    m_interval->setRange(1, 1440);
    m_interval->setSuffix(QStringLiteral(" мин"));
    m_interval->setValue(s.value(QStringLiteral("sync/intervalMin"), 5).toInt());
    m_horizon = new QSpinBox(accountTab);
    m_horizon->setRange(1, 90);
    m_horizon->setSuffix(QStringLiteral(" дн"));
    m_horizon->setValue(s.value(QStringLiteral("sync/horizonDays"), 14).toInt());
    auto *syncForm = new QFormLayout;
    syncForm->addRow(QStringLiteral("Интервал синхронизации:"), m_interval);
    syncForm->addRow(QStringLiteral("Показывать события на:"), m_horizon);

    auto *accountLay = new QVBoxLayout(accountTab);
    accountLay->addLayout(form);
    accountLay->addWidget(m_keyringHint);
    accountLay->addWidget(m_discoverBtn);
    accountLay->addWidget(m_cals, 1);
    accountLay->addLayout(syncForm);
    accountLay->addWidget(m_hint);

    // ---------- вкладка «Уведомления» ----------
    auto *notifyTab = new QWidget(this);

    m_notifyEnabled = new QCheckBox(QStringLiteral("Включить уведомления"), notifyTab);
    m_notifyEnabled->setChecked(s.value(QStringLiteral("notify/enabled"), true).toBool());

    m_notifyMode = new QComboBox(notifyTab);
    m_notifyMode->addItem(QStringLiteral("Окно + fly-notifications"), int(NotificationManager::Both));
    m_notifyMode->addItem(QStringLiteral("Только fly-notifications"), int(NotificationManager::FlyOnly));
    m_notifyMode->addItem(QStringLiteral("Только своё окно"), int(NotificationManager::WindowOnly));
    m_notifyMode->addItem(QStringLiteral("Выключены (тихо)"), int(NotificationManager::Off));
    const int mode = s.value(QStringLiteral("notify/mode"), int(NotificationManager::Both)).toInt();
    const int modeIdx = m_notifyMode->findData(mode);
    m_notifyMode->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);

    m_notifySound = new QCheckBox(QStringLiteral("Звуковой сигнал"), notifyTab);
    m_notifySound->setChecked(s.value(QStringLiteral("notify/sound"), true).toBool());

    m_thresholds = new QLineEdit(
        s.value(QStringLiteral("notify/thresholds"), QStringLiteral("15,5")).toString(), notifyTab);
    m_thresholds->setPlaceholderText(QStringLiteral("напр. 30, 10, 5"));

    m_testBtn = new QPushButton(QStringLiteral("Тест уведомления"), notifyTab);

    auto *notifyForm = new QFormLayout;
    notifyForm->addRow(m_notifyEnabled);
    notifyForm->addRow(QStringLiteral("Режим:"), m_notifyMode);
    notifyForm->addRow(m_notifySound);
    notifyForm->addRow(QStringLiteral("Предупреждать за (мин):"), m_thresholds);

    auto *notifyHint = new QLabel(QStringLiteral(
        "Список минут через запятую — за сколько предупреждать о событии. "
        "Например «30, 10, 5» = три напоминания. Цельнодневные события пока не "
        "уведомляются. Кнопка «Тест» шлёт пробное уведомление в выбранном режиме."), notifyTab);
    notifyHint->setWordWrap(true);

    auto *notifyLay = new QVBoxLayout(notifyTab);
    notifyLay->addLayout(notifyForm);
    notifyLay->addWidget(m_testBtn);
    notifyLay->addWidget(notifyHint);
    notifyLay->addStretch();

    // ---------- вкладка «Общие» ----------
    auto *generalTab = new QWidget(this);
    m_autostart = new QCheckBox(
        QStringLiteral("Запускать при входе в систему (автозапуск)"), generalTab);
    m_autostart->setChecked(Autostart::isEnabled());
    m_startMin = new QCheckBox(
        QStringLiteral("Запускать свёрнутым в трей (при любом запуске)"), generalTab);
    m_startMin->setChecked(
        s.value(QStringLiteral("startup/minimized"), false).toBool());
    auto *gHint = new QLabel(QStringLiteral(
        "Автозапуск создаёт файл:\n%1\n"
        "Он также виден в штатном «Автозапуск программ» Astra Linux. "
        "При переносе бинарника путь в файле обновляется автоматически при старте.")
        .arg(Autostart::desktopPath()), generalTab);
    gHint->setWordWrap(true);
    auto *gLay = new QVBoxLayout(generalTab);
    gLay->addWidget(m_autostart);
    gLay->addWidget(m_startMin);
    gLay->addWidget(gHint);
    gLay->addStretch();

    // ---------- вкладка «Deck» ----------
    auto *deckTab = new QWidget(this);
    m_deckEnabled = new QCheckBox(QStringLiteral("Показывать карточки Deck"), deckTab);
    m_deckEnabled->setChecked(s.value(QStringLiteral("deck/enabled"), false).toBool());
    m_deckNoDue = new QCheckBox(QStringLiteral("Показывать карточки без срока"), deckTab);
    m_deckNoDue->setChecked(s.value(QStringLiteral("deck/showNoDue"), false).toBool());
    m_deckFind = new QPushButton(QStringLiteral("Найти доски"), deckTab);
    m_deckBoards = new QListWidget(deckTab);
    const QStringList savedBoards = s.value(QStringLiteral("deck/boards")).toStringList();
    for (const QString &e : savedBoards) {
        auto *it = new QListWidgetItem(e.section('|', 1), m_deckBoards);
        it->setData(Qt::UserRole, e.section('|', 0, 0).toInt());
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }
    auto *deckHint = new QLabel(QStringLiteral(
        "Карточки берутся через API Deck тем же логином и паролем приложения. "
        "Отметьте нужные доски. Окно «Карточки Deck» открывается с панели и из меню трея; "
        "напоминания о сроках идут через общий движок уведомлений."), deckTab);
    deckHint->setWordWrap(true);
    auto *deckLay = new QVBoxLayout(deckTab);
    deckLay->addWidget(m_deckEnabled);
    deckLay->addWidget(m_deckNoDue);
    deckLay->addWidget(m_deckFind);
    deckLay->addWidget(m_deckBoards, 1);
    deckLay->addWidget(deckHint);

    m_deckClient = new DeckClient(this);
    connect(m_deckClient, &DeckClient::boardsReady, this, [this](const QList<DeckBoard> &boards) {
        m_deckFind->setEnabled(true);
        m_deckFind->setText(QStringLiteral("Найти доски"));
        QSet<int> checked;
        for (int i = 0; i < m_deckBoards->count(); ++i) {
            auto *it = m_deckBoards->item(i);
            if (it->checkState() == Qt::Checked)
                checked.insert(it->data(Qt::UserRole).toInt());
        }
        const bool first = checked.isEmpty();
        m_deckBoards->clear();
        for (const DeckBoard &b : boards) {
            auto *it = new QListWidgetItem(b.title, m_deckBoards);
            it->setData(Qt::UserRole, b.id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(first || checked.contains(b.id) ? Qt::Checked : Qt::Unchecked);
        }
    });
    connect(m_deckClient, &DeckClient::failed, this, [this](const QString &, const QString &msg) {
        m_deckFind->setEnabled(true);
        m_deckFind->setText(QStringLiteral("Найти доски"));
        QMessageBox::warning(this, QStringLiteral("Deck"), msg);
    });
    connect(m_deckFind, &QPushButton::clicked, this, [this]() {
        const QString pass = m_pass->text().isEmpty() ? m_currentPassword : m_pass->text();
        if (m_url->text().trimmed().isEmpty() || m_user->text().trimmed().isEmpty() || pass.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Deck"),
                QStringLiteral("Заполните адрес, логин и пароль приложения на вкладке «Аккаунт»."));
            return;
        }
        m_deckClient->setAccount(m_url->text().trimmed(), m_user->text().trimmed(), pass);
        m_deckFind->setEnabled(false);
        m_deckFind->setText(QStringLiteral("Поиск…"));
        m_deckClient->fetchBoards();
    });

    // ---------- сборка ----------
    auto *tabs = new QTabWidget(this);
    tabs->addTab(accountTab, QStringLiteral("Аккаунт"));
    tabs->addTab(notifyTab, QStringLiteral("Уведомления"));
    tabs->addTab(generalTab, QStringLiteral("Общие"));
    tabs->addTab(deckTab, QStringLiteral("Deck"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(m_discoverBtn, &QPushButton::clicked, this, &SettingsDialog::discover);
    connect(m_authType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateAuthUi(); });
    connect(m_testBtn, &QPushButton::clicked, this, [this]() { emit testNotification(); });

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(tabs);
    lay->addWidget(buttons);

    m_client = new CalDavClient(this);
    connect(m_client, &CalDavClient::calendarsDiscovered, this, &SettingsDialog::onCalendars);
    connect(m_client, &CalDavClient::requestFailed, this, &SettingsDialog::onError);

    updateAuthUi();
}

void SettingsDialog::updateAuthUi()
{
    const bool pw = m_authType->currentData().toString() == QLatin1String("password");
    m_user->setEnabled(true);
    m_pass->setEnabled(pw);
    m_keyringHint->setVisible(pw);
    m_caFile->setEnabled(!pw);
    m_caBrowse->setEnabled(!pw);
    if (pw) {
        m_hint->setText(QStringLiteral(
            "Пароль приложения: Nextcloud → Настройки → Безопасность → Устройства и сессии.\n"
            "Список календарей ниже загружен из настроек. «Найти календари» обновляет его "
            "с сервера — при каждом запуске нажимать не нужно."));
    } else {
        m_hint->setText(QStringLiteral(
            "Сквозная авторизация: используется билет Kerberos доменной сессии, пароль не нужен.\n"
            "Логин — имя пользователя Nextcloud (обычно совпадает с доменным).\n"
            "Если билета нет, выполните kinit в терминале. Сертификат сервера проверяется\n"
            "системным хранилищем CA; при необходимости укажите файл корневого сертификата."));
    }
}

QString SettingsDialog::password() const
{
    return m_pass->text();
}

void SettingsDialog::discover()
{
    const bool kerberos = m_authType->currentData().toString() == QLatin1String("kerberos");
    const QString url = m_url->text().trimmed();
    const QString user = m_user->text().trimmed();
    const QString pass = m_pass->text().isEmpty() ? m_currentPassword : m_pass->text();
    if (url.isEmpty() || user.isEmpty() || (!kerberos && pass.isEmpty())) {
        QMessageBox::warning(this, QStringLiteral("nc-notify"),
            kerberos ? QStringLiteral("Заполните адрес сервера и логин.")
                     : QStringLiteral("Заполните адрес, логин и пароль приложения."));
        return;
    }
    m_client->setAuthMode(kerberos ? CalDavClient::KerberosAuth : CalDavClient::PasswordAuth);
    m_client->setCaFile(m_caFile->text().trimmed());
    m_client->setAccount(url, user, pass);
    m_discoverBtn->setEnabled(false);
    m_discoverBtn->setText(QStringLiteral("Поиск…"));
    m_client->discoverCalendars();
}

void SettingsDialog::onCalendars(const QList<CalendarInfo> &cals)
{
    m_discoverBtn->setEnabled(true);
    m_discoverBtn->setText(QStringLiteral("Найти календари"));
    QSet<QString> checked;
    for (int i = 0; i < m_cals->count(); ++i) {
        QListWidgetItem *it = m_cals->item(i);
        if (it->checkState() == Qt::Checked)
            checked.insert(it->data(Qt::UserRole).toString());
    }
    const bool firstRun = checked.isEmpty() && m_selectedUrls.isEmpty();
    m_cals->clear();
    for (const CalendarInfo &ci : cals) {
        auto *it = new QListWidgetItem(ci.name, m_cals);
        it->setData(Qt::UserRole, ci.url);
        it->setToolTip(ci.url);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(firstRun || checked.contains(ci.url) ? Qt::Checked : Qt::Unchecked);
    }
    m_hint->setText(QStringLiteral("Найдено календарей: %1. Отметьте нужные.").arg(cals.count()));
}

void SettingsDialog::onError(const QString &ctx, const QString &msg)
{
    Q_UNUSED(ctx);
    m_discoverBtn->setEnabled(true);
    m_discoverBtn->setText(QStringLiteral("Найти календари"));
    QMessageBox::warning(this, QStringLiteral("Ошибка подключения"), msg);
}

void SettingsDialog::accept()
{
    const QString url = m_url->text().trimmed();
    const QString user = m_user->text().trimmed();
    if (url.isEmpty() || user.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("nc-notify"),
                             QStringLiteral("Укажите адрес сервера и логин."));
        return;
    }
    const QUrl u(url);
    if (!u.isValid() || (u.scheme() != QLatin1String("https") && u.scheme() != QLatin1String("http"))) {
        QMessageBox::warning(this, QStringLiteral("nc-notify"),
                             QStringLiteral("Адрес должен начинаться с http:// или https://"));
        return;
    }
    QSettings s;
    s.setValue(QStringLiteral("account/authType"), m_authType->currentData().toString());
    s.setValue(QStringLiteral("account/caFile"), m_caFile->text().trimmed());
    s.setValue(QStringLiteral("account/serverUrl"), url);
    s.setValue(QStringLiteral("account/user"), user);
    QStringList entries;
    for (int i = 0; i < m_cals->count(); ++i) {
        QListWidgetItem *it = m_cals->item(i);
        if (it->checkState() == Qt::Checked) {
            CalendarInfo ci;
            ci.url = it->data(Qt::UserRole).toString();
            ci.name = it->text();
            entries << encodeCalendar(ci);
        }
    }
    s.setValue(QStringLiteral("account/calendars"), entries);
    s.setValue(QStringLiteral("sync/intervalMin"), m_interval->value());
    s.setValue(QStringLiteral("sync/horizonDays"), m_horizon->value());

    // уведомления
    s.setValue(QStringLiteral("notify/enabled"), m_notifyEnabled->isChecked());
    s.setValue(QStringLiteral("notify/mode"), m_notifyMode->currentData().toInt());
    s.setValue(QStringLiteral("notify/sound"), m_notifySound->isChecked());
    s.setValue(QStringLiteral("notify/thresholds"), m_thresholds->text().trimmed());
    s.setValue(QStringLiteral("startup/minimized"), m_startMin->isChecked());
    Autostart::setEnabled(m_autostart->isChecked());
    s.setValue(QStringLiteral("deck/enabled"), m_deckEnabled->isChecked());
    s.setValue(QStringLiteral("deck/showNoDue"), m_deckNoDue->isChecked());
    QStringList deckEntries;
    for (int i = 0; i < m_deckBoards->count(); ++i) {
        QListWidgetItem *it = m_deckBoards->item(i);
        if (it->checkState() == Qt::Checked)
            deckEntries << QString::number(it->data(Qt::UserRole).toInt()) + '|' + it->text();
    }
    s.setValue(QStringLiteral("deck/boards"), deckEntries);
    s.sync();

    // Запись пароля в связку ключей делает MainWindow::savePassword()
    // (job должен жить дольше диалога, иначе async-запись обрывается).

    QDialog::accept();
}
