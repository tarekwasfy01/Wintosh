if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreVideoLib.vcxproj`.
        CVBuffer.mm
        CVImageBuffer.mm
        CVMetalTexture.mm
        CVMetalTextureCache.mm
        CVOpenGLESTexture.mm
        CVOpenGLESTextureCache.mm
        CVPixelBuffer.mm
        CVPixelBufferPool.mm
        CVPixelFormatDescription.mm
        CVTime.mm)

    add_library (CoreVideo SHARED ${SOURCE_FILES})
    woc_framework (CoreVideo)

    target_include_directories (CoreVideo PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreVideo PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreVideoLib.vcxproj`.
        COREVIDEO_IMPEXP=)

    target_compile_options (CoreVideo PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreVideo PRIVATE
        ${WOCFX_LIBS}
        # From `CoreVideo.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreVideo/dll/\
CoreVideo.def")
endif (NOT CL_COMPILER)
