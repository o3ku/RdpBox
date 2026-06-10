cmake_minimum_required(VERSION 3.24)

project(RdpBox VERSION 1.11.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(BUILD_TEST "Build test targets." OFF)

set(CMAKE_MFC_FLAG 2)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)

find_package(FreeRDP CONFIG REQUIRED)
find_package(FreeRDP-Client CONFIG REQUIRED)
find_package(WinPR CONFIG REQUIRED)

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

add_executable(RdpBox WIN32 ${RDPBOX_SOURCES})

target_compile_definitions(RdpBox PRIVATE
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

target_include_directories(RdpBox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

get_target_property(FREERDP_INCLUDE_DIRS freerdp INTERFACE_INCLUDE_DIRECTORIES)
foreach(_freerdp_include_dir IN LISTS FREERDP_INCLUDE_DIRS)
    if(EXISTS "${_freerdp_include_dir}/freerdp3")
        target_include_directories(RdpBox PRIVATE "${_freerdp_include_dir}/freerdp3")
    endif()
endforeach()

target_link_libraries(RdpBox PRIVATE
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
    set_target_properties(RdpBox PROPERTIES LINK_FLAGS "/ENTRY:WinMainCRTStartup")
endif()

if(BUILD_TEST)
    include(CTest)
    add_subdirectory(../tests tests)
endif()
