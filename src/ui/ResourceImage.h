#pragma once

#include <afxwin.h>

#include <memory>

namespace Gdiplus {
class Bitmap;
}

class ResourceImage
{
public:
    ResourceImage();
    ~ResourceImage();

    ResourceImage(const ResourceImage &) = delete;
    ResourceImage &operator=(const ResourceImage &) = delete;

    bool loadFromResource(UINT resourceId, LPCWSTR resourceType = L"PNG");

    bool isValid() const;
    int width() const;
    int height() const;

    void drawTo(HDC hdc, const CRect &targetRect) const;

private:
    std::unique_ptr<Gdiplus::Bitmap> m_bitmap;
};
