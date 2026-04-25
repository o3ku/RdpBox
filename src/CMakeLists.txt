cmake_minimum_required(VERSION 3.24)

project(RdpBox VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

option(BUILD_TEST "Build test targets." OFF)
option(RDPBOX_ENABLE_GRPC "Enable gRPC statistics polling when protobuf and gRPC are available." ON)

set(CMAKE_MFC_FLAG 2)

find_package(cJSON CONFIG REQUIRED)
find_package(FreeRDP CONFIG REQUIRED)
find_package(FreeRDP-Client CONFIG REQUIRED)
find_package(WinPR CONFIG REQUIRED)

set(QT_DEFAULT_MAJOR_VERSION 5)
set(RDPBOX_QT_COMPONENTS Core Gui)
if(BUILD_TEST)
    list(APPEND RDPBOX_QT_COMPONENTS Test)
endif()

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS ${RDPBOX_QT_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${RDPBOX_QT_COMPONENTS})

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_standard_project_setup()
endif()

set(RDPBOX_SOURCES
    RdpBoxApp.h
    RdpBoxApp.cpp
    qtmain_stub.cpp
    MainWindow.h
    MainWindow.cpp
    profiles/Profile.h
    profiles/ProfileRepository.h
    profiles/ProfileRepository.cpp
    rdp/FreeRdpProcess.h
    rdp/FreeRdpProcess.cpp
    rdp/RdpCursorClassifier.h
    rdp/RdpCursorClassifier.cpp
    rdp/RdpModifierSyncTracker.h
    rdp/RdpModifierSyncTracker.cpp
    rdp/RdpResizeBurstTracker.h
    rdp/RdpResizeBurstTracker.cpp
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
    ui/ProfileEditDialog.h
    ui/ProfileEditDialog.cpp
    ui/ConnectionListDialog.h
    ui/ConnectionListDialog.cpp
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
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    freerdp
    freerdp-client
    winpr
    cjson
    Ole32
    Shell32
    Comctl32
)

if(MSVC)
    set_target_properties(RdpBox PROPERTIES LINK_FLAGS "/ENTRY:WinMainCRTStartup /NODEFAULTLIB:qtmaind.lib /NODEFAULTLIB:qtmain.lib")
endif()

if(BUILD_TEST)
    include(CTest)
    add_subdirectory(../tests tests)
endif()

