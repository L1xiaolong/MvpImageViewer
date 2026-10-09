qt_add_executable(mvpview_performance_tests performance_tests.cpp ../src/qml/thumbnail_image_provider.cpp)
target_link_libraries(mvpview_performance_tests PRIVATE mvpview_io mvpview_browser_backend Qt6::Core Qt6::Gui Qt6::Quick)
add_test(NAME performance_regressions COMMAND mvpview_performance_tests)
set_tests_properties(performance_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
