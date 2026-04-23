set(RDPBOX_SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    rdp/FreeRdpProcess.h
    rdp/FreeRdpProcess.cpp
    rdp/RdpSessionWidget.h
    rdp/RdpSessionWidget.cpp
)

add_executable(RdpBox WIN32 ${RDPBOX_SOURCES})

target_include_directories(RdpBox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(RdpBox PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
