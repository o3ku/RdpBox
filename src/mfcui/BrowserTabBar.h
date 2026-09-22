#pragma once

#include <afxcmn.h>
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
    using TooltipCallback = std::function<std::wstring(int tabIndex)>;
    using TabReorderedCallback = std::function<void(int fromIndex, int toIndex)>;

    enum class TabStatus { Inactive, ConnectedGood, ConnectedWarn, ConnectedBad };

    struct TabStatusInfo
    {
        TabStatus status = TabStatus::Inactive;
    };

    BrowserTabBar();
    ~BrowserTabBar() override;

    bool create(CWnd *parent, const CRect &rect, UINT id);

    int insertTab(const std::wstring &title);
    void removeTab(int index);
    void clearTabs();
    int tabCount() const;

    void setSelectedIndex(int index);
    int selectedIndex() const;

    void setSelectionChangedCallback(SelectionChangedCallback callback);
    void setCloseRequestedCallback(CloseRequestedCallback callback);
    void setTabReorderedCallback(TabReorderedCallback callback);

    void setTooltipCallback(TooltipCallback callback);

    void setTabStatus(int index, TabStatusInfo status);

    int hitTestTabAtScreenPoint(CPoint screenPoint) const;

    BOOL PreTranslateMessage(MSG *msg) override;
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC *dc);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnMButtonUp(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg void OnSize(UINT type, int cx, int cy);

    afx_msg void OnGetDispInfo(NMHDR *nmhdr, LRESULT *result);

    DECLARE_MESSAGE_MAP()

private:
    struct TabItem
    {
        std::wstring title;
        CRect rect;
        CRect closeRect;
        TabStatusInfo status;
    };

    void ensureFonts();
    void recomputeLayout();
    int hitTestTab(CPoint clientPoint) const;
    int hitTestClose(CPoint clientPoint) const;
    void invalidateTab(int index);
    void ensureMouseTracking();
    bool moveTab(int fromIndex, int toIndex);
    void updateDraggedTabPosition(CPoint point);

    std::vector<TabItem> m_tabs;
    int m_selectedIndex = -1;
    int m_hoverTabIndex = -1;
    int m_hoverCloseIndex = -1;
    bool m_trackingMouse = false;
    CToolTipCtrl m_tooltip;
    TooltipCallback m_tooltipCallback;
    CFont m_tabFontBold;
    int m_pressedTabIndex = -1;
    int m_draggingTabIndex = -1;
    int m_dropInsertIndex = -1;
    CPoint m_dragStartPoint{};
    bool m_leftButtonDown = false;

    SelectionChangedCallback m_selectionChanged;
    CloseRequestedCallback m_closeRequested;
    TabReorderedCallback m_tabReordered;
};
