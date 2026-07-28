cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PACKAGE_ROOT OR PACKAGE_ROOT STREQUAL "")
    message(FATAL_ERROR "PACKAGE_ROOT must point to a staged application package")
endif()
if(NOT DEFINED PACKAGE_PLATFORM)
    message(FATAL_ERROR "PACKAGE_PLATFORM must be macos or windows")
endif()

file(REAL_PATH "${PACKAGE_ROOT}" package_root)
if(PACKAGE_PLATFORM STREQUAL "macos")
    set(contents_root "${package_root}/Contents")
    set(qml_root "${contents_root}/Resources/qml")
    set(plugin_root "${contents_root}/PlugIns")
    set(application_binary "${contents_root}/MacOS/ISPImageViewer")
    set(plugin_prefix "lib")
    set(plugin_suffix ".dylib")
elseif(PACKAGE_PLATFORM STREQUAL "windows")
    set(contents_root "${package_root}")
    set(qml_root "${package_root}/qml")
    set(plugin_root "${package_root}")
    set(application_binary "${package_root}/ISPImageViewer.exe")
    set(plugin_prefix "")
    set(plugin_suffix ".dll")
else()
    message(FATAL_ERROR "Unsupported PACKAGE_PLATFORM: ${PACKAGE_PLATFORM}")
endif()

if(NOT EXISTS "${application_binary}")
    message(FATAL_ERROR "Refusing to prune an invalid package: ${application_binary} is missing")
endif()
if(NOT IS_DIRECTORY "${qml_root}" OR NOT IS_DIRECTORY "${plugin_root}")
    message(FATAL_ERROR "Refusing to prune a package without deployed Qt QML and plugins")
endif()

function(remove_paths)
    foreach(relative_path IN LISTS ARGN)
        set(candidate "${contents_root}/${relative_path}")
        if(EXISTS "${candidate}" OR IS_SYMLINK "${candidate}")
            file(REMOVE_RECURSE "${candidate}")
        endif()
    endforeach()
endfunction()

function(remove_qml_paths)
    foreach(relative_path IN LISTS ARGN)
        set(candidate "${qml_root}/${relative_path}")
        if(EXISTS "${candidate}" OR IS_SYMLINK "${candidate}")
            file(REMOVE_RECURSE "${candidate}")
        endif()
    endforeach()
endfunction()

function(remove_plugins plugin_type)
    foreach(plugin_name IN LISTS ARGN)
        set(candidate
            "${plugin_root}/${plugin_type}/${plugin_prefix}${plugin_name}${plugin_suffix}")
        if(EXISTS "${candidate}" OR IS_SYMLINK "${candidate}")
            file(REMOVE "${candidate}")
        endif()
    endforeach()
endfunction()

# ISP Image Viewer explicitly selects Qt Quick Controls Basic before the QML engine starts.
# Shipping every alternative style adds several megabytes and can also pull GPL-only modules
# into an otherwise LGPL deployment. Keep Basic, Templates, impl, Shapes and Dialogs.
remove_qml_paths(
    "QtQuick/Controls/designer"
    "QtQuick/Controls/FluentWinUI3"
    "QtQuick/Controls/Fusion"
    "QtQuick/Controls/Imagine"
    "QtQuick/Controls/Material"
    "QtQuick/Controls/Universal"
    "QtQuick/Controls/iOS"
    "QtQuick/Controls/macOS"
    "QtQuick/NativeStyle"
    "QtQuick/Dialogs/quickimpl/qml/+Fusion"
    "QtQuick/Dialogs/quickimpl/qml/+Imagine"
    "QtQuick/Dialogs/quickimpl/qml/+Material"
    "QtQuick/Dialogs/quickimpl/qml/+Universal"
)

# These QML modules are not imported by production QML or used by its Basic style dependency
# closure. Qt deployment tools discover them as optional/soft imports and otherwise copy them.
remove_qml_paths(
    "QtQml/StateMachine"
    "QtQml/XmlListModel"
    "QtQuick/Effects"
    "QtQuick/LocalStorage"
    "QtQuick/Particles"
    "QtQuick/Pdf"
    "QtQuick/Scene2D"
    "QtQuick/Scene3D"
    "QtQuick/Timeline"
    "QtQuick/VectorImage"
    "QtQuick/VirtualKeyboard"
    "QtQuick/tooling"
)

if(PACKAGE_PLATFORM STREQUAL "macos")
    remove_plugins("quick"
        effectsplugin
        particlesplugin
        pdfquickplugin
        qmllocalstorageplugin
        qmlxmllistmodelplugin
        qtqmlstatemachineplugin
        qquickvectorimageplugin
        qtquickcontrols2fluentwinui3styleimplplugin
        qtquickcontrols2fluentwinui3styleplugin
        qtquickcontrols2fusionstyleimplplugin
        qtquickcontrols2fusionstyleplugin
        qtquickcontrols2imaginestyleimplplugin
        qtquickcontrols2imaginestyleplugin
        qtquickcontrols2iosstyleimplplugin
        qtquickcontrols2iosstyleplugin
        qtquickcontrols2macosstyleimplplugin
        qtquickcontrols2macosstyleplugin
        qtquickcontrols2materialstyleimplplugin
        qtquickcontrols2materialstyleplugin
        qtquickcontrols2nativestyleplugin
        qtquickcontrols2universalstyleimplplugin
        qtquickcontrols2universalstyleplugin
        qtquickscene2dplugin
        qtquickscene3dplugin
        qtquicktimelineblendtreesplugin
        qtquicktimelineplugin
        quicktoolingplugin
        qtvkbbuiltinstylesplugin
        qtvkbcomponentsplugin
        qtvkbhangulplugin
        qtvkbhunspellplugin
        qtvkblayoutsplugin
        qtvkbopenwnnplugin
        qtvkbpinyinplugin
        qtvkbplugin
        qtvkbpluginsplugin
        qtvkbsettingsplugin
        qtvkbstylesplugin
        qtvkbtcimeplugin
        qtvkbthaiplugin
        virtualkeyboardplugin
    )
endif()

# The encoded-image decoder intentionally accepts JPEG and PNG only. SVG remains required by
# the application icon/image provider. Keep ICO/ICNS conservatively for native shell artwork.
remove_plugins("imageformats"
    qgif
    qjp2
    qmacheif
    qmng
    qpdf
    qtga
    qtiff
    qwbmp
    qwebp
)

# The application has no network client. These are optional backends rather than QtNetwork
# itself, which remains in the dependency closure where Qt QML requires it.
remove_paths(
    "PlugIns/networkinformation"
    "PlugIns/platforminputcontexts"
    "PlugIns/tls"
)
if(PACKAGE_PLATFORM STREQUAL "windows")
    remove_paths(
        "networkinformation"
        "platforminputcontexts"
        "tls"
    )
endif()

if(PACKAGE_PLATFORM STREQUAL "macos")
    set(framework_root "${contents_root}/Frameworks")
    file(GLOB framework_directories "${framework_root}/*.framework")
    file(GLOB framework_dylibs "${framework_root}/*.dylib")
    set(framework_binaries)
    foreach(framework_directory IN LISTS framework_directories)
        get_filename_component(framework_filename "${framework_directory}" NAME)
        string(REGEX REPLACE "\\.framework$" "" framework_name "${framework_filename}")
        set(framework_binary
            "${framework_directory}/Versions/A/${framework_name}")
        if(EXISTS "${framework_binary}")
            list(APPEND framework_binaries "${framework_binary}")
        endif()
    endforeach()
    list(APPEND framework_binaries ${framework_dylibs})

    # macdeployqt copies transitive libraries for plugins that were subsequently pruned.
    # Compute the Mach-O dependency closure from the executable and retained plugins, then
    # remove only top-level frameworks/dylibs that cannot be reached from those roots.
    file(GLOB_RECURSE retained_plugin_binaries "${plugin_root}/*.dylib")
    set(dependency_queue "${application_binary}" ${retained_plugin_binaries})
    set(reachable_binaries)
    while(dependency_queue)
        list(POP_FRONT dependency_queue binary)
        list(FIND reachable_binaries "${binary}" already_visited)
        if(NOT already_visited EQUAL -1 OR NOT EXISTS "${binary}")
            continue()
        endif()
        list(APPEND reachable_binaries "${binary}")
        execute_process(
            COMMAND otool -L "${binary}"
            RESULT_VARIABLE otool_result
            OUTPUT_VARIABLE dependency_output
            ERROR_QUIET
        )
        if(NOT otool_result EQUAL 0)
            continue()
        endif()
        string(REPLACE "\n" ";" dependency_lines "${dependency_output}")
        foreach(dependency_line IN LISTS dependency_lines)
            string(STRIP "${dependency_line}" dependency_line)
            if(NOT dependency_line MATCHES "^([^ \t]+)")
                continue()
            endif()
            set(dependency "${CMAKE_MATCH_1}")
            set(candidate)
            if(dependency MATCHES
               "([^/]+\\.framework)/Versions/[^/]+/([^/ \t]+)")
                set(candidate
                    "${framework_root}/${CMAKE_MATCH_1}/Versions/A/${CMAKE_MATCH_2}")
            elseif(dependency MATCHES "([^/ \t]+\\.dylib)")
                get_filename_component(dependency_name "${dependency}" NAME)
                set(candidate "${framework_root}/${dependency_name}")
            endif()
            if(candidate)
                list(FIND framework_binaries "${candidate}" packaged_dependency)
                list(FIND reachable_binaries "${candidate}" dependency_visited)
                if(NOT packaged_dependency EQUAL -1 AND dependency_visited EQUAL -1)
                    list(APPEND dependency_queue "${candidate}")
                endif()
            endif()
        endforeach()
    endwhile()

    foreach(framework_binary IN LISTS framework_binaries)
        list(FIND reachable_binaries "${framework_binary}" is_reachable)
        if(NOT is_reachable EQUAL -1)
            continue()
        endif()
        if(framework_binary MATCHES
           "^${framework_root}/([^/]+\\.framework)/")
            set(unused_component "${framework_root}/${CMAKE_MATCH_1}")
        else()
            set(unused_component "${framework_binary}")
        endif()
        if(EXISTS "${unused_component}" OR IS_SYMLINK "${unused_component}")
            get_filename_component(unused_name "${unused_component}" NAME)
            message(STATUS "Removing unreachable runtime dependency: ${unused_name}")
            file(REMOVE_RECURSE "${unused_component}")
        endif()
    endforeach()

    # Framework headers and linker metadata are development artifacts. They are never read by
    # the packaged process and some Homebrew frameworks retain them when copied with ditto.
    file(GLOB retained_framework_directories "${framework_root}/*.framework")
    foreach(framework_directory IN LISTS retained_framework_directories)
        foreach(header_directory
                "${framework_directory}/Headers"
                "${framework_directory}/Versions/A/Headers"
                "${framework_directory}/Versions/Current/Headers")
            if(IS_DIRECTORY "${header_directory}" OR IS_SYMLINK "${header_directory}")
                file(REMOVE_RECURSE "${header_directory}")
            endif()
        endforeach()
        file(GLOB framework_linker_metadata
            "${framework_directory}/*.prl"
            "${framework_directory}/Versions/A/Resources/*.prl"
            "${framework_directory}/Versions/Current/Resources/*.prl")
        if(framework_linker_metadata)
            file(REMOVE ${framework_linker_metadata})
        endif()
    endforeach()
endif()

set(required_paths
    "${qml_root}/QtQuick/qmldir"
    "${qml_root}/QtQuick/Controls/qmldir"
    "${qml_root}/QtQuick/Controls/Basic/qmldir"
    "${qml_root}/QtQuick/Dialogs/qmldir"
    "${qml_root}/QtQuick/Layouts/qmldir"
)
if(PACKAGE_PLATFORM STREQUAL "macos")
    list(APPEND required_paths
        "${plugin_root}/platforms/libqcocoa.dylib"
        "${plugin_root}/imageformats/libqjpeg.dylib"
        "${plugin_root}/imageformats/libqsvg.dylib"
    )
else()
    list(APPEND required_paths
        "${plugin_root}/platforms/qwindows.dll"
        "${plugin_root}/imageformats/qjpeg.dll"
        "${plugin_root}/imageformats/qsvg.dll"
    )
endif()
foreach(required_path IN LISTS required_paths)
    if(NOT EXISTS "${required_path}" AND NOT IS_SYMLINK "${required_path}")
        message(FATAL_ERROR "Required Qt runtime component was not deployed: ${required_path}")
    endif()
endforeach()

file(GLOB_RECURSE packaged_components LIST_DIRECTORIES TRUE "${contents_root}/*")
set(forbidden_components)
foreach(packaged_component IN LISTS packaged_components)
    get_filename_component(component_name "${packaged_component}" NAME)
    string(TOLOWER "${component_name}" component_name_lower)
    if(component_name_lower MATCHES
       "virtualkeyboard|quicktimeline|quickscene3d|pdfquick")
        list(APPEND forbidden_components "${packaged_component}")
    endif()
endforeach()
if(forbidden_components)
    list(JOIN forbidden_components "\n  " forbidden_list)
    message(FATAL_ERROR "Forbidden unused/GPL-only Qt components remain:\n  ${forbidden_list}")
endif()

message(STATUS "Pruned optional Qt runtime components for ${PACKAGE_PLATFORM}")
