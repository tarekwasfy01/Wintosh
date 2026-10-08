if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreTextLib.vcxproj`.
        CTFont.mm
        CTFontCollection.mm
        CTFontDescriptor.mm
        CTFontManager.mm
        CTFrame.mm
        CTFramesetter.mm
        CTGlyphInfo.mm
        CTLine.mm
        CTParagraphStyle.mm
        CTRun.mm
        CTRunDelegate.mm
        CTStringAttributes.mm
        CTTextTab.mm
        CTTypesetter.mm
        CTUtilities.mm
        DWriteWrapper_CGPath.mm
        DWriteWrapper_CTFont.mm
        DWriteWrapper_CoreText.mm)

    add_library (CoreText SHARED ${SOURCE_FILES})
    woc_framework (CoreText)

    target_include_directories (CoreText PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreText PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreTextLib.vcxproj`.
        CORETEXT_IMPEXP=)

    target_compile_options (CoreText PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreText PRIVATE
        ${WOCFX_LIBS}
        # From `CoreText.vcxproj`.
        CoreGraphics
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreText/dll/\
CoreText.def")
endif (NOT CL_COMPILER)
