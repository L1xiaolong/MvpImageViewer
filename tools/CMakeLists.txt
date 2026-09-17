qt_add_executable(ispview_raw_benchmark
    raw_decode_benchmark.cpp
)

target_link_libraries(ispview_raw_benchmark PRIVATE ispview_io Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_raw_benchmark)

qt_add_executable(ispview_histogram_benchmark
    histogram_benchmark.cpp
)

target_link_libraries(ispview_histogram_benchmark PRIVATE ispview_core Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_histogram_benchmark)

qt_add_executable(ispview_color_benchmark
    color_management_benchmark.cpp
)

target_link_libraries(ispview_color_benchmark PRIVATE ispview_io Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_color_benchmark)

qt_add_executable(ispview_sample_check
    sample_decode_check.cpp
)

target_link_libraries(ispview_sample_check PRIVATE
    ispview_io
    Qt6::Core
    Qt6::Gui
)
ispview_enable_warnings(ispview_sample_check)

qt_add_executable(ispview_browser_benchmark
    browser_benchmark.cpp
)

target_link_libraries(ispview_browser_benchmark PRIVATE
    ispview_io
    Qt6::Core
    Qt6::Gui
)
if(WIN32)
    target_link_libraries(ispview_browser_benchmark PRIVATE psapi)
endif()
ispview_enable_warnings(ispview_browser_benchmark)

qt_add_executable(ispview_diagnostics_benchmark diagnostics_benchmark.cpp)
target_link_libraries(ispview_diagnostics_benchmark PRIVATE ispview_io Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_diagnostics_benchmark)
