#pragma once

#include <QCursor>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QSize>

#include <memory>

class FreeRdpProcess : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Starting, Running, Finished };

    explicit FreeRdpProcess(QObject *parent = nullptr);
    ~FreeRdpProcess();

    void start(const QString &host,
               int port,
               const QString &username,
               const QString &password,
               int width = 0,
               int height = 0,
               bool clipboardEnabled = true,
               bool ignoreCertificate = true);
    void stop();

    State state() const;
    QImage frame() const;
    QSize desktopSize() const;
    QCursor cursor() const;

    void sendFocusIn();
    void sendKeyMessage(quint32 message, quintptr wParam, qintptr lParam);
    void sendMouseMove(const QPoint &pos, const QSize &viewSize);
    void sendMouseButton(Qt::MouseButton button, bool down,
                         const QPoint &pos, const QSize &viewSize);
    void sendWheel(const QPoint &angleDelta, const QPoint &pos, const QSize &viewSize);
    void requestResize(const QSize &size);
    void updateFrameFromBackend(const QImage &frame, const QSize &desktopSize);
    void updateStateFromBackend(State state);
    void updateCursorFromBackend(const QCursor &cursor, bool hidden);
    void resetCursorFromBackend();
    void attachClipboardChannel(void *channelContext);
    void detachClipboardChannel();

signals:
    void stateChanged(FreeRdpProcess::State newState);
    void frameUpdated();
    void desktopResized(const QSize &size);
    void cursorUpdated();

private:
    struct Private;

    void setState(State newState);

    std::unique_ptr<Private> m_d;
};
