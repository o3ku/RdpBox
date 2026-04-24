set(RDPBOX_SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    rdp/FreeRdpProcess.h
    rdp/FreeRdpProcess.cpp
    rdp/PlatformClipboardBackend.h
    rdp/RdpClipboardBridge.h
    rdp/RdpClipboardBridge.cpp
    rdp/RdpSessionWidget.h
    rdp/RdpSessionWidget.cpp
    rdp/WindowsClipboardBackend.h
    rdp/WindowsClipboardBackend.cpp
    profiles/Profile.h
    profiles/ProfileRepository.h
    profiles/ProfileRepository.cpp
    ui/ProfileEditDialog.h
    ui/ProfileEditDialog.cpp
    ui/ConnectionListDialog.h
    ui/ConnectionListDialog.cpp
    session/SessionManager.h
    session/SessionManager.cpp
)

add_executable(RdpBox WIN32 ${RDPBOX_SOURCES})

target_include_directories(RdpBox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(FreeRDP CONFIG REQUIRED)
find_package(FreeRDP-Client CONFIG REQUIRED)
find_package(WinPR CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)

get_target_property(FREERDP_INCLUDE_DIRS freerdp INTERFACE_INCLUDE_DIRECTORIES)
foreach(_freerdp_include_dir IN LISTS FREERDP_INCLUDE_DIRS)
    if(EXISTS "${_freerdp_include_dir}/freerdp3")
        target_include_directories(RdpBox PRIVATE "${_freerdp_include_dir}/freerdp3")
    endif()
endforeach()

target_link_libraries(RdpBox PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    freerdp
    freerdp-client
    winpr
    cjson
    Ole32
    Shell32
)
