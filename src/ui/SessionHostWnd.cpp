#include "SessionHostWnd.h"

#include "ParentResizeForwarder.h"
#include "Win10Theme.h"

IMPLEMENT_DYNAMIC(SessionHostWnd, CWnd)

BEGIN_MESSAGE_MAP(SessionHostWnd, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

SessionHostWnd::SessionHostWnd() = default;

SessionHostWnd::~SessionHostWnd() = default;

bool SessionHostWnd::create(CWnd *parent, const CRect &rect, UINT id)
{
    static HBRUSH s_brush = ::CreateSolidBrush(RGB(17, 17, 17));
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  s_brush,
                                                  nullptr);
    return CreateEx(0, className, L"SessionHost",
                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    rect, parent, id) != FALSE;
}

BOOL SessionHostWnd::OnEraseBkgnd(CDC *dc)
{
    if (!dc)
        return FALSE;

    CRect rect;
    GetClientRect(&rect);
    dc->FillSolidRect(rect, RGB(17, 17, 17));
    return TRUE;
}

void SessionHostWnd::OnPaint()
{
    CPaintDC dc(this);
    CRect rect;
    GetClientRect(&rect);
    dc.FillSolidRect(rect, RGB(17, 17, 17));
}

void SessionHostWnd::OnMouseMove(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    const int hit = ParentResizeForwarder::hitTestParentFrame(this, screenPoint);
    if (hit) {
        ParentResizeForwarder::applyResizeCursor(hit);
        return;
    }

    CWnd::OnMouseMove(flags, point);
}

void SessionHostWnd::OnLButtonDown(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    CWnd::OnLButtonDown(flags, point);
}
