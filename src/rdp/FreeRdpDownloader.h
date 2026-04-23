#pragma once

#include <QObject>

class QNetworkReply;

class FreeRdpDownloader : public QObject
{
    Q_OBJECT

public:
    explicit FreeRdpDownloader(QObject *parent = nullptr);

    QString ensureAvailable(const QString &appDir, QWidget *parentWidget = nullptr);

private:
    QString findAssetUrl(const QByteArray &json);
    bool downloadFile(const QString &url, const QString &destPath, QWidget *parentWidget);
    bool extractZip(const QString &zipPath, const QString &targetDir);
};
