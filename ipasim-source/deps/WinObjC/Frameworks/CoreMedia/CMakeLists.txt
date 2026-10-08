if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreMediaLib.vcxproj`.
        CMAttachment.mm
        CMBlockBuffer.mm
        CMBufferQueue.mm
        CMFormatDescription.mm
        CMSampleBuffer.mm
        CMSync.mm
        CMTextMarkup.mm
        CMTime.mm
        CMTimeRange.mm)

    add_library (CoreMedia SHARED ${SOURCE_FILES})
    woc_framework (CoreMedia)

    target_include_directories (CoreMedia PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreMedia PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreMediaLib.vcxproj`.
        STARBOARD_PORT=1
        COREMEDIA_IMPEXP=)

    target_compile_options (CoreMedia PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `CoreMediaLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (CoreMedia PRIVATE
        ${WOCFX_LIBS}
        # From `CoreMedia.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreMedia/dll/\
CoreMedia.def")
endif (NOT CL_COMPILER)
