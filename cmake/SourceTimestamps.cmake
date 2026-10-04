# Also usable as: cmake -DGLO_FIX_SOURCE_TIMESTAMPS=ON -P cmake/SourceTimestamps.cmake
# Repair is opt-in and script-only: never persist it in a build cache.
get_filename_component(_glo_source_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(GLOB_RECURSE _glo_time_inputs LIST_DIRECTORIES false
    "${_glo_source_root}/app/*"
    "${_glo_source_root}/protocol/*"
    "${_glo_source_root}/secure_transport/*"
    "${_glo_source_root}/cmake/*")
list(APPEND _glo_time_inputs "${_glo_source_root}/CMakeLists.txt" "${_glo_source_root}/VERSION" "${_glo_source_root}/RELEASE" "${_glo_source_root}/third_party/wintun/INFO.json")
# SOURCE_DATE_EPOCH is a reproducibility input, not the actual filesystem clock.
if(DEFINED ENV{SOURCE_DATE_EPOCH})
    set(_glo_saved_epoch "$ENV{SOURCE_DATE_EPOCH}")
    unset(ENV{SOURCE_DATE_EPOCH})
endif()
string(TIMESTAMP _glo_now "%s" UTC)
if(DEFINED _glo_saved_epoch)
    set(ENV{SOURCE_DATE_EPOCH} "${_glo_saved_epoch}")
    unset(_glo_saved_epoch)
endif()
set(_glo_future "")
foreach(_glo_file IN LISTS _glo_time_inputs)
    if(IS_SYMLINK "${_glo_file}")
        continue()
    endif()
    file(TIMESTAMP "${_glo_file}" _glo_mtime "%s" UTC)
    if(_glo_mtime GREATER _glo_now)
        if(CMAKE_SCRIPT_MODE_FILE AND GLO_FIX_SOURCE_TIMESTAMPS)
            file(TOUCH "${_glo_file}")
            message(STATUS "Normalized future timestamp: ${_glo_file}")
        else()
            math(EXPR _glo_ahead "${_glo_mtime} - ${_glo_now}")
            string(APPEND _glo_future "\n  ${_glo_file} (+${_glo_ahead}s)")
        endif()
    endif()
endforeach()
if(_glo_future)
    message(FATAL_ERROR "GLO_FUTURE_SOURCE_TIMESTAMP: source files are ahead of the system clock:${_glo_future}\nCheck the system clock. If it is correct, explicitly repair source timestamps (contents unchanged):\n  cmake -DGLO_FIX_SOURCE_TIMESTAMPS=ON -P cmake/SourceTimestamps.cmake\nThen configure/build again. Do not disable regeneration.")
endif()
