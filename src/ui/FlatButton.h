#pragma once

#include <afxwin.h>

#include <memory>

class ResourceImage;

class FlatButton : public CButton
{
    DECLARE_DYNAMIC(FlatButton)

public:
    enum class IconKind
    {
        None,
        Plus,
        List
    };

    FlatButton();
    ~FlatButton() override;

    void setDefault(bool isDefault);
    void setToolbarStyle(bool enabled);
    void setIconKind(IconKind kind);
    bool setIconResource(UINT resourceId, LPCWSTR resourceType = L"PNG");
    void DrawItem(LPDRAWITEMSTRUCT drawItem) override;

protected:
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();

    DECLARE_MESSAGE_MAP()

private:
    void drawIcon(CDC &dc, const CRect &rect, COLORREF color) const;
    void drawIconImage(CDC &dc, const CRect &rect) const;

    IconKind m_iconKind = IconKind::None;
    std::unique_ptr<ResourceImage> m_iconImage;
    bool m_default = false;
    bool m_toolbarStyle = false;
    bool m_hover = false;
    bool m_trackingMouse = false;
};
