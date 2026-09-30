qt_add_library(mvpview_browser_backend STATIC
    thumbnail_model.h
    thumbnail_model.cpp
    local_file_drop.h
    local_file_drop.cpp
    file_clipboard.h
    file_clipboard.cpp
    thumbnail_filter_proxy_model.h
    thumbnail_filter_proxy_model.cpp
)

target_include_directories(mvpview_browser_backend PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mvpview_browser_backend
    PUBLIC mvpview_core mvpview_io mvpview_platform Qt6::Gui
)
mvpview_enable_warnings(mvpview_browser_backend)
