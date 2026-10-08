if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `MetalLib.vcxproj`.
        MetalFunctions.mm
        MTLArgument.mm
        MTLArrayType.mm
        MTLCommandBuffer.mm
        MTLCompileOptions.mm
        MTLComputePipelineDescriptor.mm
        MTLComputePipelineReflection.mm
        MTLDepthStencilDescriptor.mm
        MTLLibrary.mm
        MTLRenderPassAttachmentDescriptor.mm
        MTLRenderPassColorAttachmentDescriptor.mm
        MTLRenderPassColorAttachmentDescriptorArray.mm
        MTLRenderPassDepthAttachmentDescriptor.mm
        MTLRenderPassDescriptor.mm
        MTLRenderPassStencilAttachmentDescriptor.mm
        MTLRenderPipelineColorAttachmentDescriptor.mm
        MTLRenderPipelineColorAttachmentDescriptorArray.mm
        MTLRenderPipelineDescriptor.mm
        MTLRenderPipelineReflection.mm
        MTLSamplerDescriptor.mm
        MTLStencilDescriptor.mm
        MTLStructMember.mm
        MTLStructType.mm
        MTLTextureDescriptor.mm
        MTLVertexAttribute.mm
        MTLVertexAttributeDescriptor.mm
        MTLVertexAttributeDescriptorArray.mm
        MTLVertexBufferLayoutDescriptor.mm
        MTLVertexBufferLayoutDescriptorArray.mm
        MTLVertexDescriptor.mm)

    add_library (Metal SHARED ${SOURCE_FILES})
    woc_framework (Metal)

    target_include_directories (Metal PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Metal PRIVATE
        ${WINOBJC_DEFS}
        # From `MetalLib.vcxproj`.
        METAL_IMPEXP=)

    target_compile_options (Metal PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (Metal PRIVATE
        ${WOCFX_LIBS}
        # From `Metal.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Metal/dll/Metal.def")
endif (NOT CL_COMPILER)
