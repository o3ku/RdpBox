#include "ProfileRepository.h"

#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ProfileRepository::ProfileRepository(const QString &filePath)
    : m_filePath(filePath)
{
    load();
}

QList<Profile> ProfileRepository::profiles() const
{
    return m_profiles;
}

Profile ProfileRepository::profile(const QString &id) const
{
    for (const auto &p : m_profiles) {
        if (p.id == id)
            return p;
    }
    return {};
}

void ProfileRepository::addProfile(const Profile &profile)
{
    m_profiles.append(profile);
    save();
}

void ProfileRepository::updateProfile(const Profile &profile)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == profile.id) {
            m_profiles[i] = profile;
            save();
            return;
        }
    }
}

void ProfileRepository::removeProfile(const QString &id)
{
    for (int i = m_profiles.size() - 1; i >= 0; --i) {
        if (m_profiles[i].id == id)
            m_profiles.removeAt(i);
    }
    save();
}

QList<Profile> ProfileRepository::search(const QString &query) const
{
    if (query.isEmpty())
        return m_profiles;

    QList<Profile> result;
    QString lower = query.toLower();
    for (const auto &p : m_profiles) {
        if (p.name.toLower().contains(lower) || p.host.toLower().contains(lower))
            result.append(p);
    }
    return result;
}

static Profile profileFromJson(const QJsonObject &obj)
{
    Profile p;
    p.id = obj["id"].toString();
    p.name = obj["name"].toString();
    p.host = obj["host"].toString();
    p.port = obj["port"].toInt(3389);
    p.username = obj["username"].toString();
    p.password = obj["password"].toString();
    p.clipboardEnabled = obj["clipboardEnabled"].toBool(true);
    p.ignoreCertificate = obj["ignoreCertificate"].toBool(true);
    return p;
}

static QJsonObject profileToJson(const Profile &p)
{
    QJsonObject obj;
    obj["id"] = p.id;
    obj["name"] = p.name;
    obj["host"] = p.host;
    obj["port"] = p.port;
    obj["username"] = p.username;
    obj["password"] = p.password;
    obj["clipboardEnabled"] = p.clipboardEnabled;
    obj["ignoreCertificate"] = p.ignoreCertificate;
    return obj;
}

void ProfileRepository::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray())
        return;

    m_profiles.clear();
    for (const auto &val : doc.array()) {
        if (val.isObject())
            m_profiles.append(profileFromJson(val.toObject()));
    }
}

void ProfileRepository::save() const
{
    QJsonArray arr;
    for (const auto &p : m_profiles)
        arr.append(profileToJson(p));

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(arr).toJson());
    file.close();
}
