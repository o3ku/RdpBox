#pragma once

#include <afxwin.h>

#include <functional>
#include <string>
#include <vector>

class BrowserTabBar : public CWnd
{
    DECLARE_DYNAMIC(BrowserTabBar)

public:
    using SelectionChangedCallback = std::function<void(int)>;
    using CloseRequestedCallback = std::function<void(int)>;

    BrowserTabBar();
    ~BrowserTabBar() override;

    bool create(CWnd *parent, const CRect &rect, UINT id);

    int insertTab(const std::wstring &title);
    void removeTab(int index);
    void clearTabs();
    int tabCount() const;

    void setSelectedIndex(int index);
    int selectedIndex() const;

    void setTabTitle(int index, const std::wstring &title);
    std::wstring tabTitle(int index) const;

    void setSelectionChangedCallback(SelectionChangedCallback callback);
    void setCloseRequestedCallback(CloseRequestedCallback callback);

    int hitTestTabAtScreenPoint(CPoint screenPoint) const;

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC *dc);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnMButtonUp(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg void OnSize(UINT type, int cx, int cy);

    DECLARE_MESSAGE_MAP()

private:
    struct TabItem
    {
        std::wstring title;
        CRect rect;
        CRect closeRect;
    };

    void recomputeLayout();
    int hitTestTab(CPoint clientPoint) const;
    int hitTestClose(CPoint clientPoint) const;
    void invalidateTab(int index);
    void ensureMouseTracking();

    std::vector<TabItem> m_tabs;
    int m_selectedIndex = -1;
    int m_hoverTabIndex = -1;
    int m_hoverCloseIndex = -1;
    bool m_trackingMouse = false;

    SelectionChangedCallback m_selectionChanged;
    CloseRequestedCallback m_closeRequested;
};
