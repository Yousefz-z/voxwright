# VOX_SANITIZE="address;undefined" or "thread". Applied to first-party targets only.

if(VOX_SANITIZE AND MSVC)
    if(NOT VOX_SANITIZE STREQUAL "address")
        message(FATAL_ERROR "MSVC supports only VOX_SANITIZE=address (got '${VOX_SANITIZE}').")
    endif()
endif()

if("thread" IN_LIST VOX_SANITIZE AND "address" IN_LIST VOX_SANITIZE)
    message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer. "
        "Use VOX_SANITIZE='address;undefined' or VOX_SANITIZE='thread'.")
endif()

function(vox_apply_sanitizers target)
    if(NOT VOX_SANITIZE)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address)
        return()
    endif()
    list(JOIN VOX_SANITIZE "," _list)
    set(_flags -fsanitize=${_list} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_compile_options(${target} PRIVATE ${_flags})
    target_link_options(${target} PRIVATE ${_flags})
endfunction()
