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

add_executable(RdpFocusNotificationTests
    rdp/RdpFocusNotificationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpFocusNotification.cpp
)

target_include_directories(RdpFocusNotificationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpFocusNotificationTests COMMAND RdpFocusNotificationTests)

add_executable(ClipboardWindowReadyTests
    rdp/ClipboardWindowReadyTests.cpp
)

target_include_directories(ClipboardWindowReadyTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME ClipboardWindowReadyTests COMMAND ClipboardWindowReadyTests)

add_executable(RdpInputModifiersTests
    rdp/RdpInputModifiersTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpInputModifiers.cpp
)

target_include_directories(RdpInputModifiersTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpInputModifiersTests COMMAND RdpInputModifiersTests)

add_executable(RdpInputEventUtilTests
    rdp/RdpInputEventUtilTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpInputEventUtil.cpp
)

target_include_directories(RdpInputEventUtilTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpInputEventUtilTests COMMAND RdpInputEventUtilTests)

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

add_executable(RdpResumeRecoveryTests
    rdp/RdpResumeRecoveryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpResumeRecovery.cpp
)

target_include_directories(RdpResumeRecoveryTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpResumeRecoveryTests COMMAND RdpResumeRecoveryTests)

add_executable(RdpReconnectInteractionTests
    rdp/RdpReconnectInteractionTests.cpp
)

target_include_directories(RdpReconnectInteractionTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpReconnectInteractionTests COMMAND RdpReconnectInteractionTests)

add_executable(RdpResolutionRecoveryTests
    rdp/RdpResolutionRecoveryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpResolutionRecovery.cpp
)

target_include_directories(RdpResolutionRecoveryTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpResolutionRecoveryTests COMMAND RdpResolutionRecoveryTests)

add_executable(RdpReservedShortcutTrackerTests
    rdp/RdpReservedShortcutTrackerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpReservedShortcutTracker.cpp
)

target_include_directories(RdpReservedShortcutTrackerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpReservedShortcutTrackerTests COMMAND RdpReservedShortcutTrackerTests)

add_executable(SessionResumePolicyTests
    session/SessionResumePolicyTests.cpp
)

target_include_directories(SessionResumePolicyTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME SessionResumePolicyTests COMMAND SessionResumePolicyTests)

add_executable(ProfileRepositoryTests
    profiles/ProfileRepositoryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/common/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/common/PasswordProtection.cpp
    ${CMAKE_SOURCE_DIR}/src/profiles/ProfileRepository.cpp
)

target_include_directories(ProfileRepositoryTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ProfileRepositoryTests PRIVATE
    cjson
    Bcrypt
    Crypt32
    Ole32
    Shell32
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

add_executable(WindowStateScalingTests
    ui/WindowStateScalingTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/WindowStateScaling.cpp
)

target_include_directories(WindowStateScalingTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME WindowStateScalingTests COMMAND WindowStateScalingTests)

add_executable(MainWindowActivationTests
    ui/MainWindowActivationTests.cpp
)

target_include_directories(MainWindowActivationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME MainWindowActivationTests COMMAND MainWindowActivationTests)

add_executable(MainWindowShortcutsTests
    ui/MainWindowShortcutsTests.cpp
)

target_include_directories(MainWindowShortcutsTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME MainWindowShortcutsTests COMMAND MainWindowShortcutsTests)

add_executable(MainWindowStatePersistenceTests
    ui/MainWindowStatePersistenceTests.cpp
)

target_include_directories(MainWindowStatePersistenceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME MainWindowStatePersistenceTests COMMAND MainWindowStatePersistenceTests)
