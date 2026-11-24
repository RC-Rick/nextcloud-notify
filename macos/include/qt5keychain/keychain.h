#pragma once

// Подмножество API QtKeychain, реализованное поверх macOS Keychain.
// Нужно, чтобы общие файлы src/ (MainWindow.cpp, DeckWindow.cpp) собирались
// на macOS без правок: они включают <qt5keychain/keychain.h> и работают
// с ReadPasswordJob/WritePasswordJob асинхронно, через сигнал finished().

#include <QObject>
#include <QString>

namespace QKeychain {

enum Error {
    NoError = 0,
    EntryNotFound,
    AccessDenied,
    CouldNotLock,
    OtherError
};

class Job : public QObject
{
    Q_OBJECT
public:
    explicit Job(const QString &service, QObject *parent = nullptr);
    ~Job() override;

    Error error() const { return m_error; }
    QString errorString() const { return m_errorString; }

    QString service() const { return m_service; }
    QString key() const { return m_key; }
    void setKey(const QString &key) { m_key = key; }

    void setAutoDelete(bool autoDelete) { m_autoDelete = autoDelete; }
    bool autoDelete() const { return m_autoDelete; }

    virtual void start() = 0;

signals:
    void finished(QKeychain::Job *job);

protected:
    void finish(Error error, const QString &errorString = QString());

    QString m_service;
    QString m_key;
    Error m_error = NoError;
    QString m_errorString;
    bool m_autoDelete = true;
};

class ReadPasswordJob : public Job
{
    Q_OBJECT
public:
    explicit ReadPasswordJob(const QString &service, QObject *parent = nullptr);
    void start() override;
    QString textData() const { return m_textData; }
    QByteArray data() const { return m_textData.toUtf8(); }

private:
    QString m_textData;
};

class WritePasswordJob : public Job
{
    Q_OBJECT
public:
    explicit WritePasswordJob(const QString &service, QObject *parent = nullptr);
    void start() override;
    void setTextData(const QString &data) { m_textData = data; }
    void setData(const QByteArray &data) { m_textData = QString::fromUtf8(data); }

private:
    QString m_textData;
};

} // namespace QKeychain
