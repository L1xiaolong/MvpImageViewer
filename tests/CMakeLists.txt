add_executable(yuv_startup_regression yuv_startup_regression.cpp)
target_link_libraries(yuv_startup_regression PRIVATE mvpview_io)
mvpview_enable_warnings(yuv_startup_regression)
add_test(NAME yuv_startup_regression COMMAND yuv_startup_regression)
set_tests_properties(yuv_startup_regression PROPERTIES TIMEOUT 60)

add_executable(rhi_fallback_regression rhi_fallback_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/qml_image_canvas.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/qml_image_canvas.h)
target_link_libraries(rhi_fallback_regression PRIVATE
    mvpview_render_backend mvpview_diagnostics Qt6::Quick Qt6::GuiPrivate)
mvpview_enable_warnings(rhi_fallback_regression)
add_test(NAME rhi_fallback_regression COMMAND rhi_fallback_regression)
set_tests_properties(rhi_fallback_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=rhi")

add_executable(browse_restore_regression browse_restore_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/browse_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/thumbnail_image_provider.cpp)
target_link_libraries(browse_restore_regression PRIVATE
    mvpview_browser_backend mvpview_platform mvpview_io Qt6::Quick)
mvpview_enable_warnings(browse_restore_regression)
add_test(NAME browse_restore_regression COMMAND browse_restore_regression)
set_tests_properties(browse_restore_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(thumbnail_response_regression thumbnail_response_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/thumbnail_image_provider.cpp)
target_link_libraries(thumbnail_response_regression PRIVATE mvpview_io Qt6::Quick)
mvpview_enable_warnings(thumbnail_response_regression)
add_test(NAME thumbnail_response_regression COMMAND thumbnail_response_regression)
set_tests_properties(thumbnail_response_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
