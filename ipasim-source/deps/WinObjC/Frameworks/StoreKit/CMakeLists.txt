if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `StoreKitLib.vcxproj`.
        SKDownload.mm
        SKMutablePayment.mm
        SKPayment.mm
        SKPaymentQueue.mm
        SKPaymentTransaction.mm
        SKProduct.mm
        SKProductsRequest.mm
        SKProductsResponse.mm
        SKReceiptRefreshRequest.mm
        SKRequest.mm
        SKStoreProductViewController.mm
        StoreKitConstants.mm)

    add_library (StoreKit SHARED ${SOURCE_FILES})
    woc_framework (StoreKit)

    target_include_directories (StoreKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (StoreKit PRIVATE
        ${WINOBJC_DEFS}
        # From `StoreKitLib.vcxproj`.
        STOREKIT_IMPEXP=)

    target_compile_options (StoreKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (StoreKit PRIVATE
        ${WOCFX_LIBS}
        # From `StoreKit.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/StoreKit/dll/\
StoreKit.def")
endif (NOT CL_COMPILER)
