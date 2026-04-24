set(RDPBOX_TEST_SOURCES
    rdp/RdpSessionWidgetFocusTests.cpp
)

add_executable(RdpBoxTests
    ${RDPBOX_TEST_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpSessionWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/FreeRdpProcess.cpp
)

target_include_directories(RdpBoxTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(RdpBoxTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME RdpBoxTests COMMAND RdpBoxTests)
