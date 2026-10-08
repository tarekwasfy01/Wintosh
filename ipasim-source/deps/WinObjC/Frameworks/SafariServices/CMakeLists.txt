if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `SafariServicesLib.vcxproj`.
        SFSafariViewController.mm
        SFSafariOAuthViewController.mm
        SFSafariWebViewController.mm)
        # TODO: Add `dllmain.cpp`?

    add_library (SafariServices SHARED ${SOURCE_FILES})
    woc_framework (SafariServices)

    target_include_directories (SafariServices PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (SafariServices PRIVATE
        ${WINOBJC_DEFS}
        # From `SafariServicesLib.vcxproj`.
        SAFARISERVICES_IMPEXP=)

    target_compile_options (SafariServices PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `SafariServicesLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (SafariServices PRIVATE
        ${WOCFX_LIBS}
        # From `SafariServices.vcxproj`.
        CoreGraphics
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/SafariServices/dll/\
SafariServices.def")
endif (NOT CL_COMPILER)
