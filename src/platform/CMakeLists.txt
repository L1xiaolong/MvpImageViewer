qt_add_library(ispview_platform STATIC
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
    target_link_libraries(ispview_platform PRIVATE "-framework AppKit")
endif()

target_include_directories(ispview_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ispview_platform PUBLIC Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_platform)
