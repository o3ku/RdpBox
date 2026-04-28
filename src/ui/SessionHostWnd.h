#pragma once

#include <afxwin.h>

class SessionHostWnd : public CWnd
{
    DECLARE_DYNAMIC(SessionHostWnd)

public:
    SessionHostWnd();
    ~SessionHostWnd() override;

    bool create(CWnd *parent, const CRect &rect, UINT id);

protected:
    afx_msg BOOL OnEraseBkgnd(CDC *dc);
    afx_msg void OnPaint();
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);

    DECLARE_MESSAGE_MAP()
};
