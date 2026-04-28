#include "ClosableTabCtrl.h"

#include "Win10Theme.h"

#include <algorithm>

namespace
{
constexpr int kCloseButtonSize = 16;
constexpr int kCloseButtonMargin = 6;
constexpr int kAccentBarHeight = 2;
}

IMPLEMENT_DYNAMIC(ClosableTabCtrl, CTabCtrl)

BEGIN_MESSAGE_MAP(ClosableTabCtrl, CTabCtrl)
    ON_WM_LBUTTONDOWN()
    ON_WM_MBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

ClosableTabCtrl::ClosableTabCtrl() = default;

ClosableTabCtrl::~ClosableTabCtrl() = default;

void ClosableTabCtrl::setCloseCallback(CloseCallback callback)
{
    m_closeCallback = std::move(callback);
}

CRect ClosableTabCtrl::closeButtonRect(const CRect &itemRect)
{
    CRect button;
    button.right = itemRect.right - kCloseButtonMargin;
    button.left = button.right - kCloseButtonSize;
    button.top = itemRect.top + (itemRect.Height() - kCloseButtonSize) / 2;
    button.bottom = button.top + kCloseButtonSize;
    return button;
}

int ClosableTabCtrl::hitTestCloseButton(CPoint point) const
{
    const int count = GetItemCount();
    for (int index = 0; index < count; ++index) {
        CRect itemRect;
        if (!GetItemRect(index, &itemRect))
            continue;

        const CRect button = closeButtonRect(itemRect);
        if (button.PtInRect(point))
            return index;
    }
    return -1;
}

int ClosableTabCtrl::hitTestTab(CPoint point) const
{
    TCHITTESTINFO hit = {};
    hit.pt = point;
    return HitTest(&hit);
}

void ClosableTabCtrl::invalidateTab(int index)
{
    if (index < 0)
        return;
    CRect rect;
    if (GetItemRect(index, &rect))
        InvalidateRect(rect, FALSE);
}

void ClosableTabCtrl::updateHover(int newTabIndex, int newCloseIndex)
{
    if (newTabIndex == m_hoverTabIndex && newCloseIndex == m_hoverCloseIndex)
        return;

    const int oldTab = m_hoverTabIndex;
    const int oldClose = m_hoverCloseIndex;
    m_hoverTabIndex = newTabIndex;
    m_hoverCloseIndex = newCloseIndex;

    invalidateTab(oldTab);
    invalidateTab(oldClose);
    if (newTabIndex != oldTab)
        invalidateTab(newTabIndex);
    if (newCloseIndex != oldClose)
        invalidateTab(newCloseIndex);

    if (m_hoverTabIndex >= 0 || m_hoverCloseIndex >= 0) {
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = GetSafeHwnd();
            ::TrackMouseEvent(&tme);
            m_trackingMouse = true;
        }
    }
}

BOOL ClosableTabCtrl::OnEraseBkgnd(CDC *dc)
{
    if (!dc)
        return FALSE;

    CRect rect;
    GetClientRect(&rect);
    dc->FillSolidRect(rect, Win10Theme::kSurfaceMuted);
    return TRUE;
}

void ClosableTabCtrl::DrawItem(LPDRAWITEMSTRUCT drawItem)
{
    if (!drawItem)
        return;

    CDC *dc = CDC::FromHandle(drawItem->hDC);
    if (!dc)
        return;

    const int index = static_cast<int>(drawItem->itemID);
    CRect itemRect = drawItem->rcItem;

    const bool selected = (drawItem->itemState & ODS_SELECTED) != 0;
    const bool hovered = (m_hoverTabIndex == index) || (m_hoverCloseIndex == index);

    COLORREF background;
    COLORREF textColor;
    if (selected) {
        background = Win10Theme::kSurface;
        textColor = Win10Theme::kText;
    } else if (hovered) {
        background = Win10Theme::kAccentLight;
        textColor = Win10Theme::kText;
    } else {
        background = Win10Theme::kSurfaceSubtle;
        textColor = Win10Theme::kTextSubtle;
    }

    dc->FillSolidRect(itemRect, background);

    if (!selected) {
        CPen separatorPen(PS_SOLID, 1, Win10Theme::kBorderSubtle);
        CPen *oldPen = dc->SelectObject(&separatorPen);
        dc->MoveTo(itemRect.right - 1, itemRect.top + 4);
        dc->LineTo(itemRect.right - 1, itemRect.bottom - 4);
        dc->SelectObject(oldPen);
    }

    if (selected) {
        CRect accent(itemRect);
        accent.top = accent.bottom - kAccentBarHeight;
        dc->FillSolidRect(accent, Win10Theme::kAccent);
    }

    wchar_t label[256] = {};
    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    item.pszText = label;
    item.cchTextMax = static_cast<int>(std::size(label));
    GetItem(index, &item);

    const CRect closeRect = closeButtonRect(itemRect);

    CRect textRect = itemRect;
    textRect.left += 12;
    textRect.right = closeRect.left - 6;

    const int oldBkMode = dc->SetBkMode(TRANSPARENT);
    const COLORREF oldTextColor = dc->SetTextColor(textColor);
    dc->DrawTextW(label, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    dc->SetTextColor(oldTextColor);
    dc->SetBkMode(oldBkMode);

    const bool closeHovered = (m_hoverCloseIndex == index);
    if (closeHovered) {
        CBrush hoverBrush(Win10Theme::kCloseHover);
        dc->FillRect(closeRect, &hoverBrush);
    }

    const COLORREF crossColor = closeHovered ? Win10Theme::kCloseHoverText : Win10Theme::kTextSubtle;
    CPen pen(PS_SOLID, 1, crossColor);
    CPen *oldPen = dc->SelectObject(&pen);
    const int inset = 5;
    dc->MoveTo(closeRect.left + inset, closeRect.top + inset);
    dc->LineTo(closeRect.right - inset, closeRect.bottom - inset);
    dc->MoveTo(closeRect.right - inset - 1, closeRect.top + inset);
    dc->LineTo(closeRect.left + inset - 1, closeRect.bottom - inset);
    dc->SelectObject(oldPen);
}

void ClosableTabCtrl::OnLButtonDown(UINT flags, CPoint point)
{
    const int closeIndex = hitTestCloseButton(point);
    if (closeIndex >= 0) {
        if (m_closeCallback)
            m_closeCallback(closeIndex);
        return;
    }

    CTabCtrl::OnLButtonDown(flags, point);
}

void ClosableTabCtrl::OnMButtonUp(UINT flags, CPoint point)
{
    const int index = hitTestTab(point);
    if (index >= 0 && m_closeCallback) {
        m_closeCallback(index);
        return;
    }

    CTabCtrl::OnMButtonUp(flags, point);
}

void ClosableTabCtrl::OnMouseMove(UINT flags, CPoint point)
{
    const int closeIndex = hitTestCloseButton(point);
    const int tabIndex = (closeIndex >= 0) ? closeIndex : hitTestTab(point);
    updateHover(tabIndex, closeIndex);

    CTabCtrl::OnMouseMove(flags, point);
}

void ClosableTabCtrl::OnMouseLeave()
{
    m_trackingMouse = false;
    updateHover(-1, -1);
    CTabCtrl::OnMouseLeave();
}
