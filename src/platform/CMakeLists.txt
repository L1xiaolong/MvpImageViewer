qt_add_library(mvpview_platform STATIC
    full_screen_presentation_controller.h
    full_screen_presentation_controller.cpp
    platform_services.h
    platform_services.cpp
    platform_shortcuts.h
    platform_shortcuts.cpp
)

if(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(full_screen_presentation_controller.cpp
        PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(mvpview_platform PRIVATE "-framework AppKit")
endif()

if(WIN32)
    target_link_libraries(mvpview_platform PRIVATE user32)
endif()

target_include_directories(mvpview_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mvpview_platform PUBLIC Qt6::Core Qt6::Gui)
mvpview_enable_warnings(mvpview_platform)
