if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `WebKitLib.vcxproj`.
        WebKitConstants.mm
        WKBackForwardList.mm
        WKBackForwardListItem.mm
        WKFrameInfo.mm
        WKNavigation.mm
        WKNavigationAction.mm
        WKNavigationResponse.mm
        WKPreferences.mm
        WKProcessPool.mm
        WKScriptMessage.mm
        WKSecurityOrigin.mm
        WKUserContentController.mm
        WKUserScript.mm
        WKWebsiteDataRecord.mm
        WKWebsiteDataStore.mm
        WKWebView.mm
        WKWebViewConfiguration.mm
        WKWindowFeatures.mm)

    add_library (WebKit SHARED ${SOURCE_FILES})
    woc_framework (WebKit)

    target_include_directories (WebKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (WebKit PRIVATE
        ${WINOBJC_DEFS}
        # From `WebKitLib.vcxproj`.
        WEBKIT_IMPEXP=)

    target_compile_options (WebKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (WebKit PRIVATE
        ${WOCFX_LIBS}
        # From `WebKit.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/WebKit/dll/\
WebKit.def")
endif (NOT CL_COMPILER)
