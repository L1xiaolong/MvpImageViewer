qt_add_library(ispview_core STATIC
    color_conversion.h
    color_conversion.cpp
    display_color_space.h
    display_color_space.cpp
    display_histogram.h
    display_histogram.cpp
    comparison_pixel_probe.h
    comparison_pixel_probe.cpp
    image_types.h
    raw_image_parameters.h
    raw_image_parameters.cpp
    raw_plane_access.h
    raw_plane_access.cpp
    raw_plane_histogram.h
    raw_plane_histogram.cpp
    view_state.h
    view_state.cpp
    sync_group.h
    sync_group.cpp
    weighted_lru_cache.h
)

target_include_directories(ispview_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ispview_core PUBLIC Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_core)
