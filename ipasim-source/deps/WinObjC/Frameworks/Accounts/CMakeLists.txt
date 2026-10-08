if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AccountsLib.vcxproj`.
        ACAccount.mm
        ACAccountCredential.mm
        ACAccountStore.mm
        ACAccountType.mm)

    add_library (Accounts SHARED ${SOURCE_FILES})
    woc_framework (Accounts)

    target_include_directories (Accounts PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Accounts PRIVATE
        ${WINOBJC_DEFS}
        # From `AccountsLib.vcxproj`.
        STARBOARD_PORT=1
        ACCOUNTS_IMPEXP=)

    target_compile_options (Accounts PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `AccountsLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (Accounts PRIVATE
        ${WOCFX_LIBS}
        # From `Accounts.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Accounts/dll/\
Accounts.def")
endif (NOT CL_COMPILER)
