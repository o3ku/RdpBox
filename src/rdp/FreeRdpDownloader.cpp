#include "FreeRdpDownloader.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>

FreeRdpDownloader::FreeRdpDownloader(QObject *parent)
    : QObject(parent)
{
}

QString FreeRdpDownloader::ensureAvailable(const QString &appDir, QWidget *parentWidget)
{
    const QString exePath = appDir + "/wfreerdp.exe";
    if (QFile::exists(exePath))
        return exePath;

    QNetworkAccessManager nam;

    QNetworkRequest req(QUrl("https://api.github.com/repos/FreeRDP/FreeRDP/releases/latest"));
    req.setRawHeader("User-Agent", "RdpBox");

    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return {};
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QString assetUrl = findAssetUrl(data);
    if (assetUrl.isEmpty())
        return {};

    QString zipPath = QDir::tempPath() + "/wfreerdp-download.zip";
    if (!downloadFile(assetUrl, zipPath, parentWidget))
        return {};

    if (!extractZip(zipPath, appDir)) {
        QFile::remove(zipPath);
        return {};
    }

    QFile::remove(zipPath);

    return QFile::exists(exePath) ? exePath : QString();
}

QString FreeRdpDownloader::findAssetUrl(const QByteArray &json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json);
    for (const auto &val : doc["assets"].toArray()) {
        QJsonObject obj = val.toObject();
        QString name = obj["name"].toString().toLower();
        if (name.contains("windows") && name.contains("x64") && name.endsWith(".zip"))
            return obj["browser_download_url"].toString();
    }
    return {};
}

bool FreeRdpDownloader::downloadFile(const QString &url, const QString &destPath,
                                      QWidget *parentWidget)
{
    QNetworkAccessManager nam;

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "RdpBox");

    QNetworkReply *reply = nam.get(req);

    QProgressDialog progress("Downloading FreeRDP...", QString(), 0, 0, parentWidget);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setCancelButton(nullptr);

    QFile file(destPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    connect(reply, &QNetworkReply::readyRead, [&]() {
        file.write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total) {
        if (total > 0) {
            progress.setMaximum(100);
            progress.setValue(static_cast<int>(received * 100 / total));
        } else {
            progress.setLabelText(
                QString("Downloading... %1 MB").arg(received / 1024 / 1024));
        }
    });

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    file.write(reply->readAll());
    file.close();

    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    if (!ok)
        QFile::remove(destPath);

    return ok;
}

bool FreeRdpDownloader::extractZip(const QString &zipPath, const QString &targetDir)
{
    QProcess proc;
    proc.start("powershell", {"-NoProfile", "-Command",
        QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
            .arg(QDir::toNativeSeparators(zipPath),
                 QDir::toNativeSeparators(targetDir))});
    proc.waitForFinished(60000);
    if (proc.exitCode() != 0)
        return false;

    // If wfreerdp.exe is in a subdirectory, move files up
    if (!QFile::exists(targetDir + "/wfreerdp.exe")) {
        QDir dir(targetDir);
        for (const auto &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString subDir = targetDir + "/" + entry;
            if (QFile::exists(subDir + "/wfreerdp.exe")) {
                QDir sd(subDir);
                for (const auto &f : sd.entryList(QDir::Files))
                    QFile::rename(sd.filePath(f), dir.filePath(f));
                dir.rmdir(entry);
                break;
            }
        }
    }

    return QFile::exists(targetDir + "/wfreerdp.exe");
}
