cmake_minimum_required(VERSION 3.21)
include(cmake/SourceTimestamps.cmake)
project(GLO LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()
include(cmake/ClientPolicy.cmake)

include(CTest)
option(GLO_STATIC_MINGW "Link MinGW runtime statically for portable proof binaries" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(glo_protocol STATIC protocol/protocol.cpp)
target_include_directories(glo_protocol PUBLIC protocol/include)

add_executable(glo_protocol_test tests/protocol_test.cpp)
target_link_libraries(glo_protocol_test PRIVATE glo_protocol)
add_test(NAME glo_protocol_test COMMAND glo_protocol_test)

add_library(glo_route_policy STATIC app/core/src/route_policy.cpp)
target_include_directories(glo_route_policy PUBLIC app/core/include)
add_executable(glo_route_policy_test tests/route_policy_test.cpp)
target_link_libraries(glo_route_policy_test PRIVATE glo_route_policy)
add_test(NAME glo_route_policy_test COMMAND glo_route_policy_test)

add_library(glo_route_scope STATIC app/core/src/route_scope.cpp)
target_include_directories(glo_route_scope PUBLIC app/core/include)
add_executable(glo_route_scope_test tests/route_scope_test.cpp)
target_link_libraries(glo_route_scope_test PRIVATE glo_route_scope)
add_test(NAME glo_route_scope_test COMMAND glo_route_scope_test)

add_library(glo_preflight_policy STATIC app/core/src/preflight_policy.cpp)
target_include_directories(glo_preflight_policy PUBLIC app/core/include)
target_link_libraries(glo_preflight_policy PUBLIC glo_route_scope)
add_executable(glo_preflight_policy_test tests/preflight_policy_test.cpp)
target_link_libraries(glo_preflight_policy_test PRIVATE glo_preflight_policy)
add_test(NAME glo_preflight_policy_test COMMAND glo_preflight_policy_test)

add_executable(glo_ui_phase_test tests/ui_phase_test.cpp)
target_include_directories(glo_ui_phase_test PRIVATE app/core/include)
add_test(NAME glo_ui_phase_test COMMAND glo_ui_phase_test)

add_executable(glo_game_profile_test tests/game_profile_test.cpp)
target_include_directories(glo_game_profile_test PRIVATE app/core/include)
add_test(NAME glo_game_profile_test COMMAND glo_game_profile_test)

add_library(glo_profile_hosts STATIC app/core/src/profile_hosts.cpp)
target_include_directories(glo_profile_hosts PUBLIC app/core/include)
add_executable(glo_profile_hosts_test tests/profile_hosts_test.cpp)
target_link_libraries(glo_profile_hosts_test PRIVATE glo_profile_hosts)
add_test(NAME glo_profile_hosts_test COMMAND glo_profile_hosts_test)
add_library(glo_session_config STATIC app/core/src/session_config.cpp)
target_include_directories(glo_session_config PUBLIC app/core/include)
target_link_libraries(glo_session_config PUBLIC glo_profile_hosts)
target_compile_definitions(glo_session_config PUBLIC GLO_VERSION="${GLO_VERSION}")
add_executable(glo_session_config_test tests/session_config_test.cpp)
target_link_libraries(glo_session_config_test PRIVATE glo_session_config)
add_test(NAME glo_session_config_test COMMAND glo_session_config_test)

add_executable(glo_quality_metrics_test tests/quality_metrics_test.cpp)
target_include_directories(glo_quality_metrics_test PRIVATE app/core/include)
add_test(NAME glo_quality_metrics_test COMMAND glo_quality_metrics_test)

add_executable(glo_route_events_test tests/route_events_test.cpp)
target_include_directories(glo_route_events_test PRIVATE app/core/include)
add_test(NAME glo_route_events_test COMMAND glo_route_events_test)


add_executable(glo_log_rotation_test tests/log_rotation_test.cpp)
target_include_directories(glo_log_rotation_test PRIVATE app/core/include)
add_test(NAME glo_log_rotation_test COMMAND glo_log_rotation_test)

set(SODIUM_ROOT "" CACHE PATH "libsodium installation prefix for the TARGET architecture")
find_path(SODIUM_INCLUDE_DIR sodium.h HINTS "${SODIUM_ROOT}/include")
find_library(SODIUM_LIBRARY NAMES sodium libsodium HINTS "${SODIUM_ROOT}/lib")
if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "libsodium is required. Set SODIUM_ROOT to a target-architecture install prefix or use tools/build_app.sh.")
endif()
add_library(glo_secure STATIC secure_transport/src/secure_transport.cpp)
target_include_directories(glo_secure PUBLIC secure_transport/include PRIVATE "${SODIUM_INCLUDE_DIR}")
target_link_libraries(glo_secure PUBLIC glo_protocol "${SODIUM_LIBRARY}")
if(WIN32)
    target_compile_definitions(glo_secure PUBLIC SODIUM_STATIC)
    target_link_libraries(glo_secure PUBLIC bcrypt)
endif()

if(WIN32)
    add_library(glo_client_core STATIC
        app/core/src/client_core.cpp
        app/core/src/roblox_detector.cpp
        app/core/src/game_detector.cpp
        app/core/src/profile_hosts.cpp
        app/core/src/wintun_tunnel.cpp
        app/core/src/client_log.cpp
        app/core/src/network_worker.cpp
        app/core/src/user_paths.cpp
    )
    target_include_directories(glo_client_core PUBLIC
        app/core/include
        protocol/include
        "${SODIUM_INCLUDE_DIR}"
    )
    set(GLO_WINTUN_DLL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wintun/bin/amd64/wintun.dll")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wintun/INFO.json" GLO_WINTUN_INFO_JSON)
    string(JSON GLO_WINTUN_SHA256 GET "${GLO_WINTUN_INFO_JSON}" file sha256)
    if(NOT EXISTS "${GLO_WINTUN_DLL}")
        message(FATAL_ERROR "Canonical Wintun DLL is missing: ${GLO_WINTUN_DLL}")
    endif()
    file(SHA256 "${GLO_WINTUN_DLL}" GLO_WINTUN_ACTUAL_SHA256)
    if(NOT GLO_WINTUN_ACTUAL_SHA256 STREQUAL GLO_WINTUN_SHA256)
        message(FATAL_ERROR "Canonical Wintun SHA-256 mismatch: expected ${GLO_WINTUN_SHA256}, got ${GLO_WINTUN_ACTUAL_SHA256}")
    endif()
    target_compile_definitions(glo_client_core PUBLIC _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX PRIVATE GLO_VERSION="${GLO_VERSION}" GLO_WINTUN_SHA256="${GLO_WINTUN_SHA256}")
    target_link_libraries(glo_client_core PUBLIC glo_secure glo_protocol glo_route_policy glo_route_scope glo_profile_hosts ws2_32 iphlpapi shell32 advapi32)

    # Generate the Windows resource file in the build directory with an
    # absolute manifest path. GNU windres resolves resource paths from its
    # working directory, not reliably from the source .rc location.
    set(GLO_MANIFEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/app/core/glo.manifest")
    set(GLO_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/app/core/assets/icons/win32")
    set(GLO_APP_ICON_SOURCE "${GLO_ICON_DIR}/glo-app.ico")
    set(GLO_RC "${CMAKE_CURRENT_BINARY_DIR}/glo.rc")
    file(TO_CMAKE_PATH "${GLO_MANIFEST_SOURCE}" GLO_MANIFEST_RC_PATH)
    file(TO_CMAKE_PATH "${GLO_APP_ICON_SOURCE}" GLO_APP_ICON_RC_PATH)
    foreach(_icon settings_light settings_dark sun_light sun_dark moon_light moon_dark back_light back_dark official_light official_dark github_light github_dark facebook_light facebook_dark paste_light paste_dark import_light import_dark connect_light connect_dark disconnect_light disconnect_dark)
        file(TO_CMAKE_PATH "${GLO_ICON_DIR}/${_icon}.ico" "GLO_ICON_${_icon}")
    endforeach()
    string(CONCAT GLO_RC_CONTENT
        "1 24 \"${GLO_MANIFEST_RC_PATH}\"\n"
        "101 ICON \"${GLO_APP_ICON_RC_PATH}\"\n"
        "201 ICON \"${GLO_ICON_settings_light}\"\n"
        "202 ICON \"${GLO_ICON_settings_dark}\"\n"
        "203 ICON \"${GLO_ICON_sun_light}\"\n"
        "204 ICON \"${GLO_ICON_sun_dark}\"\n"
        "205 ICON \"${GLO_ICON_moon_light}\"\n"
        "206 ICON \"${GLO_ICON_moon_dark}\"\n"
        "207 ICON \"${GLO_ICON_back_light}\"\n"
        "208 ICON \"${GLO_ICON_back_dark}\"\n"
        "209 ICON \"${GLO_ICON_official_light}\"\n"
        "210 ICON \"${GLO_ICON_official_dark}\"\n"
        "211 ICON \"${GLO_ICON_github_light}\"\n"
        "212 ICON \"${GLO_ICON_github_dark}\"\n"
        "213 ICON \"${GLO_ICON_facebook_light}\"\n"
        "214 ICON \"${GLO_ICON_facebook_dark}\"\n"
        "215 ICON \"${GLO_ICON_paste_light}\"\n"
        "216 ICON \"${GLO_ICON_paste_dark}\"\n"
        "217 ICON \"${GLO_ICON_import_light}\"\n"
        "218 ICON \"${GLO_ICON_import_dark}\"\n"
        "219 ICON \"${GLO_ICON_connect_light}\"\n"
        "220 ICON \"${GLO_ICON_connect_dark}\"\n"
        "221 ICON \"${GLO_ICON_disconnect_light}\"\n"
        "222 ICON \"${GLO_ICON_disconnect_dark}\"\n"
    )
    file(CONFIGURE OUTPUT "${GLO_RC}" CONTENT "${GLO_RC_CONTENT}" @ONLY)
    set_source_files_properties("${GLO_RC}" PROPERTIES GENERATED TRUE)
    add_executable(GLO WIN32
        app/frontend/main.cpp
        ${GLO_RC})
    glo_client_policy(GLO)
    target_include_directories(GLO PRIVATE app/core/include protocol/include)
    target_link_libraries(GLO PRIVATE glo_client_core glo_session_config user32 gdi32 shell32 ole32 comdlg32 winhttp)


    if(MINGW AND GLO_STATIC_MINGW)
        target_link_options(GLO PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    add_custom_command(TARGET GLO POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GLO_WINTUN_DLL}" "$<TARGET_FILE_DIR:GLO>/wintun.dll"
        COMMENT "Copying verified Wintun amd64 DLL beside generic GLO client")
else()
    message(STATUS "Windows client targets are skipped on this platform; protocol tests and Go relay remain buildable.")
endif()

add_executable(glo_secure_probe tests/secure_cpp_probe.cpp)
target_link_libraries(glo_secure_probe PRIVATE glo_secure)

add_executable(glo_cli_generic_test tests/cli_policy_test.cpp)
target_include_directories(glo_cli_generic_test PRIVATE app/core/include)
glo_client_policy(glo_cli_generic_test)
add_test(NAME glo_cli_generic_test COMMAND glo_cli_generic_test)
