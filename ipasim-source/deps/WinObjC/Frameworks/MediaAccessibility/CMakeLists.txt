if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `MediaAccessibilityLib.vcxproj`.
        MAAppearance.mm
        MAConstants.mm)

    add_library (MediaAccessibility SHARED ${SOURCE_FILES})
    woc_framework (MediaAccessibility)

    target_include_directories (MediaAccessibility PRIVATE
        ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (MediaAccessibility PRIVATE
        ${WINOBJC_DEFS}
        # From `MediaAccessibilityLib.vcxproj`.
        MEDIAACCESSIBILITY_IMPEXP=)

    target_compile_options (MediaAccessibility PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (MediaAccessibility PRIVATE
        ${WOCFX_LIBS}
        # From `MediaAccessibility.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/MediaAccessibility/\
dll/MediaAccessibility.def")
endif (NOT CL_COMPILER)
