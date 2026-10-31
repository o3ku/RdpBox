#include "RdpSessionView.h"

#include "common/rdp/FrameBufferMemory.h"
#include "common/rdp/RdpCursorClassifier.h"
#include "common/rdp/RdpSessionViewBehavior.h"
#include <algorithm>
#include <utility>

namespace
{
void drawFrameBuffer(HDC targetDc, const CRect &targetRect, const FrameBuffer &frame)
{
    if (!targetDc || frame.empty())
        return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = frame.width;
    bmi.bmiHeader.biHeight = -frame.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (targetRect.Width() == frame.width && targetRect.Height() == frame.height) {
        ::SetDIBitsToDevice(targetDc,
                            0, 0,
                            static_cast<DWORD>(frame.width),
                            static_cast<DWORD>(frame.height),
                            0, 0,
                            0,
                            static_cast<UINT>(frame.height),
                            frame.pixels.data(),
                            &bmi,
                            DIB_RGB_COLORS);
        return;
    }

    // COLORONCOLOR drops rows instead of AND-scanning them: the default
    // BLACKONWHITE darkens 32bpp frames scaled below 100%.
    ::SetStretchBltMode(targetDc, COLORONCOLOR);
    ::StretchDIBits(targetDc,
                    0, 0, targetRect.Width(), targetRect.Height(),
                    0, 0, frame.width, frame.height,
                    frame.pixels.data(),
                    &bmi,
                    DIB_RGB_COLORS,
                    SRCCOPY);
}
}

BOOL CRdpSessionView::OnEraseBkgnd(CDC *dc)
{
    UNREFERENCED_PARAMETER(dc);
    return TRUE;
}

void CRdpSessionView::OnPaint()
{
    CPaintDC dc(this);
    CRect rect;
    GetClientRect(&rect);
    bool hideOverlayAfterFramePresent = false;

    if (m_process) {
        FrameBuffer nextFrame;
        if (m_process->consumeFrameIfNewer(m_cachedFrameGeneration, nextFrame)) {
            if (m_resumeRecovery.awaitingFrame()) {
                m_resumeRecovery.onFrameArrived();
                if (m_overlayText == L"Resuming session...")
                    hideOverlayAfterFramePresent = true;
            } else if (m_reconnectInFlight) {
                // The re-established session is alive and what is on screen is
                // real again, even when the view had no visible surface while
                // it was being set up.
                m_reconnectInFlight = false;
                if (m_overlayText == rdp::session_view::startOverlayText(true).c_str())
                    hideOverlayAfterFramePresent = true;
            }

            const FrameBuffer &frame = nextFrame;

            const rdp::session_view::FrameArrivalDecision frameDecision =
                rdp::session_view::frameArrivalDecision(m_frameGate);
            m_frameGate = frameDecision.state;
            hideOverlayAfterFramePresent = hideOverlayAfterFramePresent || frameDecision.hideOverlay;

            if (frameDecision.renderFrame) {
                m_cachedFrame = std::move(nextFrame);
            }

            if (m_resolutionRecovery.active())
                m_resolutionRecovery.onFrameProgress(frameDecision.resolutionFrameProgress);

            syncRecoveryTimer();
        }

        const bool hasCachedFrame = !m_cachedFrame.empty();
        bool paintedFrame = false;
        if (hasCachedFrame) {
            drawFrameBuffer(dc.GetSafeHdc(), rect, m_cachedFrame);
            paintedFrame = true;
        } else {
            dc.FillSolidRect(rect, RGB(17, 17, 17));
        }

        if (paintedFrame && hideOverlayAfterFramePresent)
            m_overlayText.Empty();
    } else {
        dc.FillSolidRect(rect, RGB(17, 17, 17));
    }

    if (!m_overlayText.IsEmpty())
        drawOverlay(dc, rect);

    // A repaint is a snapshot of the session, cursor included: the remote
    // pointer is only right if nothing overwrote it while the view was away.
    // When it is over this view - the case the user notices - put it back.
    CursorInfo cursor;
    if (!m_remoteCursor.needsApply() || !m_remoteCursor.cursorToApply(cursor))
        return;

    POINT position = {};
    if (!::GetCursorPos(&position))
        return;

    ::ScreenToClient(GetSafeHwnd(), &position);
    if (!rect.PtInRect(CPoint(position.x, position.y)))
        return;

    if (const HCURSOR handle = RdpCursorClassifier::cursorHandleFromInfo(cursor)) {
        ::SetCursor(handle);
        m_remoteCursor.noteApplied();
    }
}

void CRdpSessionView::drawOverlay(CDC &dc, const CRect &rect)
{
    HDC overlayDc = ::CreateCompatibleDC(dc.GetSafeHdc());
    HBITMAP overlayBitmap = ::CreateCompatibleBitmap(dc.GetSafeHdc(), 1, 1);
    HGDIOBJ oldBitmap = nullptr;
    if (overlayDc && overlayBitmap) {
        oldBitmap = ::SelectObject(overlayDc, overlayBitmap);

        RECT overlayRect = { 0, 0, 1, 1 };
        HBRUSH overlayBrush = ::CreateSolidBrush(RGB(12, 12, 12));
        ::FillRect(overlayDc, &overlayRect, overlayBrush);
        ::DeleteObject(overlayBrush);

        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 176;
        ::AlphaBlend(dc.GetSafeHdc(), 0, 0, rect.Width(), rect.Height(),
                     overlayDc, 0, 0, 1, 1, blend);
        ::SelectObject(overlayDc, oldBitmap);
    }

    if (overlayBitmap)
        ::DeleteObject(overlayBitmap);
    if (overlayDc)
        ::DeleteDC(overlayDc);

    CRect textRect = rect;
    textRect.DeflateRect(48, 48);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(230, 230, 230));
    CFont *oldFont = nullptr;
    if (m_overlayFont.GetSafeHandle())
        oldFont = dc.SelectObject(&m_overlayFont);

    CRect measuredRect = textRect;
    dc.DrawText(m_overlayText, &measuredRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);

    CRect centeredRect = textRect;
    const int textWidth = std::min(textRect.Width(), measuredRect.Width());
    const int textHeight = std::min(textRect.Height(), measuredRect.Height());
    centeredRect.left = textRect.left + (textRect.Width() - textWidth) / 2;
    centeredRect.top = textRect.top + (textRect.Height() - textHeight) / 2;
    centeredRect.right = centeredRect.left + textWidth;
    centeredRect.bottom = centeredRect.top + textHeight;

    dc.DrawText(m_overlayText, &centeredRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

    if (oldFont)
        dc.SelectObject(oldFont);
}
