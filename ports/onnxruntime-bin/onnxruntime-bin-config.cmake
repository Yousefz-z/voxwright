# Imported target for the prebuilt ONNX Runtime: onnxruntime::onnxruntime.
if(TARGET onnxruntime::onnxruntime)
    return()
endif()
get_filename_component(_ort_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(onnxruntime::onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ort_prefix}/include/onnxruntime")
if(WIN32)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_prefix}/bin/onnxruntime.dll"
        IMPORTED_IMPLIB "${_ort_prefix}/lib/onnxruntime.lib")
elseif(APPLE)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_prefix}/lib/libonnxruntime.dylib")
else()
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_prefix}/lib/libonnxruntime.so"
        IMPORTED_SONAME "libonnxruntime.so.1")
endif()
unset(_ort_prefix)
