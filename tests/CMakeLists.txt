add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

add_executable(RdpResizeBurstTrackerTests
    rdp/RdpResizeBurstTrackerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpResizeBurstTracker.cpp
)

target_include_directories(RdpResizeBurstTrackerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpResizeBurstTrackerTests COMMAND RdpResizeBurstTrackerTests)

add_executable(RdpMouseMoveCoalescerTests
    rdp/RdpMouseMoveCoalescerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpMouseMoveCoalescer.cpp
)

target_include_directories(RdpMouseMoveCoalescerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpMouseMoveCoalescerTests COMMAND RdpMouseMoveCoalescerTests)

add_executable(RdpModifierSyncTrackerTests
    rdp/RdpModifierSyncTrackerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpModifierSyncTracker.cpp
)

target_include_directories(RdpModifierSyncTrackerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpModifierSyncTrackerTests COMMAND RdpModifierSyncTrackerTests)

add_executable(RdpCursorClassifierTests
    rdp/RdpCursorClassifierTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpCursorClassifier.cpp
)

target_include_directories(RdpCursorClassifierTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(RdpCursorClassifierTests PRIVATE
    User32
    Gdi32
)

add_test(NAME RdpCursorClassifierTests COMMAND RdpCursorClassifierTests)

add_executable(ProfileRepositoryTests
    profiles/ProfileRepositoryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/profiles/ProfileRepository.cpp
)

target_include_directories(ProfileRepositoryTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ProfileRepositoryTests PRIVATE
    cjson
    Ole32
)

add_test(NAME ProfileRepositoryTests COMMAND ProfileRepositoryTests)

add_executable(WindowFrameMetricsTests
    ui/WindowFrameMetricsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/WindowFrameMetrics.cpp
)

target_include_directories(WindowFrameMetricsTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME WindowFrameMetricsTests COMMAND WindowFrameMetricsTests)
