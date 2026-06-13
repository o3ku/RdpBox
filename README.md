# RdpBox

## What It Is

RdpBox is a multi-tab Remote Desktop client built with Qt 5 and FreeRDP. It lets you manage multiple RDP sessions inside a single native desktop window instead of juggling many separate Remote Desktop windows.

## Major Features

- Multi-tab RDP session management: open, switch, close, and reconnect multiple remote desktop sessions in one app window.
- Connection management: create, edit, duplicate, delete, search, drag-reorder, and persist saved connections.
- Session restore and launch arguments: launch specific saved connections by name and restore currently open connections when restarting into a newly downloaded version.
- Built-in update flow: check GitHub for new releases in the background, download a new `RdpBox.exe`, and prompt the user to launch the new version.
- Remote desktop usability features: clipboard redirection, dynamic resolution updates, full-screen toggle, certificate confirmation, input forwarding, and handling for common system key combinations.

## Advantages

- Low resource usage: in typical use, RdpBox uses about one quarter of the resources of RDCMan.
- Single-file distribution: the shipped executable is a single file and is smaller than 5 MB.

## Supported Platforms

- Windows

## Building

The default CMake presets build the Qt-based `RdpBox.exe`. The old MFC shell is kept as an optional legacy target and can be enabled with `-DRDPBOX_BUILD_LEGACY=ON` when configuring CMake.
