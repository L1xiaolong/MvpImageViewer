# Crashpad is built separately by its supported GN build; the main project remains CMake.
set(ISPVIEW_CRASHPAD_ROOT "${PROJECT_SOURCE_DIR}/build/crashpad/crashpad" CACHE PATH "Pinned Crashpad checkout")
set(_crashpad_out "${ISPVIEW_CRASHPAD_ROOT}/out/ispview")
set(_crashpad_revision "4b8fc2b536e04407032cb6eba687cfe68cb5966a")
set(_built_revision "")
if(EXISTS "${_crashpad_out}/ispview-revision")
    file(STRINGS "${_crashpad_out}/ispview-revision" _built_revision)
endif()
if(NOT _built_revision STREQUAL _crashpad_revision OR NOT EXISTS "${_crashpad_out}/libispview_crashpad.a"
   OR NOT EXISTS "${ISPVIEW_CRASHPAD_ROOT}/out/ispview-handler/crashpad_handler")
    execute_process(COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/build_crashpad_macos.sh"
        "${ISPVIEW_CRASHPAD_ROOT}" RESULT_VARIABLE _crashpad_result)
    if(NOT _crashpad_result EQUAL 0)
        message(FATAL_ERROR "Could not build pinned Crashpad. See scripts/build_crashpad_macos.sh.")
    endif()
endif()
add_library(ispview_crashpad INTERFACE)
target_include_directories(ispview_crashpad INTERFACE "${ISPVIEW_CRASHPAD_ROOT}"
    "${ISPVIEW_CRASHPAD_ROOT}/third_party/mini_chromium/mini_chromium" "${_crashpad_out}/gen")
target_link_libraries(ispview_crashpad INTERFACE
    "${_crashpad_out}/libispview_crashpad.a"
    "-framework Foundation" "-framework CoreFoundation" "-framework Security"
    "-framework ApplicationServices" "-framework IOKit" "-framework SystemConfiguration" bsm z)
set(ISPVIEW_CRASHPAD_HANDLER "${ISPVIEW_CRASHPAD_ROOT}/out/ispview-handler/crashpad_handler" CACHE INTERNAL "Crashpad helper")
