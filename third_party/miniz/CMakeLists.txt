# miniz 3.0.2, https://github.com/richgel999/miniz/tree/3.0.2
add_library(ispview_miniz STATIC miniz.c miniz_tdef.c miniz_tinfl.c miniz_zip.c)
target_include_directories(ispview_miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(ispview_miniz PROPERTIES AUTOMOC OFF)
