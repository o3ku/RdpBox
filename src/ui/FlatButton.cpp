#include "FlatButton.h"

#include "ResourceImage.h"
#include "Win10Theme.h"

#include <algorithm>

#include <uxtheme.h>

IMPLEMENT_DYNAMIC(FlatButton, CButton)

BEGIN_MESSAGE_MAP(FlatButton, CButton)
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

FlatButton::FlatButton() = default;

FlatButton::~FlatButton() = default;

void FlatButton::setDefault(bool isDefault)
{
    if (m_default == isDefault)
        return;
    m_default = isDefault;
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

void FlatButton::setToolbarStyle(bool enabled)
{
    if (m_toolbarStyle == enabled)
        return;
    m_toolbarStyle = enabled;
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

void FlatButton::setIconKind(IconKind kind)
{
    if (m_iconKind == kind)
        return;
    m_iconKind = kind;
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

bool FlatButton::setIconResource(UINT resourceId, LPCWSTR resourceType)
{
    auto image = std::make_unique<ResourceImage>();
    if (!image->loadFromResource(resourceId, resourceType))
        return false;

    m_iconImage = std::move(image);
    if (GetSafeHwnd())
        Invalidate(FALSE);
    return true;
}

void FlatButton::drawIconImage(CDC &dc, const CRect &rect) const
{
    if (!m_iconImage || !m_iconImage->isValid())
        return;

    constexpr int kIconRender = 16;
    const int x = rect.left + (rect.Width() - kIconRender) / 2;
    const int y = rect.top + (rect.Height() - kIconRender) / 2;
    CRect target(x, y, x + kIconRender, y + kIconRender);
    m_iconImage->drawTo(dc.GetSafeHdc(), target);
}

void FlatButton::drawIcon(CDC &dc, const CRect &rect, COLORREF color) const
{
    if (m_iconKind == IconKind::None)
        return;

    const int cx = rect.left + rect.Width() / 2;
    const int cy = rect.top + rect.Height() / 2;
    const int half = 7;

    CPen pen(PS_SOLID, 2, color);
    CPen *oldPen = dc.SelectObject(&pen);

    if (m_iconKind == IconKind::Plus) {
        dc.MoveTo(cx - half, cy);
        dc.LineTo(cx + half + 1, cy);
        dc.MoveTo(cx, cy - half);
        dc.LineTo(cx, cy + half + 1);
    } else if (m_iconKind == IconKind::List) {
        for (int i = -1; i <= 1; ++i) {
            const int y = cy + i * 5;
            dc.MoveTo(cx - half, y);
            dc.LineTo(cx + half + 1, y);
        }
    }

    dc.SelectObject(oldPen);
}

void FlatButton::DrawItem(LPDRAWITEMSTRUCT drawItem)
{
    if (!drawItem)
        return;

    HDC originalDc = drawItem->hDC;
    if (!originalDc)
        return;

    CRect rect = drawItem->rcItem;

    HDC bufferedDc = nullptr;
    HPAINTBUFFER buffer = ::BeginBufferedPaint(originalDc, &rect, BPBF_TOPDOWNDIB,
                                               nullptr, &bufferedDc);
    HDC targetDc = bufferedDc ? bufferedDc : originalDc;

    CDC dc;
    dc.Attach(targetDc);

    const bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    const bool focused = (drawItem->itemState & ODS_FOCUS) != 0;
    const bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;

    COLORREF background;
    COLORREF textColor;
    COLORREF border;
    bool drawBorder = !m_toolbarStyle;

    if (m_toolbarStyle) {
        if (disabled) {
            background = Win10Theme::kCaptionBg;
            textColor = Win10Theme::kCaptionTextSubtle;
        } else if (pressed) {
            background = Win10Theme::kCaptionTabActive;
            textColor = Win10Theme::kCaptionText;
        } else if (m_hover) {
            background = Win10Theme::kCaptionButtonHover;
            textColor = Win10Theme::kCaptionText;
        } else {
            background = Win10Theme::kCaptionBg;
            textColor = Win10Theme::kCaptionText;
        }
        border = background;
    } else if (disabled) {
        background = Win10Theme::kSurfaceMuted;
        textColor = Win10Theme::kTextDisabled;
        border = Win10Theme::kBorderSubtle;
    } else if (m_default) {
        if (pressed || m_hover) {
            background = Win10Theme::kAccentDark;
            border = Win10Theme::kAccentDark;
        } else {
            background = Win10Theme::kAccent;
            border = Win10Theme::kAccent;
        }
        textColor = RGB(255, 255, 255);
    } else if (pressed) {
        background = Win10Theme::kAccentPressed;
        textColor = Win10Theme::kText;
        border = Win10Theme::kAccent;
    } else if (m_hover) {
        background = Win10Theme::kAccentLight;
        textColor = Win10Theme::kText;
        border = Win10Theme::kAccent;
    } else {
        background = Win10Theme::kSurface;
        textColor = Win10Theme::kText;
        border = Win10Theme::kBorder;
    }

    dc.FillSolidRect(rect, background);

    if (drawBorder) {
        CBrush borderBrush(border);
        dc.FrameRect(rect, &borderBrush);
    }

    HFONT font = nullptr;
    if (CFont *inherited = GetFont())
        font = reinterpret_cast<HFONT>(inherited->GetSafeHandle());
    HGDIOBJ oldFont = font ? ::SelectObject(dc.GetSafeHdc(), font) : nullptr;

    if (m_iconImage && m_iconImage->isValid()) {
        drawIconImage(dc, rect);
    } else if (m_iconKind != IconKind::None) {
        drawIcon(dc, rect, textColor);
    } else {
        CString text;
        GetWindowText(text);

        const int oldBkMode = dc.SetBkMode(TRANSPARENT);
        const COLORREF oldText = dc.SetTextColor(textColor);
        dc.DrawTextW(text, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc.SetTextColor(oldText);
        dc.SetBkMode(oldBkMode);
    }

    if (oldFont)
        ::SelectObject(dc.GetSafeHdc(), oldFont);

    if (focused && !disabled && !m_toolbarStyle) {
        CRect focusRect(rect);
        focusRect.DeflateRect(3, 3);
        dc.DrawFocusRect(focusRect);
    }

    dc.Detach();

    if (buffer) {
        ::BufferedPaintSetAlpha(buffer, &rect, 255);
        ::EndBufferedPaint(buffer, TRUE);
    }
}

void FlatButton::OnMouseMove(UINT flags, CPoint point)
{
    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = GetSafeHwnd();
        ::TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    if (!m_hover) {
        m_hover = true;
        Invalidate(FALSE);
    }

    CButton::OnMouseMove(flags, point);
}

void FlatButton::OnMouseLeave()
{
    m_trackingMouse = false;
    if (m_hover) {
        m_hover = false;
        Invalidate(FALSE);
    }
    CButton::OnMouseLeave();
}
