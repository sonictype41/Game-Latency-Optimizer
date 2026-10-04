# GLO generic-client build identity. VERSION is wire/app compatibility; RELEASE is packaging revision.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" GLO_VERSION)
string(STRIP "${GLO_VERSION}" GLO_VERSION)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../RELEASE" GLO_RELEASE)
string(STRIP "${GLO_RELEASE}" GLO_RELEASE)
set(GLO_BUILD_ID "generic-config-client")
function(glo_client_policy target)
    target_compile_definitions(${target} PRIVATE GLO_VERSION="${GLO_VERSION}" GLO_RELEASE="${GLO_RELEASE}")
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:${target}>/${target}.build.json"
        CONTENT "{\"version\":\"${GLO_VERSION}\",\"release\":\"${GLO_RELEASE}\",\"client\":\"generic-config\",\"build_id\":\"${GLO_BUILD_ID}\",\"compiler\":\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\"}\n")
endfunction()
