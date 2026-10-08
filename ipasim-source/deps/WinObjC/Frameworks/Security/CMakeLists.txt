if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `SecurityLib.vcxproj`.
        GenericPasswordItemHandler.mm
        SecItem.mm
        SecRandom.mm
        SecPolicy.mm
        SecTrust.mm
        SecCertificate.mm
        SecIdentity.mm
        SecKey.mm
        SecureTransport.mm)

    add_library (Security SHARED ${SOURCE_FILES})
    woc_framework (Security)

    target_include_directories (Security PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Security PRIVATE
        ${WINOBJC_DEFS}
        # From `SecurityLib.vcxproj`.
        STARBOARD_PORT=1
        SECURITY_IMPEXP=)

    target_compile_options (Security PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (Security PRIVATE
        ${WOCFX_LIBS}
        # From `Security.vcxproj`.
        bcrypt.lib
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Security/dll/\
Security.def")
endif (NOT CL_COMPILER)
