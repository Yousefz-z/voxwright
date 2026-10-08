get_filename_component(_miniaudio_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET miniaudio::miniaudio)
    add_library(miniaudio::miniaudio INTERFACE IMPORTED)
    set_target_properties(miniaudio::miniaudio PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_miniaudio_prefix}/include")
endif()
unset(_miniaudio_prefix)
