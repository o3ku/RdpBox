#include "ResourceImage.h"

#include <algorithm>
#include <cstring>

namespace Gdiplus
{
using std::min;
using std::max;
}

#include <gdiplus.h>

ResourceImage::ResourceImage() = default;

ResourceImage::~ResourceImage() = default;

bool ResourceImage::loadFromResource(UINT resourceId, LPCWSTR resourceType)
{
    HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC resource = ::FindResourceW(module, MAKEINTRESOURCEW(resourceId), resourceType);
    if (!resource)
        return false;

    DWORD size = ::SizeofResource(module, resource);
    HGLOBAL handle = ::LoadResource(module, resource);
    if (!handle || size == 0)
        return false;

    void *data = ::LockResource(handle);
    if (!data)
        return false;

    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory)
        return false;

    void *memoryData = ::GlobalLock(memory);
    if (!memoryData) {
        ::GlobalFree(memory);
        return false;
    }

    std::memcpy(memoryData, data, size);
    ::GlobalUnlock(memory);

    IStream *stream = nullptr;
    if (FAILED(::CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        ::GlobalFree(memory);
        return false;
    }

    auto bitmap = std::make_unique<Gdiplus::Bitmap>(stream);
    stream->Release();

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        return false;

    m_bitmap = std::move(bitmap);
    return true;
}

bool ResourceImage::isValid() const
{
    return m_bitmap != nullptr;
}

int ResourceImage::width() const
{
    return m_bitmap ? static_cast<int>(m_bitmap->GetWidth()) : 0;
}

int ResourceImage::height() const
{
    return m_bitmap ? static_cast<int>(m_bitmap->GetHeight()) : 0;
}

void ResourceImage::drawTo(HDC hdc, const CRect &targetRect) const
{
    if (!m_bitmap || !hdc)
        return;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    Gdiplus::Rect dest(targetRect.left, targetRect.top, targetRect.Width(), targetRect.Height());
    graphics.DrawImage(m_bitmap.get(), dest);
}
