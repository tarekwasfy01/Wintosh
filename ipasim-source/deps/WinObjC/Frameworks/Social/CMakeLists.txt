if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `SocialLib.vcxproj`.
        SLComposeServiceViewController.mm
        SLComposeSheetConfigurationItem.mm
        SLComposeViewController.mm
        _SLFBComposeViewController.mm
        SLRequest.mm)

    add_objcuwp_libs ()

    add_library (Social SHARED ${SOURCE_FILES})
    woc_framework (Social)

    target_include_directories (Social PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Social PRIVATE
        ${WINOBJC_DEFS}
        # From `SocialLib.vcxproj`.
        SOCIAL_IMPEXP=)

    target_compile_options (Social PRIVATE ${WINOBJC_CLANG_OPTIONS})

    # See #9.
    add_library (Social.Xaml SHARED IMPORTED)
    set_target_properties (Social.Xaml PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/../Social.Xaml/prebuilt/\
${CMAKE_BUILD_TYPE}/Socal.Xaml.dll"
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/../Social.Xaml/prebuilt/\
${CMAKE_BUILD_TYPE}/Social.Xaml.lib")

    target_link_libraries (Social PRIVATE
        ${WOCFX_LIBS}
        # From `Social.vcxproj`.
        QuartzCore
        UIKit
        Security
        Social.Xaml
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Social/dll/\
Social.def")
endif (NOT CL_COMPILER)
