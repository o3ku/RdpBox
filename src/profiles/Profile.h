#pragma once

#include <QString>
#include <QUuid>

struct Profile {
    QString id;
    QString name;
    QString host;
    int port = 3389;
    QString username;
    QString password;
    bool clipboardEnabled = true;
    bool ignoreCertificate = true;

    static Profile create() {
        Profile p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        return p;
    }

    bool isValid() const { return !id.isEmpty() && !host.isEmpty(); }
};
