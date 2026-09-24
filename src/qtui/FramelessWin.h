// FramelessWin.h - reusable frameless top-level window plumbing for Qt5.
// Windows implementation; other platforms compile to no-ops and keep their
// native decorations (call sites stay unmodified).
//
// Single self-contained header: copy it into any program, two calls.
//
//   // ctor of your top-level widget:
//   frameless::apply(this);
//
//   // forwarder for the one virtual you must override:
//   bool W::nativeEvent(const QByteArray& t, void* m, long* r) override
//   {
//       return frameless::nativeEvent(this, t, m, r,
//           [this](const QPoint& pos) { return myCaptionZone(pos); });
//   }
//
// Technique (Chatterino/Chromium): the window KEEPS the native caption/
// thickframe/maximizebox styles - so snap (drag-to-edge + Win+arrows),
// double-click maximize and aero shake all survive - while WM_NCCALCSIZE
// returns 0 so no frame is ever reserved. A 1px glass frame re-enables
// the DWM drop shadow that NCCALCSIZE=0 otherwise swallows (verified on
// Win10 19045). WM_NCHITTEST stays ours: an 8px border resizes, the
// caller-decided caption band returns HTCAPTION (native drag/snap), the
// rest is client.
//
// ponytail ceilings: frame metrics come from the primary monitor
// (per-monitor DPI not special-cased); Win11 corner rounding left at the
// system default; single top-level window per call site.
#pragma once

#include <functional>

#include <QCursor>
#include <QPoint>
#include <QWidget>

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
// children so their clicks still land).
inline bool nativeEvent(QWidget* window, const QByteArray& eventType,
                        void* message, long* result,
                        const std::function<Zone(const QPoint&)>& zoneAt)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam)
    {
        // First message means the real native window exists: buy back the
        // DWM drop shadow that returning 0 below would otherwise swallow.
        // (Idempotent and cheap - called on every recalc.)
        MARGINS glass = {0, 0, 0, 1};
        DwmExtendFrameIntoClientArea(msg->hwnd, &glass);
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
        if (!window->isMaximized())
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
    }
    return false;
}

}  // namespace frameless
#else
// Non-Windows: no-op - native decorations stay, call sites compile as-is.
namespace frameless
{
enum class Zone { Client, Caption };
inline void apply(QWidget*) {}
inline bool nativeEvent(QWidget*, const QByteArray&, void*, long*,
                        const std::function<Zone(const QPoint&)>&)
{
    return false;
}
}  // namespace frameless
#endif  // _WIN32
