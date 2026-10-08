if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreTelephonyLib.vcxproj`.
        CTCall.mm
        CTCallCenter.mm
        CTCarrier.mm
        CTSubscriber.mm
        CTTelephonyNetworkInfo.mm)

    add_library (CoreTelephony SHARED ${SOURCE_FILES})
    woc_framework (CoreTelephony)

    target_include_directories (CoreTelephony PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreTelephony PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreTelephonyLib.vcxproj`.
        CORETELEPHONY_IMPEXP=)

    target_compile_options (CoreTelephony PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreTelephony PRIVATE
        ${WOCFX_LIBS}
        # From `CoreTelephony.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreTelephony/dll/\
CoreTelephony.def")
endif (NOT CL_COMPILER)
