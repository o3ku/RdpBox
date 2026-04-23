#pragma once

#include <QList>
#include <QString>
#include "Profile.h"

class ProfileRepository
{
public:
    explicit ProfileRepository(const QString &filePath);

    QList<Profile> profiles() const;
    Profile profile(const QString &id) const;
    void addProfile(const Profile &profile);
    void updateProfile(const Profile &profile);
    void removeProfile(const QString &id);
    QList<Profile> search(const QString &query) const;

private:
    void load();
    void save() const;

    QString m_filePath;
    QList<Profile> m_profiles;
};
