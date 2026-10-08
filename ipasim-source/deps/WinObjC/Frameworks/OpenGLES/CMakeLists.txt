if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `OpenGLESLib.vcxproj`.
        EAGLContext.mm
        GLES.mm
        GLES1122/OpenGLES20/Attribute.mm
        GLES1122/OpenGLES20/MatrixStack.mm
        GLES1122/OpenGLES20/OpenGLES20Context.mm
        GLES1122/OpenGLES20/OpenGLES20Implementation.mm
        GLES1122/OpenGLES20/OpenGLESState.mm
        GLES1122/OpenGLES20/Shader20.mm
        GLES1122/OpenGLES20/ShaderFile.mm
        GLES1122/OpenGLES20/ShaderProgram.mm
        GLES1122/OpenGLES20/ShaderSource.mm
        GLES1122/OpenGLES20/Uniform.mm
        GLES1122/OpenGLESConfig.mm
        GLES1122/OpenGLESContext.mm
        GLES1122/OpenGLESFile.mm
        GLES1122/OpenGLESImplementation.mm
        GLES1122/OpenGLESString.mm
        GLES1122/OpenGLESUtil.mm)

    add_library (OpenGLES SHARED ${SOURCE_FILES})
    woc_framework (OpenGLES)

    target_include_directories (OpenGLES PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (OpenGLES PRIVATE
        ${WINOBJC_DEFS}
        # From `OpenGLESLib.vcxproj`.
        STARBOARD_PORT=1
        EAGL_IMPEXP=
        GL_GLEXT_PROTOTYPES)

    target_compile_options (OpenGLES PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (OpenGLES PRIVATE
        ${WOCFX_LIBS}
        # From `OpenGLES.vcxproj`.
        EGL GLESv2 # For NuGet package ANGLE.WindowsStore.
        QuartzCore
        d3d11.lib
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/OpenGLES/dll/\
OpenGLES.def")
endif (NOT CL_COMPILER)
