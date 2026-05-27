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
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpInputModifiers.cpp
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

add_executable(FrameBufferMemoryTests
    rdp/FrameBufferMemoryTests.cpp
)

target_include_directories(FrameBufferMemoryTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME FrameBufferMemoryTests COMMAND FrameBufferMemoryTests)

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

add_executable(Win32StringTests
    common/Win32StringTests.cpp
)

target_include_directories(Win32StringTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(Win32StringTests PRIVATE
    Ole32
)

add_test(NAME Win32StringTests COMMAND Win32StringTests)

add_executable(PasswordProtectionTests
    common/PasswordProtectionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/common/PasswordProtection.cpp
)

target_include_directories(PasswordProtectionTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(PasswordProtectionTests PRIVATE
    Bcrypt
    Crypt32
)

add_test(NAME PasswordProtectionTests COMMAND PasswordProtectionTests)

add_executable(ProfileNativeTypesTests
    common/ProfileNativeTypesTests.cpp
)

target_include_directories(ProfileNativeTypesTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME ProfileNativeTypesTests COMMAND ProfileNativeTypesTests)

add_executable(AppPathsTests
    common/AppPathsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/common/AppPaths.cpp
)

target_include_directories(AppPathsTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(AppPathsTests PRIVATE
    nlohmann_json::nlohmann_json
    Shell32
)

add_test(NAME AppPathsTests COMMAND AppPathsTests)

add_executable(AppPersistenceSmokeTests
    smoke/AppPersistenceSmokeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/common/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/common/PasswordProtection.cpp
    ${CMAKE_SOURCE_DIR}/src/profiles/ProfileRepository.cpp
)

target_include_directories(AppPersistenceSmokeTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(AppPersistenceSmokeTests PRIVATE
    nlohmann_json::nlohmann_json
    cjson
    Bcrypt
    Crypt32
    Ole32
    Shell32
)

add_test(NAME AppPersistenceSmokeTests COMMAND AppPersistenceSmokeTests)

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

add_executable(Win10ThemeTests
    ui/Win10ThemeTests.cpp
)

target_include_directories(Win10ThemeTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME Win10ThemeTests COMMAND Win10ThemeTests)

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

add_executable(BrowserTabBarTests
    ui/BrowserTabBarTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/BrowserTabBar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ParentResizeForwarder.cpp
)

target_include_directories(BrowserTabBarTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(BrowserTabBarTests PRIVATE
    _AFXDLL
)

target_link_libraries(BrowserTabBarTests PRIVATE
    Comctl32
    UxTheme
)

add_test(NAME BrowserTabBarTests COMMAND BrowserTabBarTests)

add_executable(ParentResizeForwarderTests
    ui/ParentResizeForwarderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ParentResizeForwarder.cpp
)

target_include_directories(ParentResizeForwarderTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(ParentResizeForwarderTests PRIVATE
    _AFXDLL
)

add_test(NAME ParentResizeForwarderTests COMMAND ParentResizeForwarderTests)
