# Copy project and icon notices into the directory distributed to users.
# Other bundled runtime dependencies require their own license inventory.
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED NOTICE_DESTINATION OR NOTICE_DESTINATION STREQUAL "")
    message(FATAL_ERROR "NOTICE_DESTINATION must specify the package notice directory")
endif()

get_filename_component(_source_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(MAKE_DIRECTORY "${NOTICE_DESTINATION}/licenses")
foreach(_notice IN ITEMS LICENSE THIRD_PARTY_NOTICES.md licenses/Lucide-LICENSE)
    if(NOT EXISTS "${_source_root}/${_notice}")
        message(FATAL_ERROR "Missing distribution notice: ${_notice}")
    endif()
    configure_file("${_source_root}/${_notice}" "${NOTICE_DESTINATION}/${_notice}" COPYONLY)
endforeach()
