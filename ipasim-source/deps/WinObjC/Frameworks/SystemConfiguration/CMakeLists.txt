if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `SystemConfigurationLib.vcxproj`.
        SCNetworkReachability.mm)

    add_library (SystemConfiguration SHARED ${SOURCE_FILES})
    woc_framework (SystemConfiguration)

    target_include_directories (SystemConfiguration PRIVATE
        ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (SystemConfiguration PRIVATE
        ${WINOBJC_DEFS}
        # From `SystemConfigurationLib.vcxproj`.
        STARBOARD_PORT=1
        SC_IMPEXP=)

    target_compile_options (SystemConfiguration PRIVATE
        ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (SystemConfiguration PRIVATE
        ${WOCFX_LIBS}
        # From `SystemConfiguration.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/SystemConfiguration/\
dll/SystemConfiguration.def")
endif (NOT CL_COMPILER)
