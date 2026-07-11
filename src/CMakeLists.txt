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

set(RDPBOX_QWINDOWKIT "AUTO" CACHE STRING "QWindowKit integration: AUTO, ON, or OFF.")
set_property(CACHE RDPBOX_QWINDOWKIT PROPERTY STRINGS AUTO ON OFF)
set(RDPBOX_QWINDOWKIT_ALLOWED_VALUES AUTO ON OFF)
string(TOUPPER "${RDPBOX_QWINDOWKIT}" RDPBOX_QWINDOWKIT_MODE)
if(NOT RDPBOX_QWINDOWKIT_MODE IN_LIST RDPBOX_QWINDOWKIT_ALLOWED_VALUES)
    message(FATAL_ERROR "RDPBOX_QWINDOWKIT must be AUTO, ON, or OFF.")
endif()
if(NOT RDPBOX_QWINDOWKIT_MODE STREQUAL "OFF")
    find_package(QWindowKit CONFIG QUIET COMPONENTS Widgets)
    if(RDPBOX_QWINDOWKIT_MODE STREQUAL "ON" AND NOT TARGET QWindowKit::Widgets)
        message(FATAL_ERROR "RDPBOX_QWINDOWKIT=ON requires QWindowKit::Widgets. Set QWindowKit_DIR or use RDPBOX_QWINDOWKIT=AUTO/OFF.")
    endif()
endif()

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
    qt/QtMain.cpp
    qt/QtMainWindow.h
    qt/QtMainWindow.cpp
    qt/QtRdpSessionWidget.h
    qt/QtRdpSessionWidget.cpp
    qt/QtWindowChromeBehavior.h
    qt/QtWindowChromeBehavior.cpp
    qt/QtProfileDialog.h
    qt/QtProfileDialog.cpp
    resources/qt_resources.qrc
    common/AppPaths.h
    common/AppPaths.cpp
    common/ConnectionLaunchArgs.h
    common/ConnectionLaunchArgs.cpp
    common/PasswordProtection.h
    common/PasswordProtection.cpp
    common/UpdateClient.h
    common/UpdateClient.cpp
    profiles/Profile.h
    profiles/ProfileRepository.h
    profiles/ProfileRepository.cpp
    rdp/ClipboardWindowReady.h
    rdp/FrameBufferMemory.h
    rdp/FreeRdpProcess.h
    rdp/FreeRdpProcess.cpp
    rdp/FreeRdpProcessNative.h
    rdp/FreeRdpProcessNative.cpp
    rdp/PlatformClipboardBackend.h
    rdp/RdpClipboardBridge.h
    rdp/RdpClipboardBridge.cpp
    rdp/RdpCertificatePromptBehavior.h
    rdp/RdpCertificatePromptBehavior.cpp
    rdp/RdpCursorClassifier.h
    rdp/RdpCursorClassifier.cpp
    rdp/RdpInputEventUtil.h
    rdp/RdpInputEventUtil.cpp
    rdp/RdpInputModifiers.h
    rdp/RdpInputModifiers.cpp
    rdp/RdpKeyboardInputRouter.h
    rdp/RdpKeyboardInputRouter.cpp
    rdp/RdpMouseMoveCoalescer.h
    rdp/RdpMouseMoveCoalescer.cpp
    rdp/RdpModifierSyncTracker.h
    rdp/RdpModifierSyncTracker.cpp
    rdp/RdpReconnectInteraction.h
    rdp/RdpReservedShortcutTracker.h
    rdp/RdpReservedShortcutTracker.cpp
    rdp/RdpSessionKeyboardState.h
    rdp/RdpSessionKeyboardState.cpp
    rdp/RdpSessionMouseState.h
    rdp/RdpSessionMouseState.cpp
    rdp/RdpResizeBurstTracker.h
    rdp/RdpResizeBurstTracker.cpp
    rdp/RdpResolutionRecovery.h
    rdp/RdpResolutionRecovery.cpp
    rdp/RdpSessionViewBehavior.h
    rdp/RdpSessionViewBehavior.cpp
    rdp/WindowsClipboardBackend.h
    rdp/WindowsClipboardBackend.cpp
    rdp/WindowsClipboardBackendDataObject.cpp
    rdp/WindowsClipboardBackendInternal.h
    rdp/WindowsClipboardBackendWindow.cpp
    rdp/WindowsClipboardBehavior.h
    rdp/WindowsClipboardBehavior.cpp
    ui/ConnectionListBehavior.h
    ui/ConnectionListBehavior.cpp
    ui/ProfileEditBehavior.h
    ui/ProfileEditBehavior.cpp
    ui/MainWindowTabBehavior.h
    ui/MainWindowTabBehavior.cpp
    ui/MainWindowSessionBehavior.h
    ui/MainWindowSessionBehavior.cpp
    ui/MainWindowUpdateBehavior.h
    ui/MainWindowUpdateBehavior.cpp
    ui/WindowStateScaling.h
    ui/WindowStateScaling.cpp
    resources/resource.h
    resources/app_icon.rc
)

set(RDPBOX_SOURCES
    RdpBoxApp.h
    RdpBoxApp.cpp
    WinMain.cpp
    MainWindow.h
    MainWindow.cpp
    MainWindowChrome.cpp
    MainWindowSessions.cpp
    MainWindowUpdate.cpp
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
    profiles/Profile.h
    profiles/ProfileRepository.h
    profiles/ProfileRepository.cpp
    rdp/FreeRdpProcess.h
    rdp/FreeRdpProcess.cpp
    rdp/FreeRdpProcessNative.h
    rdp/FreeRdpProcessNative.cpp
    rdp/RdpCursorClassifier.h
    rdp/RdpCursorClassifier.cpp
    rdp/RdpFocusNotification.h
    rdp/RdpFocusNotification.cpp
    rdp/RdpSystemChordTrace.h
    rdp/RdpSystemChordTrace.cpp
    rdp/RdpInputEventUtil.h
    rdp/RdpInputEventUtil.cpp
    rdp/RdpInputModifiers.h
    rdp/RdpInputModifiers.cpp
    rdp/RdpKeyboardInputRouter.h
    rdp/RdpKeyboardInputRouter.cpp
    rdp/RdpReconnectInteraction.h
    rdp/RdpResolutionRecovery.h
    rdp/RdpResolutionRecovery.cpp
    rdp/RdpSessionViewBehavior.h
    rdp/RdpSessionViewBehavior.cpp
    rdp/RdpReservedShortcutTracker.h
    rdp/RdpReservedShortcutTracker.cpp
    rdp/RdpModifierSyncTracker.h
    rdp/RdpModifierSyncTracker.cpp
    rdp/RdpMouseMoveCoalescer.h
    rdp/RdpMouseMoveCoalescer.cpp
    rdp/RdpProcessEventBehavior.h
    rdp/RdpProcessEventBehavior.cpp
    rdp/RdpProcessEventQueueBehavior.h
    rdp/RdpProcessEventQueueBehavior.cpp
    rdp/RdpResumeRecovery.h
    rdp/RdpResumeRecovery.cpp
    rdp/RdpResizeBurstTracker.h
    rdp/RdpResizeBurstTracker.cpp
    rdp/RdpSessionKeyboardState.h
    rdp/RdpSessionKeyboardState.cpp
    rdp/RdpSessionMouseState.h
    rdp/RdpSessionMouseState.cpp
    rdp/FrameBufferMemory.h
    rdp/RdpClipboardBridge.h
    rdp/RdpClipboardBridge.cpp
    rdp/RdpCertificatePromptBehavior.h
    rdp/RdpCertificatePromptBehavior.cpp
    rdp/ClipboardWindowReady.h
    rdp/PlatformClipboardBackend.h
    rdp/RdpSessionView.h
    rdp/RdpSessionView.cpp
    rdp/RdpSessionViewInput.cpp
    rdp/RdpSessionKeyboardHook.h
    rdp/RdpSessionKeyboardHook.cpp
    rdp/RdpSessionViewMessages.cpp
    rdp/RdpSessionViewRendering.cpp
    rdp/WindowsClipboardBackend.h
    rdp/WindowsClipboardBehavior.h
    rdp/WindowsClipboardBehavior.cpp
    rdp/WindowsClipboardBackendInternal.h
    rdp/WindowsClipboardBackendDataObject.cpp
    rdp/WindowsClipboardBackendWindow.cpp
    rdp/WindowsClipboardBackend.cpp
    session/SessionManager.h
    session/SessionManagerInterfaces.h
    session/SessionManager.cpp
    session/SessionManagerMfc.cpp
    session/SessionCollectionBehavior.h
    session/SessionCollectionBehavior.cpp
    session/SessionTabBehavior.h
    session/SessionTabBehavior.cpp
    session/SessionResumePolicy.h
    ui/AboutDialog.h
    ui/AboutDialog.cpp
    ui/BrowserTabBehavior.h
    ui/BrowserTabBehavior.cpp
    ui/ConnectionListBehavior.h
    ui/ConnectionListBehavior.cpp
    ui/ProfileEditDialog.h
    ui/ProfileEditDialog.cpp
    ui/ProfileEditBehavior.h
    ui/ProfileEditBehavior.cpp
    ui/ConnectionListDialog.h
    ui/ConnectionListDialog.cpp
    ui/BrowserTabBar.h
    ui/BrowserTabBar.cpp
    ui/MainWindowTabBehavior.h
    ui/MainWindowTabBehavior.cpp
    ui/MainWindowSessionBehavior.h
    ui/MainWindowSessionBehavior.cpp
    ui/MainWindowUpdateBehavior.h
    ui/MainWindowUpdateBehavior.cpp
    ui/MainWindowLayoutBehavior.h
    ui/MainWindowLayoutBehavior.cpp
    ui/FlatButton.h
    ui/FlatButton.cpp
    ui/SessionHostWnd.h
    ui/SessionHostWnd.cpp
    ui/ParentResizeForwarder.h
    ui/ParentResizeForwarder.cpp
    ui/Win10Theme.h
    ui/WindowStateScaling.h
    ui/WindowStateScaling.cpp
    ui/WindowFrameMetrics.h
    ui/WindowFrameMetrics.cpp
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

    if(TARGET QWindowKit::Widgets)
        target_compile_definitions(RdpBox PRIVATE RDPBOX_USE_QWINDOWKIT)
        target_link_libraries(RdpBox PRIVATE QWindowKit::Widgets)
        message(STATUS "RdpBox Qt chrome: QWindowKit::Widgets")
    else()
        message(STATUS "RdpBox Qt chrome: native Win32 hit-test fallback")
    endif()

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
