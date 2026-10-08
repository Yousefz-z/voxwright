include(CMakeFindDependencyMacro)
find_dependency(signalsmith-linear CONFIG)
get_filename_component(_sss_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET signalsmith::stretch)
    add_library(signalsmith::stretch INTERFACE IMPORTED)
    set_target_properties(signalsmith::stretch PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_sss_prefix}/include"
        INTERFACE_LINK_LIBRARIES signalsmith::linear)
endif()
unset(_sss_prefix)
