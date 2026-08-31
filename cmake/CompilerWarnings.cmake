# =============================================================================
#  One place that decides what the compiler complains about.
#
#  A warning is C++ telling you about the bug before it happens. A warning you
#  have learned to scroll past is a bug you have decided to ship.
#
#  Week 2 note: two of the five seeded ByteBuffer bugs were reported here for
#  free (-Wsizeof-pointer-memaccess and -Wreturn-local-addr / MSVC C4172).
#  That is the entire argument for the `strict` preset, delivered as an
#  experience rather than a claim.
# =============================================================================
function(engine_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow              # a local hiding a member: a classic C# habit
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
