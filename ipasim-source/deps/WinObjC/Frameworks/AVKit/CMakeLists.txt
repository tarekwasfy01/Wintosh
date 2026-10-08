if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AVKitLib.vcxproj`.
        AVPictureInPictureController.mm
        AVPlayerViewController.mm)

    add_library (AVKit SHARED ${SOURCE_FILES})
    woc_framework (AVKit)

    target_include_directories (AVKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (AVKit PRIVATE
        ${WINOBJC_DEFS}
        # From `AVKitLib.vcxproj`.
        AVKIT_IMPEXP=)

    target_compile_options (AVKit PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `AVKitLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (AVKit PRIVATE
        ${WOCFX_LIBS}
        # From `AVKit.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AVKit/dll/AVKit.def")
endif (NOT CL_COMPILER)
