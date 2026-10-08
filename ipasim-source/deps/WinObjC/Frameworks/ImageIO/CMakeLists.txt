if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `ImageIOLib.vcxproj`.
        CGImageDestination.mm
        CGImageProperties.mm
        CGImageSource.mm)

    add_library (ImageIO SHARED ${SOURCE_FILES})
    woc_framework (ImageIO)

    target_include_directories (ImageIO PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (ImageIO PRIVATE
        ${WINOBJC_DEFS}
        # From `ImageIOLib.vcxproj`.
        IMAGEIO_IMPEXP=)

    target_compile_options (ImageIO PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (ImageIO PRIVATE
        ${WOCFX_LIBS}
        # From `ImageIO.vcxproj`.
        CoreGraphics
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/ImageIO/dll/\
ImageIO.def")
endif (NOT CL_COMPILER)
