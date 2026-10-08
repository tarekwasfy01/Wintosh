if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `GLKitLib.vcxproj`.
        GLKEffect.mm
        GLKMath.mm
        GLKTexture.mm
        GLKView.mm
        GLKViewController.mm
        GLKSkyboxEffect.mm
        GLKShader.mm
        GLKMatrixStack.mm
        # From `GLKitShaderLib.vcxproj`.
        ShaderInfo.mm
        ShaderGen.mm
        ShaderProg.mm)

    add_library (GLKit SHARED ${SOURCE_FILES})
    woc_framework (GLKit)

    target_include_directories (GLKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (GLKit PRIVATE
        ${WINOBJC_DEFS}
        # From `GLKitLib.vcxproj`, `GLKitShaderLib.vcxproj`.
        STARBORD_PORT=1
        __GLKIT_INSIDE_BUILD)

    target_compile_options (GLKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (GLKit PRIVATE
        ${WOCFX_LIBS}
        # From `GLKit.vcxproj`.
        CoreGraphics
        OpenGLES
        QuartzCore
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/GLKit/dll/\
GLKit.def")

    # From `GLKitLib.vcxproj`.
    set_source_files_properties (GLKEffect.mm GLKSkyboxEffect.mm GLKShader.mm
        PROPERTIES COMPILE_OPTIONS "${OBJC_ARC}")
endif (NOT CL_COMPILER)
