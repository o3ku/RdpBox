#pragma once

#include <afxcmn.h>
#include <afxwin.h>

#include <functional>

class ClosableTabCtrl : public CTabCtrl
{
    DECLARE_DYNAMIC(ClosableTabCtrl)

public:
    using CloseCallback = std::function<void(int)>;

    ClosableTabCtrl();
    ~ClosableTabCtrl() override;

    void setCloseCallback(CloseCallback callback);

    void DrawItem(LPDRAWITEMSTRUCT drawItem) override;

protected:
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnMButtonUp(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg BOOL OnEraseBkgnd(CDC *dc);

    DECLARE_MESSAGE_MAP()

private:
    static CRect closeButtonRect(const CRect &itemRect);
    int hitTestCloseButton(CPoint point) const;
    int hitTestTab(CPoint point) const;
    void invalidateTab(int index);
    void updateHover(int newTabIndex, int newCloseIndex);

    CloseCallback m_closeCallback;
    int m_hoverCloseIndex = -1;
    int m_hoverTabIndex = -1;
    bool m_trackingMouse = false;
};
