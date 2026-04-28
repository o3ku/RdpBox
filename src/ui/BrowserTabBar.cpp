#include "BrowserTabBar.h"

#include "ParentResizeForwarder.h"
#include "Win10Theme.h"

#include <algorithm>

#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

namespace
{
constexpr int kTabMinWidth = 120;
constexpr int kTabMaxWidth = 220;
constexpr int kTabPadding = 12;
constexpr int kCloseButtonSize = 16;
constexpr int kCloseButtonMargin = 6;
constexpr int kAccentBarHeight = 2;

HFONT createTabFont(int pixelHeight, int weight)
{
    LOGFONTW lf = {};
    lf.lfHeight = -pixelHeight;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return ::CreateFontIndirectW(&lf);
}
}

IMPLEMENT_DYNAMIC(BrowserTabBar, CWnd)

BEGIN_MESSAGE_MAP(BrowserTabBar, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BrowserTabBar::BrowserTabBar() = default;

BrowserTabBar::~BrowserTabBar() = default;

bool BrowserTabBar::create(CWnd *parent, const CRect &rect, UINT id)
{
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                                                  nullptr);
    const bool created = CreateEx(0, className, L"BrowserTabBar",
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                  rect, parent, id) != FALSE;
    if (created)
        ensureFonts();
    return created;
}

void BrowserTabBar::ensureFonts()
{
    if (!m_tabFont.GetSafeHandle()) {
        HFONT raw = createTabFont(14, FW_NORMAL);
        if (raw)
            m_tabFont.Attach(raw);
    }
    if (!m_tabFontBold.GetSafeHandle()) {
        HFONT raw = createTabFont(14, FW_BOLD);
        if (raw)
            m_tabFontBold.Attach(raw);
    }
}

int BrowserTabBar::insertTab(const std::wstring &title)
{
    TabItem item;
    item.title = title;
    m_tabs.push_back(item);
    const int index = static_cast<int>(m_tabs.size()) - 1;
    if (m_selectedIndex < 0)
        m_selectedIndex = index;
    recomputeLayout();
    Invalidate(FALSE);
    return index;
}

void BrowserTabBar::removeTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;

    m_tabs.erase(m_tabs.begin() + index);

    if (m_tabs.empty()) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex == index) {
        m_selectedIndex = std::min(index, static_cast<int>(m_tabs.size()) - 1);
    } else if (m_selectedIndex > index) {
        --m_selectedIndex;
    }

    if (m_hoverTabIndex >= static_cast<int>(m_tabs.size()))
        m_hoverTabIndex = -1;
    if (m_hoverCloseIndex >= static_cast<int>(m_tabs.size()))
        m_hoverCloseIndex = -1;

    recomputeLayout();
    Invalidate(FALSE);
}

void BrowserTabBar::clearTabs()
{
    m_tabs.clear();
    m_selectedIndex = -1;
    m_hoverTabIndex = -1;
    m_hoverCloseIndex = -1;
    Invalidate(FALSE);
}

int BrowserTabBar::tabCount() const
{
    return static_cast<int>(m_tabs.size());
}

void BrowserTabBar::setSelectedIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    if (m_selectedIndex == index)
        return;
    m_selectedIndex = index;
    Invalidate(FALSE);
}

int BrowserTabBar::selectedIndex() const
{
    return m_selectedIndex;
}

void BrowserTabBar::setTabTitle(int index, const std::wstring &title)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    m_tabs[index].title = title;
    invalidateTab(index);
}

std::wstring BrowserTabBar::tabTitle(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return {};
    return m_tabs[index].title;
}

void BrowserTabBar::setSelectionChangedCallback(SelectionChangedCallback callback)
{
    m_selectionChanged = std::move(callback);
}

void BrowserTabBar::setCloseRequestedCallback(CloseRequestedCallback callback)
{
    m_closeRequested = std::move(callback);
}

int BrowserTabBar::hitTestTabAtScreenPoint(CPoint screenPoint) const
{
    CPoint clientPoint = screenPoint;
    const_cast<BrowserTabBar *>(this)->ScreenToClient(&clientPoint);
    return hitTestTab(clientPoint);
}

void BrowserTabBar::recomputeLayout()
{
    if (m_tabs.empty())
        return;

    CRect clientRect;
    GetClientRect(&clientRect);
    const int totalWidth = clientRect.Width();
    const int count = static_cast<int>(m_tabs.size());
    int tabWidth = totalWidth / count;
    tabWidth = std::clamp(tabWidth, kTabMinWidth, kTabMaxWidth);
    if (tabWidth * count > totalWidth)
        tabWidth = std::max(40, totalWidth / count);

    int x = 0;
    for (int i = 0; i < count; ++i) {
        TabItem &item = m_tabs[i];
        item.rect = CRect(x, 0, x + tabWidth, clientRect.bottom);
        item.closeRect.right = item.rect.right - kCloseButtonMargin;
        item.closeRect.left = item.closeRect.right - kCloseButtonSize;
        item.closeRect.top = item.rect.top + (item.rect.Height() - kCloseButtonSize) / 2;
        item.closeRect.bottom = item.closeRect.top + kCloseButtonSize;
        x += tabWidth;
    }
}

int BrowserTabBar::hitTestTab(CPoint clientPoint) const
{
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].rect.PtInRect(clientPoint))
            return static_cast<int>(i);
    }
    return -1;
}

int BrowserTabBar::hitTestClose(CPoint clientPoint) const
{
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].closeRect.PtInRect(clientPoint))
            return static_cast<int>(i);
    }
    return -1;
}

void BrowserTabBar::invalidateTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    InvalidateRect(m_tabs[index].rect, FALSE);
}

void BrowserTabBar::ensureMouseTracking()
{
    if (m_trackingMouse)
        return;
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = GetSafeHwnd();
    ::TrackMouseEvent(&tme);
    m_trackingMouse = true;
}

BOOL BrowserTabBar::OnEraseBkgnd(CDC *dc)
{
    if (!dc)
        return FALSE;
    CRect rect;
    GetClientRect(&rect);
    dc->FillSolidRect(rect, Win10Theme::kCaptionBg);
    return TRUE;
}

void BrowserTabBar::OnPaint()
{
    PAINTSTRUCT ps = {};
    HDC hdc = ::BeginPaint(GetSafeHwnd(), &ps);

    CRect clientRect;
    GetClientRect(&clientRect);

    HDC bufferedDc = nullptr;
    HPAINTBUFFER buffer = ::BeginBufferedPaint(hdc, &clientRect, BPBF_TOPDOWNDIB,
                                               nullptr, &bufferedDc);
    HDC targetDc = bufferedDc ? bufferedDc : hdc;

    CDC memDc;
    memDc.Attach(targetDc);

    memDc.FillSolidRect(clientRect, Win10Theme::kCaptionBg);

    ensureFonts();
    HGDIOBJ oldFont = ::SelectObject(memDc.GetSafeHdc(),
                                     m_tabFont.GetSafeHandle()
                                         ? m_tabFont.GetSafeHandle()
                                         : ::GetStockObject(DEFAULT_GUI_FONT));

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const TabItem &item = m_tabs[i];
        const bool selected = (static_cast<int>(i) == m_selectedIndex);
        const bool hovered = (static_cast<int>(i) == m_hoverTabIndex) || (static_cast<int>(i) == m_hoverCloseIndex);

        COLORREF background;
        COLORREF textColor;
        if (selected) {
            background = Win10Theme::kCaptionTabActive;
            textColor = Win10Theme::kCaptionText;
        } else if (hovered) {
            background = Win10Theme::kCaptionTabHover;
            textColor = Win10Theme::kCaptionText;
        } else {
            background = Win10Theme::kCaptionTabInactive;
            textColor = Win10Theme::kCaptionTextSubtle;
        }

        memDc.FillSolidRect(item.rect, background);

        if (!selected && static_cast<int>(i) + 1 < static_cast<int>(m_tabs.size())) {
            CRect separator(item.rect);
            separator.left = separator.right - 1;
            separator.top += 6;
            separator.bottom -= 6;
            memDc.FillSolidRect(separator, Win10Theme::kCaptionSeparator);
        }

        if (selected) {
            CRect accent(item.rect);
            accent.top = accent.bottom - kAccentBarHeight;
            memDc.FillSolidRect(accent, Win10Theme::kAccent);
        }

        CRect textRect(item.rect);
        textRect.left += kTabPadding;
        textRect.right = item.closeRect.left - 4;

        HFONT tabFont = selected && m_tabFontBold.GetSafeHandle()
            ? reinterpret_cast<HFONT>(m_tabFontBold.GetSafeHandle())
            : (m_tabFont.GetSafeHandle()
                ? reinterpret_cast<HFONT>(m_tabFont.GetSafeHandle())
                : reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
        ::SelectObject(memDc.GetSafeHdc(), tabFont);

        const int oldBkMode = memDc.SetBkMode(TRANSPARENT);
        const COLORREF oldTextColor = memDc.SetTextColor(textColor);
        memDc.DrawTextW(item.title.c_str(), -1, &textRect,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        memDc.SetTextColor(oldTextColor);
        memDc.SetBkMode(oldBkMode);

        const bool closeHovered = (static_cast<int>(i) == m_hoverCloseIndex);
        if (closeHovered) {
            CBrush hoverBrush(Win10Theme::kCloseHover);
            memDc.FillRect(item.closeRect, &hoverBrush);
        }

        const COLORREF crossColor = closeHovered ? Win10Theme::kCloseHoverText : Win10Theme::kCaptionTextSubtle;
        CPen pen(PS_SOLID, 1, crossColor);
        CPen *oldPen = memDc.SelectObject(&pen);
        const int inset = 5;
        memDc.MoveTo(item.closeRect.left + inset, item.closeRect.top + inset);
        memDc.LineTo(item.closeRect.right - inset, item.closeRect.bottom - inset);
        memDc.MoveTo(item.closeRect.right - inset - 1, item.closeRect.top + inset);
        memDc.LineTo(item.closeRect.left + inset - 1, item.closeRect.bottom - inset);
        memDc.SelectObject(oldPen);
    }

    ::SelectObject(memDc.GetSafeHdc(), oldFont);
    memDc.Detach();

    if (buffer) {
        ::BufferedPaintSetAlpha(buffer, &clientRect, 255);
        ::EndBufferedPaint(buffer, TRUE);
    }

    ::EndPaint(GetSafeHwnd(), &ps);
}

void BrowserTabBar::OnLButtonDown(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    const int closeIndex = hitTestClose(point);
    if (closeIndex >= 0) {
        if (m_closeRequested)
            m_closeRequested(closeIndex);
        return;
    }

    const int tabIndex = hitTestTab(point);
    if (tabIndex >= 0) {
        if (tabIndex != m_selectedIndex) {
            m_selectedIndex = tabIndex;
            Invalidate(FALSE);
            if (m_selectionChanged)
                m_selectionChanged(tabIndex);
        }
        return;
    }

    CWnd *parent = GetParent();
    if (parent && parent->GetSafeHwnd()) {
        ::ReleaseCapture();
        ::SendMessageW(parent->GetSafeHwnd(), WM_NCLBUTTONDOWN, HTCAPTION,
                       MAKELPARAM(screenPoint.x, screenPoint.y));
        return;
    }

    CWnd::OnLButtonDown(flags, point);
}

void BrowserTabBar::OnLButtonDblClk(UINT flags, CPoint point)
{
    if (hitTestClose(point) < 0 && hitTestTab(point) < 0) {
        CWnd *parent = GetParent();
        if (parent && parent->GetSafeHwnd()) {
            CPoint screenPoint(point);
            ClientToScreen(&screenPoint);
            ::SendMessageW(parent->GetSafeHwnd(), WM_NCLBUTTONDBLCLK, HTCAPTION,
                           MAKELPARAM(screenPoint.x, screenPoint.y));
            return;
        }
    }
    CWnd::OnLButtonDblClk(flags, point);
}

void BrowserTabBar::OnMButtonUp(UINT flags, CPoint point)
{
    const int tabIndex = hitTestTab(point);
    if (tabIndex >= 0 && m_closeRequested) {
        m_closeRequested(tabIndex);
        return;
    }
    CWnd::OnMButtonUp(flags, point);
}

void BrowserTabBar::OnMouseMove(UINT flags, CPoint point)
{
    ensureMouseTracking();

    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    const int parentHit = ParentResizeForwarder::hitTestParentFrame(this, screenPoint);
    if (parentHit) {
        ParentResizeForwarder::applyResizeCursor(parentHit);
        if (m_hoverTabIndex != -1 || m_hoverCloseIndex != -1) {
            const int oldTab = m_hoverTabIndex;
            const int oldClose = m_hoverCloseIndex;
            m_hoverTabIndex = -1;
            m_hoverCloseIndex = -1;
            invalidateTab(oldTab);
            invalidateTab(oldClose);
        }
        return;
    }

    const int closeIndex = hitTestClose(point);
    const int tabIndex = (closeIndex >= 0) ? closeIndex : hitTestTab(point);

    if (closeIndex != m_hoverCloseIndex || tabIndex != m_hoverTabIndex) {
        const int oldTab = m_hoverTabIndex;
        const int oldClose = m_hoverCloseIndex;
        m_hoverTabIndex = tabIndex;
        m_hoverCloseIndex = closeIndex;
        invalidateTab(oldTab);
        invalidateTab(oldClose);
        invalidateTab(tabIndex);
        invalidateTab(closeIndex);
    }

    CWnd::OnMouseMove(flags, point);
}

void BrowserTabBar::OnMouseLeave()
{
    m_trackingMouse = false;
    if (m_hoverTabIndex != -1 || m_hoverCloseIndex != -1) {
        const int oldTab = m_hoverTabIndex;
        const int oldClose = m_hoverCloseIndex;
        m_hoverTabIndex = -1;
        m_hoverCloseIndex = -1;
        invalidateTab(oldTab);
        invalidateTab(oldClose);
    }
}

void BrowserTabBar::OnSize(UINT type, int cx, int cy)
{
    CWnd::OnSize(type, cx, cy);
    recomputeLayout();
    Invalidate(FALSE);
}
