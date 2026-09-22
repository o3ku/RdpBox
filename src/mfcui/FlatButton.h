#pragma once

#include <afxwin.h>

class FlatButton : public CButton
{
    DECLARE_DYNAMIC(FlatButton)

public:
    FlatButton();
    ~FlatButton() override;

    void setDefault(bool isDefault);
    void DrawItem(LPDRAWITEMSTRUCT drawItem) override;

protected:
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();

    DECLARE_MESSAGE_MAP()

private:
    bool m_default = false;
    bool m_hover = false;
    bool m_trackingMouse = false;
};
