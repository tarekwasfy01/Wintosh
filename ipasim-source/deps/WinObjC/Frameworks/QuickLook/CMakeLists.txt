if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `QuickLookLib.vcxproj`.
        QLPreviewController.mm)

    add_library (QuickLook SHARED ${SOURCE_FILES})
    woc_framework (QuickLook)

    target_include_directories (QuickLook PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (QuickLook PRIVATE
        ${WINOBJC_DEFS}
        # From `QuickLookLib.vcxproj`.
        QUICKLOOK_IMPEXP=)

    target_compile_options (QuickLook PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (QuickLook PRIVATE
        ${WOCFX_LIBS}
        # From `QuickLook.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/QuickLook/dll/\
QuickLook.def")
endif (NOT CL_COMPILER)
