if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AutoLayoutLib.vcxproj`.
        AutoLayout.mm)

    add_library (AutoLayout SHARED ${SOURCE_FILES})
    woc_framework (AutoLayout)

    target_include_directories (AutoLayout PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (AutoLayout PRIVATE
        ${WINOBJC_DEFS}
        # From `AutoLayoutLib.vcxproj`.
        $<$<CONFIG:Debug>:HAVE_CONFIG_H>
        $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)

    target_compile_options (AutoLayout PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (AutoLayout PRIVATE
        ${WOCFX_LIBS}
        UIKit # See #11.
        # From `AutoLayout.vcxproj`.
        cassowary
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AutoLayout/dll/\
AutoLayout.def")
endif (NOT CL_COMPILER)
