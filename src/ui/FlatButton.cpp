#include "FlatButton.h"

#include "Win10Theme.h"

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

    if (disabled) {
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

    CBrush borderBrush(border);
    dc.FrameRect(rect, &borderBrush);

    HFONT font = nullptr;
    if (CFont *inherited = GetFont())
        font = reinterpret_cast<HFONT>(inherited->GetSafeHandle());
    HGDIOBJ oldFont = font ? ::SelectObject(dc.GetSafeHdc(), font) : nullptr;

    CString text;
    GetWindowText(text);

    const int oldBkMode = dc.SetBkMode(TRANSPARENT);
    const COLORREF oldText = dc.SetTextColor(textColor);
    dc.DrawTextW(text, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    dc.SetTextColor(oldText);
    dc.SetBkMode(oldBkMode);

    if (oldFont)
        ::SelectObject(dc.GetSafeHdc(), oldFont);

    if (focused && !disabled) {
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
