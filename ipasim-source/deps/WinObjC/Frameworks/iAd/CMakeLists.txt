if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `iAdLib.vcxproj`.
        ADBannerView.mm
        ADClient.mm
        ADInterstitialAd.mm
        UIViewController.mm)

    add_library (iAd SHARED ${SOURCE_FILES})
    woc_framework (iAd)

    target_include_directories (iAd PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (iAd PRIVATE
        ${WINOBJC_DEFS}
        # From `iAdLib.vcxproj`.
        IAD_IMPEXP=)

    target_compile_options (iAd PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (iAd PRIVATE
        ${WOCFX_LIBS}
        # From `iAd.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/iAd/dll/\
iAd.def")
endif (NOT CL_COMPILER)
