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

#include <QOperatingSystemVersion>
#include <QPoint>
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
