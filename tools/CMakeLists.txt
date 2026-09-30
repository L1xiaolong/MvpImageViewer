qt_add_executable(mvpview_raw_benchmark
    raw_decode_benchmark.cpp
)

target_link_libraries(mvpview_raw_benchmark PRIVATE mvpview_io Qt6::Core Qt6::Gui)
mvpview_enable_warnings(mvpview_raw_benchmark)

qt_add_executable(mvpview_histogram_benchmark
    histogram_benchmark.cpp
)

target_link_libraries(mvpview_histogram_benchmark PRIVATE mvpview_core Qt6::Core Qt6::Gui)
mvpview_enable_warnings(mvpview_histogram_benchmark)

qt_add_executable(mvpview_color_benchmark
    color_management_benchmark.cpp
)

target_link_libraries(mvpview_color_benchmark PRIVATE mvpview_io Qt6::Core Qt6::Gui)
mvpview_enable_warnings(mvpview_color_benchmark)

qt_add_executable(mvpview_sample_check
    sample_decode_check.cpp
)

target_link_libraries(mvpview_sample_check PRIVATE
    mvpview_io
    Qt6::Core
    Qt6::Gui
)
mvpview_enable_warnings(mvpview_sample_check)

qt_add_executable(mvpview_browser_benchmark
    browser_benchmark.cpp
)

target_link_libraries(mvpview_browser_benchmark PRIVATE
    mvpview_io
    Qt6::Core
    Qt6::Gui
)
if(WIN32)
    target_link_libraries(mvpview_browser_benchmark PRIVATE psapi)
endif()
mvpview_enable_warnings(mvpview_browser_benchmark)

qt_add_executable(mvpview_diagnostics_benchmark diagnostics_benchmark.cpp)
target_link_libraries(mvpview_diagnostics_benchmark PRIVATE mvpview_io Qt6::Core Qt6::Gui)
mvpview_enable_warnings(mvpview_diagnostics_benchmark)
