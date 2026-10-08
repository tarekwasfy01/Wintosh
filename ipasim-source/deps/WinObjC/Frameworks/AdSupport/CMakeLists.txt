if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AdSupportLib.vcxproj`.
        ASIdentifierManager.mm)

    add_library (AdSupport SHARED ${SOURCE_FILES})
    woc_framework (AdSupport)

    target_include_directories (AdSupport PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (AdSupport PRIVATE
        ${WINOBJC_DEFS}
        # From `AdSupportLib.vcxproj`.
        ADSUPPORT_IMPEXP=)

    target_compile_options (AdSupport PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (AdSupport PRIVATE
        ${WOCFX_LIBS}
        # From `AdSupport.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AdSupport/dll/\
AdSupport.def")
endif (NOT CL_COMPILER)
