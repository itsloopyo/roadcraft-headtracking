cmake_minimum_required(VERSION 3.20)
project(RoadCraftHeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/config.cpp
    src/camera_fov.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/hotkey_names.cpp
    src/window_centering.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(RoadCraftHeadTracking SHARED ${MOD_SOURCES})

target_include_directories(RoadCraftHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

# release.ps1 bumps the project VERSION above, so the banner the log opens with
# names the build a player is reporting against without a second place to bump.
target_compile_definitions(RoadCraftHeadTracking PRIVATE
    RC_HT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(RoadCraftHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    ws2_32
)

# A linker map so a crash offset (asi+0xNNNN) resolves to a symbol.
if(MSVC)
    target_link_options(RoadCraftHeadTracking PRIVATE /MAP /MAPINFO:EXPORTS)
endif()

set_target_properties(RoadCraftHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Opt-in unit tests, built by scripts/run-tests.ps1 (`pixi run test`) so the
# packaged build/ tree never carries a test binary.
option(RC_BUILD_TESTS "Build RoadCraftHeadTracking unit tests" OFF)
if(RC_BUILD_TESTS)
    enable_testing()

    function(rc_add_test test_name)
        set(target "rc_${test_name}_tests")
        add_executable(${target} tests/${test_name}_tests.cpp ${ARGN})
        target_include_directories(${target} PRIVATE
            src
            cameraunlock-core/cpp/include
        )
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        add_test(NAME ${test_name} COMMAND ${target})
        set_property(GLOBAL APPEND PROPERTY RC_TEST_TARGETS ${target})
    endfunction()

    rc_add_test(config_sanitize)

    # The camera composition compiled from the production translation unit:
    # signs, camera-local rotation order, clean-axis lean and the gate blend.
    rc_add_test(camera_transform src/camera_transform.cpp)

    # The live field-of-view read off the camera, the base each camera rests at,
    # and the zoom factor the pose is scaled by.
    rc_add_test(camera_fov src/camera_fov.cpp src/camera_transform.cpp)

    # The gate's pure classification and the Wwise ID hash it matches on.
    rc_add_test(game_state src/game_state.cpp src/builds/build_registry.cpp
                src/builds/steam_offsets.cpp)
    target_link_libraries(rc_game_state_tests PRIVATE cameraunlock cameraunlock_hooks)

    rc_add_test(hotkey_names src/hotkey_names.cpp)

    rc_add_test(config_defaults src/config.cpp)
    target_link_libraries(rc_config_defaults_tests PRIVATE cameraunlock)
    target_compile_definitions(rc_config_defaults_tests PRIVATE
        RC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    rc_add_test(config_hotkeys src/config.cpp)
    target_link_libraries(rc_config_hotkeys_tests PRIVATE cameraunlock)

    # The port, the smoothing pair, the position limits and the flags, read
    # through the real loader: what each key does with a trailing comment, a
    # comma decimal, a value out of range and a NaN.
    rc_add_test(config_values src/config.cpp)
    target_link_libraries(rc_config_values_tests PRIVATE cameraunlock)

    # Live sockets: binds the port, holds it, releases it, and times the
    # receiver's recovery. Slower than the rest of the suite by design.
    rc_add_test(udp_recovery)
    target_link_libraries(rc_udp_recovery_tests PRIVATE cameraunlock ws2_32)
    set_tests_properties(udp_recovery PROPERTIES TIMEOUT 120)

    get_property(rc_test_targets GLOBAL PROPERTY RC_TEST_TARGETS)
    add_custom_target(rc_tests DEPENDS ${rc_test_targets})
endif()
