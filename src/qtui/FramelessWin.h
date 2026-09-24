// FramelessWin.h - reusable frameless top-level window plumbing for Qt.
// Drop this single header into any project; two calls per window:
//
//   // ctor of your top-level widget:
//   frameless::apply(this);
//
//   // forward the one Qt virtual (Qt5: long*, Qt6: qintptr* - both work):
//   bool W::nativeEvent(const QByteArray& t, void* m, long* r) override
//   {
//       return frameless::nativeEvent(this, t, m, r,
//           [this](const QPoint& pos) { return myCaptionZone(pos); });
//   }
//
// myCaptionZone decides which local points are the caption band (return
// Zone::Caption there, Zone::Client over interactive children so their
// clicks still land; edges always resize).
//
//   // optional: a fixed-size frameless window - just use plain Qt:
//   setFixedSize(480, 320);   // resize hit zones are skipped automatically
//
// Ready-made chrome (same plumbing, single implementation, both platforms):
//   frameless::Dialog        - Win10-metric title bar + close button;
//                              contentLayout(); height via ctor (default 32)
//   frameless::MainWindow    - Win10-metric bar with app icon, title and
//                              min/max/close; contentLayout(); height via
//                              ctor (default 32). setTitleIcon(QIcon())
//                              hides the stock logo for custom chrome.
//
// Platform behavior:
//   Win8/10/11 - frameless via WM_NCCALCSIZE=0 with native snap (drag-to-
//                edge, Win+arrows), double-click maximize and the DWM drop
//                shadow (1px glass frame) kept. Caller window hints
//                (always-on-top, ...) are preserved; Qt::Dialog windows
//                keep their flags (top-level with owner).
//   Win7       - same, plus: DWM NC rendering disabled, classic "UAH" frame
//                messages blocked and a square window region pinned - the
//                native glass frame/caption buttons never leak through
//                (Basic theme/RDP included). No DWM shadow on Win7; draw
//                your own outline if the silhouette needs one.
//   other OS   - no-ops; windows keep their native decorations.
//
// Requirements: Qt >= 5.10 (QOperatingSystemVersion); MSVC auto-links
// dwmapi (MinGW: add -ldwmapi). Single top-level window per apply() call.
//
// Technique (Chatterino/Chromium): the window KEEPS the native caption/
// thickframe/maximizebox styles - so snap (drag-to-edge + Win+arrows),
// double-click maximize and aero shake all survive - while WM_NCCALCSIZE
// returns 0 so no frame is ever reserved. A 1px glass frame re-enables
// the DWM drop shadow that NCCALCSIZE=0 otherwise swallows (verified on
// Win10 19045). Win7's DWM would keep painting its native glass frame
// and caption buttons over the client (fully visible on focus loss), so
// there DWM non-client rendering is disabled instead (no shadow, but no
// ghost frame). WM_NCHITTEST stays ours: an 8px border resizes, the
// caller-decided caption band returns HTCAPTION (native drag/snap), the
// rest is client.
//
//   // optional: a fixed-size frameless window - just use plain Qt:
//   setFixedSize(480, 320);   // nativeEvent skips resize zones for it
//
// ponytail ceilings: frame metrics come from the primary monitor
// (per-monitor DPI not special-cased); Win11 corner rounding left at the
// system default; single top-level window per call site.
#pragma once

#include <functional>

#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QPoint>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace frameless
{
// Win7's DWM keeps compositing the native glass frame + caption buttons for
// any window that keeps WS_CAPTION (fully visible on focus loss), which
// NCCALCSIZE=0 cannot suppress. Win8+ DWM doesn't, and needs the 1px glass
// trick for the drop shadow instead.
inline bool needsWin7FrameWorkaround()
{
#ifdef _WIN32
    static const bool win7 = QOperatingSystemVersion::current()
        <= QOperatingSystemVersion::Windows7;
    return win7;
#else
    return false;
#endif
}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#pragma comment(lib, "dwmapi.lib")

namespace frameless
{
inline constexpr int kResizeBorder = 8;  // px hit zone for edge resize

enum class Zone
{
    Client,   // clicks land on widgets
    Caption,  // HTCAPTION: native drag, double-click maximize, snap
};

// Flags only; call once in the ctor. The 1px glass frame is applied
// lazily in nativeEvent (first WM_NCCALCSIZE) - forcing winId() here would
// create the native window too early: for a parented QDialog that happens
// before the transient-parent linkage exists, and Windows then creates a
// full-size WS_CHILD of the parent instead of a popup. That stray child
// swallows all mouse input over the parent (dead drag/resize) until the
// process exits.
inline void apply(QWidget* window)
{
    if (window->windowType() != Qt::Dialog)
        // Replace only the window-type bits, keep caller hints (always-on-top,
        // fixed-size, ...); a plain reset would silently strip them.
        window->setWindowFlags((window->windowFlags() & ~Qt::WindowType_Mask)
                               | Qt::Window);
}

// Returns true when the message was consumed. zoneAt decides which local
// points belong to the caption band (return Zone::Caption for interactive
// children so their clicks still land). Result is deduced: Qt5 passes
// long*, Qt6 qintptr* - no caller difference.
template <typename Result>
inline bool nativeEvent(QWidget* window, const QByteArray& eventType,
                        void* message, Result* result,
                        const std::function<Zone(const QPoint&)>& zoneAt)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG* msg = static_cast<const MSG*>(message);
    // Win7: no DWM frame compositing and no classic NC painting at all -
    // both would draw the native frame (glass border + caption buttons)
    // over the client that NCCALCSIZE=0 handed us. Cost: no DWM shadow.
    if (needsWin7FrameWorkaround())
    {
        if (msg->message == WM_NCPAINT)
        {
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCACTIVATE)
        {
            *result = TRUE;
            return true;
        }
        // No-composition path (Basic theme / RDP): the classic "UAH" frame
        // messages paint the native caption + buttons over the client.
        // Blocking them is what Chromium does; styles (and thus snap) stay.
        if (msg->message == 0x00AE /* WM_NCUAHDRAWCAPTION */
            || msg->message == 0x00AF /* WM_NCUAHDRAWFRAME */)
        {
            *result = 0;
            return true;
        }
        // DWM keeps rounding the window silhouette even with NC rendering
        // disabled; pin an explicit rectangular region (no shadow on this
        // path anyway, so nothing is lost).
        if (msg->message == WM_SIZE)
        {
            RECT rc;
            GetClientRect(msg->hwnd, &rc);
            HRGN rgn = CreateRectRgn(0, 0, rc.right, rc.bottom);
            if (!SetWindowRgn(msg->hwnd, rgn, FALSE))
                DeleteObject(rgn);  // on success the system owns the region
        }
    }
    if (msg->message == WM_NCCALCSIZE && msg->wParam)
    {
        if (needsWin7FrameWorkaround())
        {
            DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
            DwmSetWindowAttribute(msg->hwnd, DWMWA_NCRENDERING_POLICY,
                                  &policy, sizeof(policy));
        }
        else
        {
            // First message means the real native window exists: buy back the
            // DWM drop shadow that returning 0 below would otherwise swallow.
            // (Idempotent and cheap - called on every recalc.)
            MARGINS glass = {0, 0, 0, 1};
            DwmExtendFrameIntoClientArea(msg->hwnd, &glass);
        }
        // Client area = whole window (no native frame reserved). When
        // maximized the system inflates the rect by the frame thickness;
        // clip it back or content bleeds off screen on every side.
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
        if (IsZoomed(msg->hwnd))
        {
            const LONG frame = GetSystemMetrics(SM_CXSIZEFRAME)
                + GetSystemMetrics(SM_CXPADDEDBORDER);
            params->rgrc[0].left += frame;
            params->rgrc[0].top += frame;
            params->rgrc[0].right -= frame;
            params->rgrc[0].bottom -= frame;
        }
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCHITTEST)
    {
        // Hit-test the queried point (lParam), not the live cursor: real
        // mouse input passes the same coordinates, synthetic queries
        // (automation, accessibility) stay correct too.
        const QPoint pos = window->mapFromGlobal(
            QPoint(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)));
        const bool x1 = pos.x() < kResizeBorder;
        const bool x2 = pos.x() >= window->width() - kResizeBorder;
        const bool y1 = pos.y() < kResizeBorder;
        const bool y2 = pos.y() >= window->height() - kResizeBorder;
        // Fixed-size windows (QWidget::setFixedSize) get no resize zones:
        // min == max means every drag would be clamped right back anyway.
        const bool fixedSize = window->minimumSize() == window->maximumSize();
        if (!window->isMaximized() && !fixedSize)
        {
            if (x1 && y1) { *result = HTTOPLEFT; return true; }
            if (x2 && y1) { *result = HTTOPRIGHT; return true; }
            if (x1 && y2) { *result = HTBOTTOMLEFT; return true; }
            if (x2 && y2) { *result = HTBOTTOMRIGHT; return true; }
            if (x1) { *result = HTLEFT; return true; }
            if (x2) { *result = HTRIGHT; return true; }
            if (y1) { *result = HTTOP; return true; }
            if (y2) { *result = HTBOTTOM; return true; }
        }
        if (zoneAt && zoneAt(pos) == Zone::Caption)
        {
            *result = HTCAPTION;
            return true;
        }
        // Never fall through to DefWindowProc: the window keeps WS_CAPTION
        // for snap, and DefWindowProc would answer HTCLOSE/HTMINBUTTON/...
        // over the top-right corner - on Win7 DWM then paints the native
        // caption buttons (ghost buttons over the custom ones) on hover.
        *result = HTCLIENT;
        return true;
    }
    return false;
}

}  // namespace frameless
#else
// other OS: no-op - native decorations stay, call sites compile as-is.
namespace frameless
{
enum class Zone { Client, Caption };
inline bool needsWin7FrameWorkaround() { return false; }
inline void apply(QWidget*) {}
template <typename Result>
inline bool nativeEvent(QWidget*, const QByteArray&, void*, Result*,
                        const std::function<Zone(const QPoint&)>&)
{
    return false;
}
}  // namespace frameless
#endif  // _WIN32

// Convenience dialog (both platforms, single implementation on top of the
// platform functions above): draggable title bar with a close button; add
// content through contentLayout(). Restyle via the accessors - object names
// "framelessTitleBar"/"framelessCloseButton" are QSS hooks (rename to your
// app's names to reuse its stylesheet). Non-Windows hides the bar (native
// decorations provide title + close). Win7 paints the outline ring set via
// setOutlineColor into the reserved 1px band.
namespace frameless
{
class Dialog : public QDialog
{
public:
    explicit Dialog(QWidget* parent, const QString& title, int titleBarHeight = 32)
        : QDialog(parent)
    {
        apply(this);
        setWindowTitle(title);
        // Pin the application icon for taskbar/Alt-Tab - dialog windows
        // don't reliably inherit it on every platform.
        if (!QGuiApplication::windowIcon().isNull())
            setWindowIcon(QGuiApplication::windowIcon());

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        if (needsWin7FrameWorkaround())
            root->setContentsMargins(1, 1, 1, 1);  // outline band

        m_titleBar = new QWidget(this);
        m_titleBar->setObjectName(QStringLiteral("framelessTitleBar"));
        m_titleBar->setFixedHeight(titleBarHeight);
        auto* row = new QHBoxLayout(m_titleBar);
        // Win10 caption metrics: 8px icon inset, 16px icon, 8px to the text.
        row->setContentsMargins(8, 0, 0, 0);
        row->setSpacing(0);
        row->addWidget(new QLabel(title, m_titleBar));
        row->addStretch(1);
        m_closeButton = new QToolButton(m_titleBar);
        m_closeButton->setObjectName(QStringLiteral("framelessCloseButton"));
        m_closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        m_closeButton->setAutoRaise(true);
        m_closeButton->setFocusPolicy(Qt::NoFocus);
        connect(m_closeButton, &QToolButton::clicked, this, &QDialog::reject);
        row->addWidget(m_closeButton);
        root->addWidget(m_titleBar);

        m_contentLayout = new QVBoxLayout;
        m_contentLayout->setContentsMargins(0, 0, 0, 0);
        root->addLayout(m_contentLayout);
#ifndef _WIN32
        m_titleBar->hide();  // native decorations provide title + close
#endif
    }

    QVBoxLayout* contentLayout() const { return m_contentLayout; }
    QWidget* titleBar() const { return m_titleBar; }
    QToolButton* closeButton() const { return m_closeButton; }
    void setOutlineColor(const QColor& color) { m_outlineColor = color; }

    // Small leading icon in the title bar (brand logo).
    void setTitleIcon(const QIcon& icon)
    {
        if (icon.isNull())
            return;
        if (!m_titleIcon) {
            m_titleIcon = new QLabel(m_titleBar);
            m_titleIcon->setPixmap(icon.pixmap(16, 16));
            auto* row = static_cast<QHBoxLayout*>(m_titleBar->layout());
            row->insertWidget(0, m_titleIcon);
            row->insertSpacing(1, 8);
        } else {
            m_titleIcon->setPixmap(icon.pixmap(16, 16));
        }
    }

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override
#else
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override
#endif
    {
        return nativeEventImpl(eventType, message, result);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QDialog::paintEvent(event);
#ifdef _WIN32
        if (needsWin7FrameWorkaround() && m_outlineColor.isValid()) {
            QPainter painter(this);
            painter.setPen(m_outlineColor);
            painter.drawRect(QRect(0, 0, width() - 1, height() - 1));
        }
#endif
    }

private:
    template <typename Result>
    bool nativeEventImpl(const QByteArray& eventType, void* message, Result* result)
    {
        return frameless::nativeEvent(this, eventType, message, result,
                                      [this](const QPoint& pos) { return zoneFor(pos); });
    }

    Zone zoneFor(const QPoint& pos) const
    {
        if (!m_titleBar->geometry().contains(pos))
            return Zone::Client;
        if (m_closeButton->geometry().translated(m_titleBar->pos()).contains(pos))
            return Zone::Client;
        return Zone::Caption;
    }

    QWidget* m_titleBar = nullptr;
    QLabel* m_titleIcon = nullptr;
    QToolButton* m_closeButton = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QColor m_outlineColor;
};

// Convenience frameless main window: Win10-metric title bar (32px default,
// settable) with app icon, title, and min/max/close buttons; add the UI
// through contentLayout() (a QVBoxLayout). Restyle via accessors/object
// names as with Dialog; on non-Windows the bar hides and native
// decorations stay. RdpBox keeps its own tab-bearing bar instead - this
// class is for consumers wanting a ready standard chrome.
class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(int titleBarHeight = 32)
    {
        apply(this);

        auto* root = new QWidget(this);
        auto* rootLayout = new QVBoxLayout(root);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);
        if (needsWin7FrameWorkaround())
            rootLayout->setContentsMargins(1, 1, 1, 1);  // outline band
        setCentralWidget(root);

        m_titleBar = new QWidget(root);
        m_titleBar->setObjectName(QStringLiteral("framelessTitleBar"));
        m_titleBar->setFixedHeight(titleBarHeight);
        auto* row = new QHBoxLayout(m_titleBar);
        row->setContentsMargins(8, 0, 0, 0);
        row->setSpacing(0);

        m_titleIcon = new QLabel(m_titleBar);
        m_titleIcon->setPixmap(QGuiApplication::windowIcon().pixmap(16, 16));
        m_titleLabel = new QLabel(m_titleBar);
        row->addWidget(m_titleIcon);
        row->addSpacing(8);
        row->addWidget(m_titleLabel);
        row->addStretch(1);

        const auto makeButton = [this](QStyle::StandardPixmap icon, const char* name) {
            auto* button = new QToolButton(m_titleBar);
            button->setObjectName(QLatin1String(name));
            button->setIcon(style()->standardIcon(icon));
            button->setIconSize(QSize(16, 16));
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            return button;
        };
        m_minimizeButton = makeButton(QStyle::SP_TitleBarMinButton, "framelessMinButton");
        m_maximizeButton = makeButton(QStyle::SP_TitleBarMaxButton, "framelessMaxButton");
        m_closeButton = makeButton(QStyle::SP_TitleBarCloseButton, "framelessCloseButton");
        connect(m_minimizeButton, &QToolButton::clicked, this, &QWidget::showMinimized);
        connect(m_maximizeButton, &QToolButton::clicked, this, &MainWindow::toggleMaximize);
        connect(m_closeButton, &QToolButton::clicked, this, &QWidget::close);
        for (QToolButton* button : {m_minimizeButton, m_maximizeButton, m_closeButton})
            button->setFixedSize(46, titleBarHeight);
        row->addWidget(m_minimizeButton);
        row->addWidget(m_maximizeButton);
        row->addWidget(m_closeButton);
        rootLayout->addWidget(m_titleBar);

        m_contentLayout = new QVBoxLayout;
        m_contentLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->addLayout(m_contentLayout);
#ifndef _WIN32
        m_titleBar->hide();  // native decorations provide the chrome
#endif
    }

    QVBoxLayout* contentLayout() const { return m_contentLayout; }
    QWidget* titleBar() const { return m_titleBar; }
    QToolButton* minimizeButton() const { return m_minimizeButton; }
    QToolButton* maximizeButton() const { return m_maximizeButton; }
    QToolButton* closeButton() const { return m_closeButton; }
    void setOutlineColor(const QColor& color) { m_outlineColor = color; }

    // 16px leading icon; a null icon hides it (custom logos welcome).
    void setTitleIcon(const QIcon& icon)
    {
        if (icon.isNull()) {
            m_titleIcon->hide();
            return;
        }
        m_titleIcon->show();
        m_titleIcon->setPixmap(icon.pixmap(16, 16));
    }

    // Toggle maximize/restore on the caption button (drag/double-click go
    // through the native HTCAPTION path already).
    void toggleMaximize()
    {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    }

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override
#else
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override
#endif
    {
        return nativeEventImpl(eventType, message, result);
    }

    void changeEvent(QEvent* event) override
    {
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::WindowTitleChange)
            m_titleLabel->setText(windowTitle());
        if (event->type() == QEvent::WindowStateChange)
            m_maximizeButton->setIcon(style()->standardIcon(
                isMaximized() ? QStyle::SP_TitleBarNormalButton
                              : QStyle::SP_TitleBarMaxButton));
    }

    void paintEvent(QPaintEvent* event) override
    {
        QMainWindow::paintEvent(event);
#ifdef _WIN32
        if (needsWin7FrameWorkaround() && m_outlineColor.isValid()) {
            QPainter painter(this);
            painter.setPen(m_outlineColor);
            painter.drawRect(QRect(0, 0, width() - 1, height() - 1));
        }
#endif
    }

private:
    template <typename Result>
    bool nativeEventImpl(const QByteArray& eventType, void* message, Result* result)
    {
        return frameless::nativeEvent(this, eventType, message, result,
                                      [this](const QPoint& pos) { return zoneFor(pos); });
    }

    Zone zoneFor(const QPoint& pos) const
    {
        if (!m_titleBar->geometry().contains(pos))
            return Zone::Client;
        for (const QToolButton* button : {m_minimizeButton, m_maximizeButton, m_closeButton})
            if (button->geometry().translated(m_titleBar->pos()).contains(pos))
                return Zone::Client;
        return Zone::Caption;
    }

    QWidget* m_titleBar = nullptr;
    QLabel* m_titleIcon = nullptr;
    QLabel* m_titleLabel = nullptr;
    QToolButton* m_minimizeButton = nullptr;
    QToolButton* m_maximizeButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QColor m_outlineColor;
};
}  // namespace frameless
