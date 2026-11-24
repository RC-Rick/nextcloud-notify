#include <qt5keychain/keychain.h>

#include <QByteArray>

#include <Security/Security.h>

namespace {

CFStringRef cfString(const QString &s)
{
    return CFStringCreateWithCharacters(nullptr, reinterpret_cast<const UniChar *>(s.utf16()),
                                        static_cast<CFIndex>(s.size()));
}

QString fromCFString(CFStringRef s)
{
    if (!s)
        return QString();
    const CFIndex len = CFStringGetLength(s);
    QString out(static_cast<int>(len), Qt::Uninitialized);
    CFStringGetCharacters(s, CFRangeMake(0, len), reinterpret_cast<UniChar *>(out.data()));
    return out;
}

QString osStatusText(OSStatus status)
{
    CFStringRef msg = SecCopyErrorMessageString(status, nullptr);
    const QString text = msg ? fromCFString(msg)
                             : QStringLiteral("OSStatus %1").arg(int(status));
    if (msg)
        CFRelease(msg);
    return text;
}

QKeychain::Error mapStatus(OSStatus status)
{
    switch (status) {
    case errSecSuccess:
        return QKeychain::NoError;
    case errSecItemNotFound:
        return QKeychain::EntryNotFound;
    case errSecAuthFailed:
    case errSecInteractionNotAllowed:
        return QKeychain::AccessDenied;
    default:
        return QKeychain::OtherError;
    }
}

CFDictionaryRef baseQuery(CFStringRef service, CFStringRef account, bool returnData)
{
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(nullptr, 0,
                                                          &kCFTypeDictionaryKeyCallBacks,
                                                          &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(d, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(d, kSecAttrService, service);
    CFDictionarySetValue(d, kSecAttrAccount, account);
    if (returnData) {
        CFDictionarySetValue(d, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(d, kSecMatchLimit, kSecMatchLimitOne);
    }
    return d;
}

} // namespace

namespace QKeychain {

Job::Job(const QString &service, QObject *parent)
    : QObject(parent), m_service(service)
{
}

Job::~Job() = default;

void Job::finish(Error error, const QString &errorString)
{
    m_error = error;
    m_errorString = errorString;
    emit finished(this);
    if (m_autoDelete)
        deleteLater();
}

ReadPasswordJob::ReadPasswordJob(const QString &service, QObject *parent)
    : Job(service, parent)
{
}

void ReadPasswordJob::start()
{
    if (m_key.isEmpty()) {
        finish(OtherError, QStringLiteral("Не задан ключ хранилища"));
        return;
    }

    CFStringRef service = cfString(m_service);
    CFStringRef account = cfString(m_key);
    CFDictionaryRef query = baseQuery(service, account, true);

    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);

    if (status == errSecSuccess && result) {
        const auto *data = static_cast<CFDataRef>(result);
        m_textData = QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                                       static_cast<int>(CFDataGetLength(data)));
        CFRelease(result);
        finish(NoError);
    } else {
        finish(mapStatus(status), osStatusText(status));
    }

    CFRelease(query);
    CFRelease(service);
    CFRelease(account);
}

WritePasswordJob::WritePasswordJob(const QString &service, QObject *parent)
    : Job(service, parent)
{
}

void WritePasswordJob::start()
{
    if (m_key.isEmpty()) {
        finish(OtherError, QStringLiteral("Не задан ключ хранилища"));
        return;
    }

    CFStringRef service = cfString(m_service);
    CFStringRef account = cfString(m_key);
    const QByteArray utf8 = m_textData.toUtf8();
    CFDataRef value = CFDataCreate(nullptr,
                                   reinterpret_cast<const UInt8 *>(utf8.constData()),
                                   static_cast<CFIndex>(utf8.size()));

    CFDictionaryRef query = baseQuery(service, account, false);
    CFMutableDictionaryRef item = CFDictionaryCreateMutableCopy(nullptr, 0, query);
    CFDictionarySetValue(item, kSecValueData, value);
    CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleWhenUnlocked);

    OSStatus status = SecItemAdd(item, nullptr);
    if (status == errSecDuplicateItem) {
        const void *updateKeys[] = { kSecValueData };
        const void *updateValues[] = { value };
        CFDictionaryRef changes = CFDictionaryCreate(
            nullptr, updateKeys, updateValues, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        status = SecItemUpdate(query, changes);
        CFRelease(changes);
    }

    if (status == errSecSuccess)
        finish(NoError);
    else
        finish(mapStatus(status), osStatusText(status));

    CFRelease(item);
    CFRelease(query);
    CFRelease(value);
    CFRelease(service);
    CFRelease(account);
}

} // namespace QKeychain
