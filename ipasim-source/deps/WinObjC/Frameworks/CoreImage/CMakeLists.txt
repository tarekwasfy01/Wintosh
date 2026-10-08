if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreImageLib.vcxproj`.
        CIColor.mm
        CIImage.mm
        CIColorKernel.mm
        CIDetector.mm
        CIFaceFeature.mm
        CIFeature.mm
        CIFilter.mm
        CIFilterShape.mm
        CIImageAccumulator.mm
        CIImageProvider.mm
        CIKernel.mm
        CIQRCodeFeature.mm
        CIRectangleFeature.mm
        CISampler.mm
        CITextFeature.mm
        CIVector.mm
        CIWarpKernel.mm
        CIContext.mm)

    add_library (CoreImage SHARED ${SOURCE_FILES})
    woc_framework (CoreImage)

    target_include_directories (CoreImage PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreImage PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreImageLib.vcxproj`.
        COREIMAGE_IMPEXP=)

    target_compile_options (CoreImage PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreImage PRIVATE
        ${WOCFX_LIBS}
        # From `CoreImageLib.vcxproj`.
        UIKit
        # From `CoreImage.vcxproj`.
        CoreGraphics
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreImage/dll/\
CoreImage.def")

    # From `CoreImageLib.vcxproj`.
    set_source_files_properties (CIContext.mm PROPERTIES
        COMPILE_OPTIONS "${OBJC_ARC}")
endif (NOT CL_COMPILER)
