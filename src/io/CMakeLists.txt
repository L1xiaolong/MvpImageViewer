add_library(mvpview_io_libraw_adapter STATIC
    camera_raw_decoder.h
    camera_raw_decoder.cpp
)
target_include_directories(mvpview_io_libraw_adapter PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mvpview_io_libraw_adapter PUBLIC mvpview_core Qt6::Core Qt6::Gui)
target_compile_definitions(mvpview_io_libraw_adapter PRIVATE
    MVPVIEW_HAS_LIBRAW=$<BOOL:${MVPVIEW_HAS_LIBRAW}>
)
if(MVPVIEW_HAS_LIBRAW)
    target_link_libraries(mvpview_io_libraw_adapter PRIVATE LibRaw::LibRaw)
endif()
mvpview_enable_warnings(mvpview_io_libraw_adapter)

add_library(mvpview_io_exiv2_adapter STATIC
    metadata_exiv2_lock.h
    metadata_reader.h
    metadata_reader.cpp
    metadata_writer.h
    metadata_writer.cpp
)
target_include_directories(mvpview_io_exiv2_adapter PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mvpview_io_exiv2_adapter PUBLIC mvpview_core Qt6::Core Qt6::Gui)
target_compile_definitions(mvpview_io_exiv2_adapter PRIVATE
    MVPVIEW_HAS_EXIV2=$<BOOL:${MVPVIEW_HAS_EXIV2}>
)
if(MVPVIEW_HAS_EXIV2)
    target_link_libraries(mvpview_io_exiv2_adapter PRIVATE Exiv2::exiv2lib)
endif()
mvpview_enable_warnings(mvpview_io_exiv2_adapter)

add_library(mvpview_io_lcms2_adapter STATIC
    encoded_color_management.h
    encoded_color_management.cpp
)
target_include_directories(mvpview_io_lcms2_adapter PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mvpview_io_lcms2_adapter PUBLIC mvpview_core Qt6::Core Qt6::Gui)
target_compile_definitions(mvpview_io_lcms2_adapter PRIVATE
    MVPVIEW_HAS_LCMS2=$<BOOL:${MVPVIEW_HAS_LCMS2}>
)
if(MVPVIEW_HAS_LCMS2)
    target_link_libraries(mvpview_io_lcms2_adapter PRIVATE LCMS2::LCMS2)
endif()
mvpview_enable_warnings(mvpview_io_lcms2_adapter)

qt_add_library(mvpview_io STATIC
    image_decoder.h
    image_decoder_registry.h
    image_decoder_registry.cpp
    default_image_decoder.h
    default_image_decoder.cpp
    supported_image_formats.h
    supported_image_formats.cpp
    camera_raw_decoder.h
    raw_image_decoder.h
    raw_image_decoder.cpp
    raw_preset_store.h
    raw_preset_store.cpp
    qt_image_decoder.h
    qt_image_decoder.cpp
    metadata_reader.h
    encoded_color_management.h
    directory_scanner.h
    directory_scanner.cpp
    image_loader.h
    image_loader.cpp
    image_transformer.h
    image_transformer.cpp
    single_file_rename.h
    single_file_rename.cpp
    drop_copy_operation.h
    drop_copy_operation.cpp
    thumbnail_disk_cache.h
    thumbnail_disk_cache.cpp
)

target_include_directories(mvpview_io PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Optional third-party implementations are separate static adapters. Their library headers and
# compile options do not leak into the generic IO sources or UI target.
target_link_libraries(mvpview_io
    PUBLIC mvpview_core mvpview_diagnostics Qt6::Core Qt6::Gui
    PRIVATE
        mvpview_io_libraw_adapter
        mvpview_io_exiv2_adapter
        mvpview_io_lcms2_adapter
)
mvpview_enable_warnings(mvpview_io)
