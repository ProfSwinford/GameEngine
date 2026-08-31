# =============================================================================
#  One place that decides what the compiler complains about.
#
#  A compiler warning is the compiler telling you about a bug before it
#  happens. A warning you have learned to scroll past is a bug you have decided
#  to ship, so this project turns a generous set of them on everywhere.
#
#  The `strict` build preset additionally turns warnings into ERRORS, which
#  stops the build. Run it before handing work in.
# =============================================================================
function(engine_set_warnings target)
    if(MSVC)
        # /W4 is Microsoft's "tell me a lot" level. /permissive- makes the
        # compiler follow the C++ standard strictly rather than accepting
        # older Microsoft-only syntax, which keeps the code portable.
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow              # a local variable hiding a member of the same name
            -Wnon-virtual-dtor    # a base class that cannot be safely deleted
            -Wold-style-cast      # C-style casts, which hide what they are doing
            -Wcast-align
            -Wunused
            -Woverloaded-virtual  # a function that hides one it meant to override
            -Wnull-dereference
            -Wdouble-promotion)   # a float quietly widened to a double
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
