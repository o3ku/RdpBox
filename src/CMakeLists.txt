cmake_minimum_required(VERSION 3.24)

project(RdpBox VERSION 1.3.0 LANGUAGES CXX RC)

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

set(RDPBOX_DISPLAY_VERSION "${PROJECT_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND)
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

set(RDPBOX_GITHUB_URL_VALUE "https://github.com/o3ku/RdpBox")

set(RDPBOX_SOURCES
    RdpBoxApp.h
    RdpBoxApp.cpp
    WinMain.cpp
    MainWindow.h
    MainWindow.cpp
    common/AppPaths.h
    common/AppPaths.cpp
    common/PasswordProtection.h
    common/PasswordProtection.cpp
    common/NativeTypes.h
    common/Win32String.h
    profiles/Profile.h
    profiles/ProfileRepository.h
    profiles/ProfileRepository.cpp
    rdp/FreeRdpProcess.h
    rdp/FreeRdpProcess.cpp
    rdp/RdpCursorClassifier.h
    rdp/RdpCursorClassifier.cpp
    rdp/RdpModifierSyncTracker.h
    rdp/RdpModifierSyncTracker.cpp
    rdp/RdpMouseMoveCoalescer.h
    rdp/RdpMouseMoveCoalescer.cpp
    rdp/RdpResizeBurstTracker.h
    rdp/RdpResizeBurstTracker.cpp
    rdp/FrameBufferAnalysis.h
    rdp/FrameBufferAnalysis.cpp
    rdp/RdpClipboardBridge.h
    rdp/RdpClipboardBridge.cpp
    rdp/PlatformClipboardBackend.h
    rdp/RdpSessionView.h
    rdp/RdpSessionView.cpp
    rdp/WindowsClipboardBackend.h
    rdp/WindowsClipboardBackend.cpp
    rdp/WindowsClipboardBackendNative.c
    session/SessionManager.h
    session/SessionManager.cpp
    ui/AboutDialog.h
    ui/AboutDialog.cpp
    ui/ProfileEditDialog.h
    ui/ProfileEditDialog.cpp
    ui/ConnectionListDialog.h
    ui/ConnectionListDialog.cpp
    ui/ClosableTabCtrl.h
    ui/ClosableTabCtrl.cpp
    ui/BrowserTabBar.h
    ui/BrowserTabBar.cpp
    ui/FlatButton.h
    ui/FlatButton.cpp
    ui/SessionHostWnd.h
    ui/SessionHostWnd.cpp
    ui/ResourceImage.h
    ui/ResourceImage.cpp
    ui/ParentResizeForwarder.h
    ui/ParentResizeForwarder.cpp
    ui/Win10Theme.h
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
    cjson
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
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        find_program(UPX_EXECUTABLE upx)
        if(UPX_EXECUTABLE)
            add_custom_command(TARGET RdpBox POST_BUILD
                COMMAND "${UPX_EXECUTABLE}" -9 "$<TARGET_FILE:RdpBox>"
                COMMENT "Compressing RdpBox.exe with UPX -9"
            )
        endif()
    endif()
endif()

if(BUILD_TEST)
    include(CTest)
    add_subdirectory(../tests tests)
endif()
