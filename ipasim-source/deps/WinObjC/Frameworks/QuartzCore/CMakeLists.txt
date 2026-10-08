if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `QuartzCoreLib.vcxproj`.
        CATransform3D.mm
        CAAnimation.mm
        CAAnimationGroup.mm
        CABasicAnimation.mm
        CADisplayLink.mm
        CAEAGLLayer.mm
        CAEmitterCell.mm
        CAEmitterLayer.mm
        CAGradientLayer.mm
        CAKeyframeAnimation.mm
        CALayer.mm
        CAMediaTiming.mm
        CAMediaTimingFunction.mm
        CAMetalLayer.mm
        CAPropertyAnimation.mm
        CAReplicatorLayer.mm
        CARenderer.mm
        CAScrollLayer.mm
        CAShapeLayer.mm
        CATextLayer.mm
        CATiledLayer.mm
        CATransaction.mm
        CATransformLayer.mm
        CATransition.mm
        CAValueFunction.mm
        CoreAnimationFunctions.mm
        ../Starboard/Quaternion.mm)

    add_library (QuartzCore SHARED ${SOURCE_FILES})
    woc_framework (QuartzCore)

    target_include_directories (QuartzCore PRIVATE
        ${WOCFX_INCLUDE_DIRS}
        # From `QuartzCoreLib.vcxproj`.
        ../UIKit)

    target_compile_definitions (QuartzCore PRIVATE
        ${WINOBJC_DEFS}
        # From `QuartzCoreLib.vcxproj`.
        STARBOARD_PORT=1
        CA_IMPEXP=)

    target_compile_options (QuartzCore PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (QuartzCore PRIVATE
        ${WOCFX_LIBS}
        # From `QuartzCore.vcxproj`.
        CoreGraphics
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/QuartzCore/dll/\
QuartzCore.def")
endif (NOT CL_COMPILER)
