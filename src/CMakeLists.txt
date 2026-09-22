cmake_minimum_required(VERSION 3.24)

project(RdpBox VERSION 2.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(BUILD_TEST "Build test targets." OFF)
option(RDPBOX_BUILD_LEGACY "Build the legacy MFC UI target." OFF)
option(RDPBOX_BUILD_QT "Build the Qt UI target." OFF)

if(BUILD_TEST OR RDPBOX_BUILD_LEGACY)
    set(CMAKE_MFC_FLAG 2)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)

find_package(FreeRDP CONFIG REQUIRED)
find_package(FreeRDP-Client CONFIG REQUIRED)
find_package(WinPR CONFIG REQUIRED)

get_target_property(FREERDP_INCLUDE_DIRS freerdp INTERFACE_INCLUDE_DIRECTORIES)

set(RDPBOX_VERSION_OVERRIDE "" CACHE STRING "Override the RdpBox display/update version.")

set(RDPBOX_DISPLAY_VERSION "${PROJECT_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND AND RDPBOX_VERSION_OVERRIDE STREQUAL "")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE RDPBOX_GIT_TAG_RESULT
        OUTPUT_VARIABLE RDPBOX_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(RDPBOX_GIT_TAG_RESULT EQUAL 0 AND NOT RDPBOX_GIT_TAG STREQUAL "")
        set(RDPBOX_DISPLAY_VERSION "${RDPBOX_GIT_TAG}")
    endif()

endif()

if(NOT RDPBOX_VERSION_OVERRIDE STREQUAL "")
    set(RDPBOX_DISPLAY_VERSION "${RDPBOX_VERSION_OVERRIDE}")
endif()

set(RDPBOX_GITHUB_URL_VALUE "https://github.com/o3ku/RdpBox")

set(RDPBOX_QT_SOURCES
    qtui/FramelessWin.h
    qtui/QtMain.cpp
    qtui/QtMainWindow.h
    qtui/QtMainWindow.cpp
    qtui/QtRdpSessionWidget.h
    qtui/QtRdpSessionWidget.cpp
    qtui/QtWindowChromeBehavior.h
    qtui/QtWindowChromeBehavior.cpp
    qtui/QtProfileDialog.h
    qtui/QtProfileDialog.cpp
    qtui/QtShortcutSettings.h
    qtui/QtShortcutSettings.cpp
    qtui/QtShortcutsDialog.h
    qtui/QtShortcutsDialog.cpp
    resources/qt_resources.qrc
    common/AppPaths.h
    common/AppPaths.cpp
    common/ConnectionLaunchArgs.h
    common/ConnectionLaunchArgs.cpp
    common/PasswordProtection.h
    common/PasswordProtection.cpp
    common/UpdateClient.h
    common/UpdateClient.cpp
    common/profiles/Profile.h
    common/profiles/ProfileRepository.h
    common/profiles/ProfileRepository.cpp
    common/rdp/ClipboardWindowReady.h
    common/rdp/FrameBufferMemory.h
    common/rdp/FreeRdpProcess.h
    common/rdp/FreeRdpProcess.cpp
    common/rdp/FreeRdpProcessNative.h
    common/rdp/FreeRdpProcessNative.cpp
    common/rdp/PlatformClipboardBackend.h
    common/rdp/RdpClipboardBridge.h
    common/rdp/RdpClipboardBridge.cpp
    common/rdp/RdpCertificatePromptBehavior.h
    common/rdp/RdpCertificatePromptBehavior.cpp
    common/rdp/RdpCursorClassifier.h
    common/rdp/RdpCursorClassifier.cpp
    common/rdp/RdpInputEventUtil.h
    common/rdp/RdpInputEventUtil.cpp
    common/rdp/RdpInputModifiers.h
    common/rdp/RdpInputModifiers.cpp
    common/rdp/RdpKeyboardInputRouter.h
    common/rdp/RdpKeyboardInputRouter.cpp
    common/rdp/RdpMouseMoveCoalescer.h
    common/rdp/RdpMouseMoveCoalescer.cpp
    common/rdp/RdpModifierSyncTracker.h
    common/rdp/RdpModifierSyncTracker.cpp
    common/rdp/RdpReconnectInteraction.h
    common/rdp/RdpReservedShortcutTracker.h
    common/rdp/RdpReservedShortcutTracker.cpp
    common/rdp/RdpSessionKeyboardState.h
    common/rdp/RdpSessionKeyboardState.cpp
    common/rdp/RdpSessionMouseState.h
    common/rdp/RdpSessionMouseState.cpp
    common/rdp/RdpResizeBurstTracker.h
    common/rdp/RdpResizeBurstTracker.cpp
    common/rdp/RdpResolutionRecovery.h
    common/rdp/RdpResolutionRecovery.cpp
    common/rdp/RdpSessionViewBehavior.h
    common/rdp/RdpSessionViewBehavior.cpp
    common/rdp/WindowsClipboardBackend.h
    common/rdp/WindowsClipboardBackend.cpp
    common/rdp/WindowsClipboardBackendDataObject.cpp
    common/rdp/WindowsClipboardBackendInternal.h
    common/rdp/WindowsClipboardBackendWindow.cpp
    common/rdp/WindowsClipboardBehavior.h
    common/rdp/WindowsClipboardBehavior.cpp
    common/ui/ConnectionListBehavior.h
    common/ui/ConnectionListBehavior.cpp
    common/ui/ProfileEditBehavior.h
    common/ui/ProfileEditBehavior.cpp
    common/ui/MainWindowTabBehavior.h
    common/ui/MainWindowTabBehavior.cpp
    common/ui/MainWindowSessionBehavior.h
    common/ui/MainWindowSessionBehavior.cpp
    common/ui/MainWindowUpdateBehavior.h
    common/ui/MainWindowUpdateBehavior.cpp
    common/ui/WindowStateScaling.h
    common/ui/WindowStateScaling.cpp
    resources/resource.h
    resources/app_icon.rc
)

set(RDPBOX_SOURCES
    mfcui/RdpBoxApp.h
    mfcui/RdpBoxApp.cpp
    mfcui/WinMain.cpp
    mfcui/MainWindow.h
    mfcui/MainWindow.cpp
    mfcui/MainWindowChrome.cpp
    mfcui/MainWindowSessions.cpp
    mfcui/MainWindowUpdate.cpp
    common/AppPaths.h
    common/AppPaths.cpp
    common/ConnectionLaunchArgs.h
    common/ConnectionLaunchArgs.cpp
    common/PasswordProtection.h
    common/PasswordProtection.cpp
    common/UpdateClient.h
    common/UpdateClient.cpp
    common/NativeTypes.h
    common/Win32String.h
    common/profiles/Profile.h
    common/profiles/ProfileRepository.h
    common/profiles/ProfileRepository.cpp
    common/rdp/FreeRdpProcess.h
    common/rdp/FreeRdpProcess.cpp
    common/rdp/FreeRdpProcessNative.h
    common/rdp/FreeRdpProcessNative.cpp
    common/rdp/RdpCursorClassifier.h
    common/rdp/RdpCursorClassifier.cpp
    common/rdp/RdpFocusNotification.h
    common/rdp/RdpFocusNotification.cpp
    common/rdp/RdpInputEventUtil.h
    common/rdp/RdpInputEventUtil.cpp
    common/rdp/RdpInputModifiers.h
    common/rdp/RdpInputModifiers.cpp
    common/rdp/RdpKeyboardInputRouter.h
    common/rdp/RdpKeyboardInputRouter.cpp
    common/rdp/RdpReconnectInteraction.h
    common/rdp/RdpResolutionRecovery.h
    common/rdp/RdpResolutionRecovery.cpp
    common/rdp/RdpSessionViewBehavior.h
    common/rdp/RdpSessionViewBehavior.cpp
    common/rdp/RdpReservedShortcutTracker.h
    common/rdp/RdpReservedShortcutTracker.cpp
    common/rdp/RdpModifierSyncTracker.h
    common/rdp/RdpModifierSyncTracker.cpp
    common/rdp/RdpMouseMoveCoalescer.h
    common/rdp/RdpMouseMoveCoalescer.cpp
    common/rdp/RdpProcessEventBehavior.h
    common/rdp/RdpProcessEventBehavior.cpp
    common/rdp/RdpProcessEventQueueBehavior.h
    common/rdp/RdpProcessEventQueueBehavior.cpp
    common/rdp/RdpResumeRecovery.h
    common/rdp/RdpResumeRecovery.cpp
    common/rdp/RdpResizeBurstTracker.h
    common/rdp/RdpResizeBurstTracker.cpp
    common/rdp/RdpSessionKeyboardState.h
    common/rdp/RdpSessionKeyboardState.cpp
    common/rdp/RdpSessionMouseState.h
    common/rdp/RdpSessionMouseState.cpp
    common/rdp/FrameBufferMemory.h
    common/rdp/RdpClipboardBridge.h
    common/rdp/RdpClipboardBridge.cpp
    common/rdp/RdpCertificatePromptBehavior.h
    common/rdp/RdpCertificatePromptBehavior.cpp
    common/rdp/ClipboardWindowReady.h
    common/rdp/PlatformClipboardBackend.h
    common/rdp/RdpSessionView.h
    common/rdp/RdpSessionView.cpp
    common/rdp/RdpSessionViewInput.cpp
    common/rdp/RdpSessionKeyboardHook.h
    common/rdp/RdpSessionKeyboardHook.cpp
    common/rdp/RdpSessionViewMessages.cpp
    common/rdp/RdpSessionViewRendering.cpp
    common/rdp/WindowsClipboardBackend.h
    common/rdp/WindowsClipboardBehavior.h
    common/rdp/WindowsClipboardBehavior.cpp
    common/rdp/WindowsClipboardBackendInternal.h
    common/rdp/WindowsClipboardBackendDataObject.cpp
    common/rdp/WindowsClipboardBackendWindow.cpp
    common/rdp/WindowsClipboardBackend.cpp
    mfcui/session/SessionManager.h
    mfcui/session/SessionManagerInterfaces.h
    mfcui/session/SessionManager.cpp
    mfcui/session/SessionManagerMfc.cpp
    mfcui/session/SessionCollectionBehavior.h
    mfcui/session/SessionCollectionBehavior.cpp
    mfcui/session/SessionTabBehavior.h
    mfcui/session/SessionTabBehavior.cpp
    common/session/SessionResumePolicy.h
    mfcui/AboutDialog.h
    mfcui/AboutDialog.cpp
    mfcui/MainWindowShortcuts.h
    mfcui/MainWindowShortcuts.cpp
    mfcui/ShortcutsDialog.h
    mfcui/ShortcutsDialog.cpp
    mfcui/BrowserTabBehavior.h
    mfcui/BrowserTabBehavior.cpp
    common/ui/ConnectionListBehavior.h
    common/ui/ConnectionListBehavior.cpp
    mfcui/ProfileEditDialog.h
    mfcui/ProfileEditDialog.cpp
    common/ui/ProfileEditBehavior.h
    common/ui/ProfileEditBehavior.cpp
    mfcui/ConnectionListDialog.h
    mfcui/ConnectionListDialog.cpp
    mfcui/BrowserTabBar.h
    mfcui/BrowserTabBar.cpp
    common/ui/MainWindowTabBehavior.h
    common/ui/MainWindowTabBehavior.cpp
    common/ui/MainWindowSessionBehavior.h
    common/ui/MainWindowSessionBehavior.cpp
    common/ui/MainWindowUpdateBehavior.h
    common/ui/MainWindowUpdateBehavior.cpp
    mfcui/MainWindowLayoutBehavior.h
    mfcui/MainWindowLayoutBehavior.cpp
    mfcui/FlatButton.h
    mfcui/FlatButton.cpp
    mfcui/SessionHostWnd.h
    mfcui/SessionHostWnd.cpp
    mfcui/ParentResizeForwarder.h
    mfcui/ParentResizeForwarder.cpp
    mfcui/Win10Theme.h
    common/ui/WindowStateScaling.h
    common/ui/WindowStateScaling.cpp
    mfcui/WindowFrameMetrics.h
    mfcui/WindowFrameMetrics.cpp
    resources/resource.h
    resources/mfc_ui.rc
    resources/app_icon.rc
)

if(RDPBOX_BUILD_QT)
    find_package(Qt5 5.15 CONFIG REQUIRED COMPONENTS Widgets)

    add_executable(RdpBox WIN32 ${RDPBOX_QT_SOURCES})

    set_target_properties(RdpBox PROPERTIES
        AUTOMOC ON
        AUTORCC ON
    )

    target_compile_definitions(RdpBox PRIVATE
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
        RDPBOX_VERSION=L"${RDPBOX_DISPLAY_VERSION}"
        RDPBOX_GITHUB_URL=L"${RDPBOX_GITHUB_URL_VALUE}"
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    target_include_directories(RdpBox PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    foreach(_freerdp_include_dir IN LISTS FREERDP_INCLUDE_DIRS)
        if(EXISTS "${_freerdp_include_dir}/freerdp3")
            target_include_directories(RdpBox PRIVATE "${_freerdp_include_dir}/freerdp3")
        endif()
    endforeach()

    target_link_libraries(RdpBox PRIVATE
        Qt5::Widgets
        freerdp
        freerdp-client
        winpr
        nlohmann_json::nlohmann_json
        cjson

        Bcrypt
        Crypt32
        Imm32
        Ole32
        Shell32
        User32
        Gdi32
    )

    if(TARGET Qt5::WinMain)
        target_link_libraries(RdpBox PRIVATE Qt5::WinMain)
    endif()
endif()

if(RDPBOX_BUILD_LEGACY)
    add_executable(RdpBoxLegacy WIN32 ${RDPBOX_SOURCES})

    target_compile_definitions(RdpBoxLegacy PRIVATE
        _AFXDLL
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
        RDPBOX_VERSION=L"${RDPBOX_DISPLAY_VERSION}"
        RDPBOX_GITHUB_URL=L"${RDPBOX_GITHUB_URL_VALUE}"
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    target_include_directories(RdpBoxLegacy PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    foreach(_freerdp_include_dir IN LISTS FREERDP_INCLUDE_DIRS)
        if(EXISTS "${_freerdp_include_dir}/freerdp3")
            target_include_directories(RdpBoxLegacy PRIVATE "${_freerdp_include_dir}/freerdp3")
        endif()
    endforeach()

    target_link_libraries(RdpBoxLegacy PRIVATE
        freerdp
        freerdp-client
        winpr
        nlohmann_json::nlohmann_json

        Bcrypt
        Crypt32
        Imm32
        Ole32
        Shell32
        Comctl32
        Dwmapi
        Msimg32
        UxTheme
        Gdiplus
    )

    if(MSVC)
        set_target_properties(RdpBoxLegacy PROPERTIES
            OUTPUT_NAME "RdpBox"
            LINK_FLAGS "/ENTRY:WinMainCRTStartup"
        )
    endif()
endif()

if(BUILD_TEST)
    include(CTest)
    add_subdirectory(../tests tests)
endif()
