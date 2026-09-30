cmake_minimum_required(VERSION 3.20)
project(RoadCraftHeadTracking VERSION 0.2.0 LANGUAGES C CXX)

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

add_library(rc_discovery STATIC src/builds/runtime_discovery.cpp src/builds/camera_contract.cpp)
target_include_directories(rc_discovery PUBLIC src cameraunlock-core/cpp/include
    PRIVATE cameraunlock-core/vendor/minhook/src/hde)
target_link_libraries(rc_discovery PUBLIC minhook)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/config.cpp
    src/legacy_config/legacy_config.cpp
    src/camera_fov.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/window_centering.cpp
    src/builds/build_registry.cpp
    src/builds/runtime_image.cpp
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
    rc_discovery
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

    # The frozen reader's value guards - headers only, nothing to link.
    rc_add_test(config_sanitize)

    rc_add_test(runtime_discovery src/builds/build_registry.cpp src/builds/steam_offsets.cpp)
    target_include_directories(rc_runtime_discovery_tests PRIVATE cameraunlock-core/vendor/minhook/src/hde)
    target_link_libraries(rc_runtime_discovery_tests PRIVATE rc_discovery cameraunlock)

    # The camera composition compiled from the production translation unit:
    # signs, camera-local rotation order, clean-axis lean and the gate blend.
    rc_add_test(camera_transform src/camera_transform.cpp)

    # The live field-of-view read off the camera, the base each camera rests at,
    # and the zoom factor the pose is scaled by.
    rc_add_test(camera_fov src/camera_fov.cpp src/camera_transform.cpp)

    # The gate's pure classification and the Wwise ID hash it matches on.
    rc_add_test(game_state src/game_state.cpp src/builds/build_registry.cpp src/builds/runtime_image.cpp
                src/builds/steam_offsets.cpp)
    target_link_libraries(rc_game_state_tests PRIVATE cameraunlock cameraunlock_hooks rc_discovery)

    # The committed config against the table's fresh render, the fleet's
    # default hotkeys, and the mode cycle's save. `rc_config_tests
    # --render-config <path>` is pixi run render-config.
    rc_add_test(config src/config.cpp src/legacy_config/legacy_config.cpp)
    target_link_libraries(rc_config_tests PRIVATE cameraunlock)
    target_compile_definitions(rc_config_tests PRIVATE
        RC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The hotkey half of the frozen loader: which INI values reach a binding and
    # which leave the action on the key it already had.
    rc_add_test(config_hotkeys src/legacy_config/legacy_config.cpp)
    target_link_libraries(rc_config_hotkeys_tests PRIVATE cameraunlock)

    # The port, the smoothing pair, the position limits and the flags, read
    # through the frozen loader: what each key does with a trailing comment, a
    # comma decimal, a value out of range and a NaN.
    rc_add_test(config_values src/legacy_config/legacy_config.cpp)
    target_link_libraries(rc_config_values_tests PRIVATE cameraunlock)

    # The canonical config conversion's differential test: v0.1.0's reader and
    # startup code (the oracle, compiled from byte copies in
    # tests/config_differential/oracle) against the frozen reader in
    # src/legacy_config/, and that against the config owner's migration into
    # CameraUnlock.ini.
    set(RC_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_executable(rc_config_differential
        ${RC_DIFFERENTIAL_DIR}/differential_tests.cpp
        ${RC_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(rc_config_differential PRIVATE
        src
        ${RC_DIFFERENTIAL_DIR}
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(rc_config_differential PRIVATE
        RC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(rc_config_differential PRIVATE cameraunlock)
    set_target_properties(rc_config_differential PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(rc_config_differential PRIVATE /WX /EHsc /bigobj)
    endif()
    add_test(NAME config_differential COMMAND rc_config_differential)
    set_property(GLOBAL APPEND PROPERTY RC_TEST_TARGETS rc_config_differential)
    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated/.
    add_test(NAME config_lint COMMAND node
             ${RC_DIFFERENTIAL_DIR}/lint-migrated.mjs
             $<TARGET_FILE_DIR:rc_config_differential>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)
    set_tests_properties(config_differential config_lint PROPERTIES LABELS differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay v0.1.0's (git show v0.1.0:src/<file>),
    # the core sources both readers compile must stay the ones v0.1.0 was built
    # with (core b8d47eb), and the frozen reader is never edited.
    function(rc_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(RC_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${RC_DIFFERENTIAL_DIR}/oracle/src/config.cpp|4df87edd1af8633895143b25c635547e25b245df383a090b548712ed88e942ee"
            "${RC_DIFFERENTIAL_DIR}/oracle/src/config.h|8e41879e3f2dc29a3095c80e1f950ea611f54b301744a3b3590dcc057cf02dcf"
            "${RC_DIFFERENTIAL_DIR}/oracle/src/config_sanitize.h|f31557b0f4cbe7283b814fe4e30a5d0152e3fb735a9f6fdad30ad1231f827077"
            "${RC_DIFFERENTIAL_DIR}/oracle/src/logging.h|998b401b301041e8a6c7660246a13fce95aa6d60ecdf4c808ea55f5bb16b0cc3"
            "${RC_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${RC_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${RC_CORE_DIR}/include/cameraunlock/protocol/port_utils.h|91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"
            "${RC_CORE_DIR}/include/cameraunlock/math/smoothing_utils.h|fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"
            "${RC_CORE_DIR}/include/cameraunlock/math/angle_utils.h|d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"
            "${RC_CORE_DIR}/include/cameraunlock/data/position_settings.h|b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"
            "${RC_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${RC_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|b07878a612b46417924bfdd40774ced69a9bb79c7b1239cabc847f59a0229a7f"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|a7febe14df10f84a7b63d65fdf376ef71170bcc7107b1ef96efa3c9ab96f1560"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/config_sanitize.h|7605121eddc3a59fb228cd56711f852aa570a4f7d24a0b34288653a695bddaeb")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        rc_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()

    # Live sockets: binds the port, holds it, releases it, and times the
    # receiver's recovery. Slower than the rest of the suite by design.
    rc_add_test(udp_recovery)
    target_link_libraries(rc_udp_recovery_tests PRIVATE cameraunlock ws2_32)
    set_tests_properties(udp_recovery PROPERTIES TIMEOUT 120)

    get_property(rc_test_targets GLOBAL PROPERTY RC_TEST_TARGETS)
    add_custom_target(rc_tests DEPENDS ${rc_test_targets})
endif()
