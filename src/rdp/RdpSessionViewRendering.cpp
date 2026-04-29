#include "RdpSessionView.h"

#include "common/AppPaths.h"
#include "rdp/FrameBufferMemory.h"
#include <algorithm>
#include <cstring>
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

bool CRdpSessionView::ensureRenderSurface(const FrameBuffer &frame)
{
    if (frame.empty())
        return false;

    if (m_renderDc && m_renderBitmap && m_renderBits
        && m_renderWidth == frame.width
        && m_renderHeight == frame.height) {
        return true;
    }

    releaseRenderSurface();

    HDC screenDc = ::GetDC(nullptr);
    if (!screenDc)
        return false;

    m_renderDc = ::CreateCompatibleDC(screenDc);
    if (!m_renderDc) {
        ::ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = frame.width;
    bmi.bmiHeader.biHeight = -frame.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_renderBitmap = ::CreateDIBSection(m_renderDc, &bmi, DIB_RGB_COLORS, &m_renderBits, nullptr, 0);
    ::ReleaseDC(nullptr, screenDc);

    if (!m_renderBitmap || !m_renderBits) {
        releaseRenderSurface();
        return false;
    }

    m_renderOldBitmap = ::SelectObject(m_renderDc, m_renderBitmap);
    m_renderWidth = frame.width;
    m_renderHeight = frame.height;
    return true;
}

void CRdpSessionView::releaseRenderSurface()
{
    if (m_renderDc && m_renderOldBitmap) {
        ::SelectObject(m_renderDc, m_renderOldBitmap);
        m_renderOldBitmap = nullptr;
    }

    if (m_renderBitmap) {
        ::DeleteObject(m_renderBitmap);
        m_renderBitmap = nullptr;
    }

    if (m_renderDc) {
        ::DeleteDC(m_renderDc);
        m_renderDc = nullptr;
    }

    m_renderBits = nullptr;
    m_renderWidth = 0;
    m_renderHeight = 0;
    m_renderedFrameGeneration = 0;
}

void CRdpSessionView::copyFrameToRenderSurface(const FrameBuffer &frame)
{
    if (!m_renderBits || frame.empty())
        return;

    const int dstStride = m_renderWidth * 4;
    auto *dst = static_cast<std::uint8_t *>(m_renderBits);
    const auto *src = frame.pixels.data();

    if (frame.stride == dstStride) {
        std::memcpy(dst, src, static_cast<std::size_t>(dstStride) * static_cast<std::size_t>(frame.height));
        return;
    }

    for (int y = 0; y < frame.height; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstStride),
                    src + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride),
                    static_cast<std::size_t>(dstStride));
    }
}

void CRdpSessionView::drawRenderSurface(HDC targetDc, const CRect &targetRect) const
{
    if (!targetDc || !m_renderDc || !m_renderBitmap)
        return;

    if (targetRect.Width() == m_renderWidth && targetRect.Height() == m_renderHeight) {
        ::BitBlt(targetDc, 0, 0, m_renderWidth, m_renderHeight, m_renderDc, 0, 0, SRCCOPY);
        return;
    }

    ::SetStretchBltMode(targetDc, COLORONCOLOR);
    ::StretchBlt(targetDc,
                 0, 0, targetRect.Width(), targetRect.Height(),
                 m_renderDc,
                 0, 0, m_renderWidth, m_renderHeight,
                 SRCCOPY);
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
        m_process->visitFrameIfNewer(m_cachedFrameGeneration, [this, &hideOverlayAfterFramePresent](const FrameBuffer &frame, uint64_t generation) {
            captureFrameIfRequested(frame);

            bool shouldRenderFrame = true;
            if (m_frameGateActive) {
                if (m_frameGateRemaining > 0)
                    --m_frameGateRemaining;

                if (m_frameGateRemaining == 0) {
                    m_frameGateActive = false;
                    m_waitingForFirstContentFrame = false;
                    m_resolutionUpdatePending = false;
                    m_pendingDesktopSize = {};
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

            if (!shouldRenderFrame)
                return;

            if (ensureRenderSurface(frame)) {
                copyFrameToRenderSurface(frame);
                m_renderedFrameGeneration = generation;
                FrameBufferMemory::release(m_cachedFrame);
                return;
            }

            m_cachedFrame = frame;
        });

        const bool hasCachedFrame = !m_cachedFrame.empty();
        bool paintedFrame = false;
        if (hasCachedFrame && ensureRenderSurface(m_cachedFrame)) {
            if (m_renderedFrameGeneration != m_cachedFrameGeneration) {
                copyFrameToRenderSurface(m_cachedFrame);
                m_renderedFrameGeneration = m_cachedFrameGeneration;
                FrameBufferMemory::release(m_cachedFrame);
            }
            drawRenderSurface(dc.GetSafeHdc(), rect);
            paintedFrame = true;
        } else if (hasCachedFrame) {
            const FrameBuffer &frame = m_cachedFrame;
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = frame.width;
            bmi.bmiHeader.biHeight = -frame.height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            if (rect.Width() == frame.width && rect.Height() == frame.height) {
                SetDIBitsToDevice(dc.GetSafeHdc(),
                                  0, 0,
                                  static_cast<DWORD>(frame.width),
                                  static_cast<DWORD>(frame.height),
                                  0, 0,
                                  0,
                                  static_cast<UINT>(frame.height),
                                  frame.pixels.data(),
                                  &bmi,
                                  DIB_RGB_COLORS);
            } else {
                StretchDIBits(dc.GetSafeHdc(), 0, 0, rect.Width(), rect.Height(), 0, 0,
                              frame.width, frame.height, frame.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            }
            paintedFrame = true;
        } else if (m_renderDc && m_renderBitmap) {
            drawRenderSurface(dc.GetSafeHdc(), rect);
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
