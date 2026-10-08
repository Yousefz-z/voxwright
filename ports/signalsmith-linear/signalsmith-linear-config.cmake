get_filename_component(_ssl_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET signalsmith::linear)
    add_library(signalsmith::linear INTERFACE IMPORTED)
    set_target_properties(signalsmith::linear PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_ssl_prefix}/include")
    if(APPLE)
        set_property(TARGET signalsmith::linear APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES "-framework Accelerate")
        set_property(TARGET signalsmith::linear APPEND PROPERTY
            INTERFACE_COMPILE_DEFINITIONS SIGNALSMITH_USE_ACCELERATE ACCELERATE_NEW_LAPACK)
    endif()
endif()
unset(_ssl_prefix)
