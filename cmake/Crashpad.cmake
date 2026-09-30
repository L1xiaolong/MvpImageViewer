# Crashpad is built separately by its supported GN build; the main project remains CMake.
set(MVPVIEW_CRASHPAD_ROOT "${PROJECT_SOURCE_DIR}/build/crashpad/crashpad" CACHE PATH "Pinned Crashpad checkout")
set(_crashpad_out "${MVPVIEW_CRASHPAD_ROOT}/out/mvpview")
set(_crashpad_revision "4b8fc2b536e04407032cb6eba687cfe68cb5966a")
set(_built_revision "")
if(EXISTS "${_crashpad_out}/mvpview-revision")
    file(STRINGS "${_crashpad_out}/mvpview-revision" _built_revision)
endif()
if(NOT _built_revision STREQUAL _crashpad_revision OR NOT EXISTS "${_crashpad_out}/libmvpview_crashpad.a"
   OR NOT EXISTS "${MVPVIEW_CRASHPAD_ROOT}/out/mvpview-handler/crashpad_handler")
    execute_process(COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/build_crashpad_macos.sh"
        "${MVPVIEW_CRASHPAD_ROOT}" RESULT_VARIABLE _crashpad_result)
    if(NOT _crashpad_result EQUAL 0)
        message(FATAL_ERROR "Could not build pinned Crashpad. See scripts/build_crashpad_macos.sh.")
    endif()
endif()
add_library(mvpview_crashpad INTERFACE)
target_include_directories(mvpview_crashpad INTERFACE "${MVPVIEW_CRASHPAD_ROOT}"
    "${MVPVIEW_CRASHPAD_ROOT}/third_party/mini_chromium/mini_chromium" "${_crashpad_out}/gen")
target_link_libraries(mvpview_crashpad INTERFACE
    "${_crashpad_out}/libmvpview_crashpad.a"
    "-framework Foundation" "-framework CoreFoundation" "-framework Security"
    "-framework ApplicationServices" "-framework IOKit" "-framework SystemConfiguration" bsm z)
set(MVPVIEW_CRASHPAD_HANDLER "${MVPVIEW_CRASHPAD_ROOT}/out/mvpview-handler/crashpad_handler" CACHE INTERNAL "Crashpad helper")
