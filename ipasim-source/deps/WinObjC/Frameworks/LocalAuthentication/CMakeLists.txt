if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `LocalAuthenticationLib.vcxproj`.
        LAContext.mm)

    add_library (LocalAuthentication SHARED ${SOURCE_FILES})
    woc_framework (LocalAuthentication)

    target_include_directories (LocalAuthentication PRIVATE
        ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (LocalAuthentication PRIVATE
        ${WINOBJC_DEFS}
        # From `LocalAuthenticationLib.vcxproj`.
        LOCALAUTHENTICATION_IMPEXP=)

    target_compile_options (LocalAuthentication PRIVATE
        ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (LocalAuthentication PRIVATE
        ${WOCFX_LIBS}
        # From `LocalAuthentication.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/LocalAuthentication/\
dll/LocalAuthentication.def")
endif (NOT CL_COMPILER)
