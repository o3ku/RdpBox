add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

function(rdpbox_prepend_test_runtime_path test_name runtime_target)
    if(WIN32)
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:${runtime_target}>"
        )
    endif()
endfunction()

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

add_executable(RdpKeyboardInputRouterTests
    rdp/RdpKeyboardInputRouterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpKeyboardInputRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpInputModifiers.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpInputEventUtil.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpModifierSyncTracker.cpp
)

target_include_directories(RdpKeyboardInputRouterTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(RdpKeyboardInputRouterTests PRIVATE
    User32
)

add_test(NAME RdpKeyboardInputRouterTests COMMAND RdpKeyboardInputRouterTests)

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

add_executable(WindowsClipboardBehaviorTests
    rdp/WindowsClipboardBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/WindowsClipboardBehavior.cpp
)

target_include_directories(WindowsClipboardBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME WindowsClipboardBehaviorTests COMMAND WindowsClipboardBehaviorTests)

add_executable(WindowsClipboardDataObjectTests
    rdp/WindowsClipboardDataObjectTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/WindowsClipboardBackendDataObject.cpp
)

target_include_directories(WindowsClipboardDataObjectTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

get_target_property(WINDOWS_CLIPBOARD_TEST_FREERDP_INCLUDE_DIRS freerdp INTERFACE_INCLUDE_DIRECTORIES)
foreach(_freerdp_include_dir IN LISTS WINDOWS_CLIPBOARD_TEST_FREERDP_INCLUDE_DIRS)
    if(EXISTS "${_freerdp_include_dir}/freerdp3")
        target_include_directories(WindowsClipboardDataObjectTests PRIVATE "${_freerdp_include_dir}/freerdp3")
    endif()
endforeach()

target_link_libraries(WindowsClipboardDataObjectTests PRIVATE
    freerdp
    freerdp-client
    winpr
    Ole32
)

add_test(NAME WindowsClipboardDataObjectTests COMMAND WindowsClipboardDataObjectTests)

add_executable(RdpCertificatePromptBehaviorTests
    rdp/RdpCertificatePromptBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpCertificatePromptBehavior.cpp
)

target_include_directories(RdpCertificatePromptBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpCertificatePromptBehaviorTests COMMAND RdpCertificatePromptBehaviorTests)

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

add_executable(RdpProcessEventBehaviorTests
    rdp/RdpProcessEventBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpProcessEventBehavior.cpp
)

target_include_directories(RdpProcessEventBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpProcessEventBehaviorTests COMMAND RdpProcessEventBehaviorTests)

add_executable(RdpProcessEventQueueBehaviorTests
    rdp/RdpProcessEventQueueBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpProcessEventQueueBehavior.cpp
)

target_include_directories(RdpProcessEventQueueBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(RdpProcessEventQueueBehaviorTests PRIVATE
    winpr
)

add_test(NAME RdpProcessEventQueueBehaviorTests COMMAND RdpProcessEventQueueBehaviorTests)

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

add_executable(RdpSessionViewBehaviorTests
    rdp/RdpSessionViewBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/rdp/RdpSessionViewBehavior.cpp
)

target_include_directories(RdpSessionViewBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME RdpSessionViewBehaviorTests COMMAND RdpSessionViewBehaviorTests)

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

add_executable(SessionCollectionBehaviorTests
    session/SessionCollectionBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/session/SessionCollectionBehavior.cpp
    ${CMAKE_SOURCE_DIR}/src/session/SessionTabBehavior.cpp
)

target_include_directories(SessionCollectionBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME SessionCollectionBehaviorTests COMMAND SessionCollectionBehaviorTests)

add_executable(SessionTabBehaviorTests
    session/SessionTabBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/session/SessionTabBehavior.cpp
)

target_include_directories(SessionTabBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME SessionTabBehaviorTests COMMAND SessionTabBehaviorTests)

add_executable(SessionManagerIntegrationTests
    session/SessionManagerIntegrationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/session/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/session/SessionCollectionBehavior.cpp
    ${CMAKE_SOURCE_DIR}/src/session/SessionTabBehavior.cpp
    ${CMAKE_SOURCE_DIR}/src/common/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/common/PasswordProtection.cpp
    ${CMAKE_SOURCE_DIR}/src/profiles/ProfileRepository.cpp
)

target_include_directories(SessionManagerIntegrationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(SessionManagerIntegrationTests PRIVATE
    winpr
    cjson
    Bcrypt
    Crypt32
    Ole32
    Shell32
)

add_test(NAME SessionManagerIntegrationTests COMMAND SessionManagerIntegrationTests)

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

add_executable(ConnectionLaunchArgsTests
    common/ConnectionLaunchArgsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/common/ConnectionLaunchArgs.cpp
)

target_include_directories(ConnectionLaunchArgsTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME ConnectionLaunchArgsTests COMMAND ConnectionLaunchArgsTests)

add_executable(UpdateClientTests
    common/UpdateClientTests.cpp
    ${CMAKE_SOURCE_DIR}/src/common/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/common/UpdateClient.cpp
)

target_include_directories(UpdateClientTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(UpdateClientTests PRIVATE
    nlohmann_json::nlohmann_json
    Shell32
)

add_test(NAME UpdateClientTests COMMAND UpdateClientTests)

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

add_executable(MainWindowTabBehaviorTests
    ui/MainWindowTabBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindowTabBehavior.cpp
)

target_include_directories(MainWindowTabBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME MainWindowTabBehaviorTests COMMAND MainWindowTabBehaviorTests)

add_executable(MainWindowSessionBehaviorTests
    ui/MainWindowSessionBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindowSessionBehavior.cpp
)

target_include_directories(MainWindowSessionBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME MainWindowSessionBehaviorTests COMMAND MainWindowSessionBehaviorTests)

add_executable(MainWindowUpdateBehaviorTests
    ui/MainWindowUpdateBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindowUpdateBehavior.cpp
)

target_include_directories(MainWindowUpdateBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME MainWindowUpdateBehaviorTests COMMAND MainWindowUpdateBehaviorTests)

add_executable(MainWindowLayoutBehaviorTests
    ui/MainWindowLayoutBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindowLayoutBehavior.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/WindowFrameMetrics.cpp
)

target_include_directories(MainWindowLayoutBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME MainWindowLayoutBehaviorTests COMMAND MainWindowLayoutBehaviorTests)

add_executable(ConnectionListBehaviorTests
    ui/ConnectionListBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ConnectionListBehavior.cpp
)

target_include_directories(ConnectionListBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME ConnectionListBehaviorTests COMMAND ConnectionListBehaviorTests)

add_executable(ProfileEditBehaviorTests
    ui/ProfileEditBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProfileEditBehavior.cpp
)

target_include_directories(ProfileEditBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME ProfileEditBehaviorTests COMMAND ProfileEditBehaviorTests)

add_executable(BrowserTabBehaviorTests
    ui/BrowserTabBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/BrowserTabBehavior.cpp
)

target_include_directories(BrowserTabBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME BrowserTabBehaviorTests COMMAND BrowserTabBehaviorTests)

add_executable(BrowserTabBarTests
    ui/BrowserTabBarTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/BrowserTabBar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/BrowserTabBehavior.cpp
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

add_executable(QtWindowChromeBehaviorTests
    qt/QtWindowChromeBehaviorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/qt/QtWindowChromeBehavior.cpp
)

target_include_directories(QtWindowChromeBehaviorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(QtWindowChromeBehaviorTests PRIVATE
    Qt5::Core
)

add_test(NAME QtWindowChromeBehaviorTests COMMAND QtWindowChromeBehaviorTests)
rdpbox_prepend_test_runtime_path(QtWindowChromeBehaviorTests Qt5::Core)
