if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CFNetworkLib.vcxproj`.
        CFFTPStream.mm
        CFHost.mm
        CFHTTPAuthentication.mm
        CFHTTPMessage.mm
        CFHTTPStream.mm
        CFNetDiagnostics.mm
        CFNetServices.mm
        CFNetworkBase.mm
        CFProxySupport.mm
        CFSocketStream.mm)

    add_library (CFNetwork SHARED ${SOURCE_FILES})
    woc_framework (CFNetwork)

    target_include_directories (CFNetwork PRIVATE
        ${WINOBJC_INCLUDE_DIRS}
        # From `CFNetworkLib.vcxproj`.
        ../include)

    target_compile_definitions (CFNetwork PRIVATE
        ${WINOBJC_DEFS}
        # From `CFNetworkLib.vcxproj`.
        CFNETWORK_IMPEXP=
        WIL_IGNORE_OBJC_EXCEPTIONS)

    target_compile_options (CFNetwork PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CFNetwork PRIVATE
        ${WINOBJC_LIBS}
        woc-Logging
        CoreFoundation
        # From `CFNetwork.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CFNetwork/dll/\
CFNetwork.def") # For `<ModuleDefinitionFile>`.
endif (NOT CL_COMPILER)
