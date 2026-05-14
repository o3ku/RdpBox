#include "RdpSessionView.h"

#include "common/AppPaths.h"
#include "rdp/FrameBufferMemory.h"
#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

namespace Gdiplus
{
using std::min;
using std::max;
}
#include <gdiplus.h>

namespace
{
std::wstring captureTimestamp()
{
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u-%03u",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

bool isFrameCaptureEnabled()
{
    wchar_t value[8] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"RDPBOX_CAPTURE_FRAMES", value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value))
        return false;

    return value[0] == L'1'
        || value[0] == L't'
        || value[0] == L'T'
        || value[0] == L'y'
        || value[0] == L'Y';
}

int getJpegEncoderClsid(CLSID *clsid)
{
    if (!clsid)
        return -1;

    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0)
        return -1;

    auto buffer = std::make_unique<BYTE[]>(size);
    auto *encoders = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.get());
    if (Gdiplus::GetImageEncoders(num, size, encoders) != Gdiplus::Ok)
        return -1;

    for (UINT i = 0; i < num; ++i) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/jpeg") == 0) {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool saveFrameAsJpeg(const FrameBuffer &frame, const std::wstring &path)
{
    if (frame.empty())
        return false;

    CLSID jpegClsid = {};
    if (getJpegEncoderClsid(&jpegClsid) < 0)
        return false;

    Gdiplus::Bitmap bitmap(frame.width, frame.height, frame.stride, PixelFormat32bppARGB,
                           const_cast<BYTE *>(frame.pixels.data()));

    Gdiplus::EncoderParameters params = {};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    ULONG quality = 90;
    params.Parameter[0].Value = &quality;

    return bitmap.Save(path.c_str(), &jpegClsid, &params) == Gdiplus::Ok;
}

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

    ::StretchDIBits(targetDc,
                    0, 0, targetRect.Width(), targetRect.Height(),
                    0, 0, frame.width, frame.height,
                    frame.pixels.data(),
                    &bmi,
                    DIB_RGB_COLORS,
                    SRCCOPY);
}
}

void CRdpSessionView::beginFrameCapture(const wchar_t *reason)
{
    m_captureDirectory.clear();
    m_captureFramesRemaining = 0;
    m_captureFrameIndex = 0;

    if (!isFrameCaptureEnabled())
        return;

    const std::wstring root = AppPaths::frameCaptureRootPath();
    if (root.empty())
        return;

    const std::wstring sessionName = m_profile.name.empty() ? L"unnamed" : m_profile.name;
    std::wstring safeName = sessionName;
    for (auto &ch : safeName) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }

    const std::wstring directory = root + L"\\" + captureTimestamp() + L"_" + safeName + L"_" + reason;
    ::CreateDirectoryW(directory.c_str(), nullptr);
    m_captureDirectory = directory;
    m_captureFramesRemaining = 20;
    m_captureFrameIndex = 0;
}

void CRdpSessionView::captureFrameIfRequested(const FrameBuffer &frame)
{
    if (m_captureFramesRemaining <= 0 || m_captureDirectory.empty() || frame.empty())
        return;

    wchar_t fileName[64] = {};
    swprintf_s(fileName, L"frame-%02d.jpg", m_captureFrameIndex + 1);
    const std::wstring path = m_captureDirectory + L"\\" + fileName;
    if (saveFrameAsJpeg(frame, path)) {
        ++m_captureFrameIndex;
        --m_captureFramesRemaining;
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
                KillTimer(kResumeRecoveryTimerId);
                if (m_overlayText == L"Resuming session...")
                    hideOverlayAfterFramePresent = true;
            }

            const FrameBuffer &frame = nextFrame;
            captureFrameIfRequested(frame);

            bool shouldRenderFrame = true;
            if (m_frameGateActive) {
                if (m_frameGateRemaining > 0)
                    --m_frameGateRemaining;

                if (m_frameGateRemaining == 0) {
                    m_frameGateActive = false;
                    m_waitingForFirstContentFrame = false;
                    m_resolutionUpdatePending = false;
                    hideOverlayAfterFramePresent = true;
                } else {
                    shouldRenderFrame = false;
                }
            } else {
                if (m_waitingForFirstContentFrame) {
                    m_waitingForFirstContentFrame = false;
                    if (!m_resolutionUpdatePending)
                        hideOverlayAfterFramePresent = true;
                }
            }

            if (shouldRenderFrame) {
                m_cachedFrame = std::move(nextFrame);
            }
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
