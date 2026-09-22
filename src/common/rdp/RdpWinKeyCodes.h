#pragma once

// Win32 key/message constants for code shared across platforms. On Windows
// they come from windows.h; elsewhere winpr/freerdp still speak VK values in
// a few shared state machines, so define the handful we reference.
#ifdef _WIN32
#include <windows.h>
#else
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87

#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_UNICHAR 0x0109
#define WM_IME_CHAR 0x0286
#define WM_MOUSEACTIVATE 0x0021
#define MA_ACTIVATE 1
#endif
