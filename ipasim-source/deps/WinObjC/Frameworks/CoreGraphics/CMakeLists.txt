if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreGraphicsLib.vcxproj`.
        CGColorSpace.mm
        CGContext.mm
        CGDataProvider.mm
        CGFont.mm
        CGGradient.mm
        CGImage.mm
        CGPath.mm
        CGPattern.mm
        CGAffineTransform.mm
        CGRect.mm
        _CGLifetimeBridgingType.mm
        CGColor.mm
        CGDataConsumer.mm
        CGFunction.mm
        CGGeometry.mm
        CGLayer.mm
        CGPDFArray.mm
        CGPDFContentStream.mm
        CGPDFContext.mm
        CGPDFDictionary.mm
        CGPDFDocument.mm
        CGPDFObject.mm
        CGPDFOperatorTable.mm
        CGPDFPage.mm
        CGPDFScanner.mm
        CGPDFStream.mm
        CGPDFString.mm
        CGShading.mm
        DWriteFontBinaryDataLoader.mm
        DWriteFontBinaryDataCollectionLoader.mm
        DWriteFontCollectionHelper.mm
        DWriteWrapper.mm
        D2DWrapper.mm
        D2DWrapper_CGPathApply.mm)

    add_library (CoreGraphics SHARED ${SOURCE_FILES})
    woc_framework (CoreGraphics)

    target_include_directories (CoreGraphics PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreGraphics PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreGraphicsLib.vcxproj`.
        STARBOARD_PORT=1
        CORETEXT_IMPEXP=
        COREGRAPHICS_IMPEXP=)

    target_compile_options (CoreGraphics PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreGraphics PRIVATE
        ${WOCFX_LIBS}
        # From `CoreGraphics.vcxproj`.
        dxguid.lib
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreGraphics/dll/\
CoreGraphics.def")
endif (NOT CL_COMPILER)
