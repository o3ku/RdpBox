#include "WindowsClipboardBackendInternal.h"

#include <atomic>

namespace
{
constexpr UINT WM_CLIPRDR_MESSAGE = WM_USER + 156;
constexpr WPARAM OLE_SETCLIPBOARD = 1;

void clearResponseData(ClipboardContext *clipboard)
{
    if (!clipboard || !clipboard->responseData)
        return;

    GlobalFree(clipboard->responseData);
    clipboard->responseData = nullptr;
}

bool shouldSyncClipboard(const ClipboardContext *clipboard)
{
    return clipboard && clipboard->sync &&
           GetClipboardOwner() != clipboard->hwnd &&
           OleIsCurrentClipboard(clipboard->dataObject) == S_FALSE;
}

LRESULT handleRenderFormat(ClipboardContext *clipboard, WPARAM format)
{
    if (!clipboard || clipboardSendDataRequest(clipboard, static_cast<UINT32>(format)) != CHANNEL_RC_OK)
        return 0;

    if (!SetClipboardData(static_cast<UINT>(format), clipboard->responseData) && clipboard->responseData)
        clearResponseData(clipboard);
    return 0;
}

LRESULT CALLBACK clipboardWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *clipboard = reinterpret_cast<ClipboardContext*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        auto *create = reinterpret_cast<CREATESTRUCT*>(lParam);
        clipboard = static_cast<ClipboardContext*>(create->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(clipboard));
        clipboard->hwnd = hwnd;
        if (!clipboard->legacyApi)
            clipboard->addClipboardFormatListener(hwnd);
        else
            clipboard->nextViewer = SetClipboardViewer(hwnd);
        return 0;
    }
    case WM_CLOSE:
        if (clipboard && !clipboard->legacyApi)
            clipboard->removeClipboardFormatListener(hwnd);
        return 0;
    case WM_DESTROY:
        if (clipboard && clipboard->legacyApi)
            ChangeClipboardChain(hwnd, clipboard->nextViewer);
        return 0;
    case WM_CLIPBOARDUPDATE:
        if (shouldSyncClipboard(clipboard)) {
            clearResponseData(clipboard);
            clipboardSendFormatList(clipboard);
        }
        return 0;
    case WM_RENDERALLFORMATS:
        if (clipboard && clipboardTryOpenClipboard(clipboard->hwnd)) {
            EmptyClipboard();
            CloseClipboard();
        }
        return 0;
    case WM_RENDERFORMAT:
        return handleRenderFormat(clipboard, wParam);
    case WM_DRAWCLIPBOARD:
        if (clipboard && clipboard->legacyApi) {
            if (shouldSyncClipboard(clipboard))
                clipboardSendFormatList(clipboard);
            SendMessage(clipboard->nextViewer, message, wParam, lParam);
        }
        return 0;
    case WM_CHANGECBCHAIN:
        if (clipboard && clipboard->legacyApi) {
            if (reinterpret_cast<HWND>(wParam) == clipboard->nextViewer)
                clipboard->nextViewer = reinterpret_cast<HWND>(lParam);
            else if (clipboard->nextViewer)
                SendMessage(clipboard->nextViewer, message, wParam, lParam);
        }
        return 0;
    case WM_CLIPRDR_MESSAGE:
        if (clipboard && wParam == OLE_SETCLIPBOARD) {
            destroyFileDataObject(clipboard->dataObject);
            clipboard->dataObject = nullptr;
            if (createFileDataObject(clipboard, &clipboard->dataObject))
                OleSetClipboard(clipboard->dataObject);
        }
        return 0;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

int createClipboardWindow(ClipboardContext *clipboard)
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = clipboardWindowProc;
    windowClass.hInstance = GetModuleHandle(nullptr);
    windowClass.lpszClassName = L"RdpBoxClipboardHiddenWindow";
    RegisterClassExW(&windowClass);

    clipboard->hwnd = CreateWindowExW(WS_EX_LEFT, windowClass.lpszClassName, L"rdpclip", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), clipboard);
    return clipboard->hwnd ? 0 : -1;
}

}

DWORD WINAPI clipboardThreadProc(LPVOID argument)
{
    auto *clipboard = static_cast<ClipboardContext*>(argument);
    OleInitialize(nullptr);

    if (createClipboardWindow(clipboard) != 0) {
        if (clipboard && clipboard->readyEvent)
            SetEvent(clipboard->readyEvent);
        OleUninitialize();
        return 0;
    }

    clipboard->windowReady.store(true, std::memory_order_release);
    if (clipboard->readyEvent)
        SetEvent(clipboard->readyEvent);

    MSG message = {};
    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    OleUninitialize();
    return 0;
}
