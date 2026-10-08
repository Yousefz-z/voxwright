# Script mode: cmake -DOUTPUT=<file.cpp> -DHEADER=<include> -DFUNCTION=<name>
#                    -DFILES="a;b;c" -P EmbedTextFiles.cmake
# Writes a C++ source exposing the files as raw string literals through
# `std::span<const vox::plugins::EmbeddedText> FUNCTION() noexcept`.

if(NOT OUTPUT OR NOT FUNCTION OR NOT HEADER)
    message(FATAL_ERROR "EmbedTextFiles.cmake needs OUTPUT, HEADER, FUNCTION, and FILES.")
endif()

list(LENGTH FILES count)
set(body "")
foreach(path IN LISTS FILES)
    get_filename_component(name "${path}" NAME)
    file(READ "${path}" content)
    string(FIND "${content}" ")voxtext\"" clash)
    if(NOT clash EQUAL -1)
        message(FATAL_ERROR "${path} contains the raw string terminator )voxtext\"")
    endif()
    string(APPEND body "    EmbeddedText{\"${name}\", R\"voxtext(${content})voxtext\"},\n")
endforeach()

set(source "// Built by cmake/EmbedTextFiles.cmake from plugins/voices. Do not edit.\n")
string(APPEND source "#include \"${HEADER}\"\n\n#include <array>\n\n")
string(APPEND source "namespace vox::plugins {\nnamespace {\n\n")
string(APPEND source "constexpr std::array<EmbeddedText, ${count}> kFiles{\n${body}};\n\n")
string(APPEND source "} // namespace\n\n")
string(APPEND source "std::span<const EmbeddedText> ${FUNCTION}() noexcept { return kFiles; }\n\n")
string(APPEND source "} // namespace vox::plugins\n")

# Only touch the output when the content changes, to avoid needless rebuilds.
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
    if(previous STREQUAL source)
        return()
    endif()
endif()
file(WRITE "${OUTPUT}" "${source}")
