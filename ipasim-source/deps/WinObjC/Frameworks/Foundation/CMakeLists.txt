if (CL_COMPILER)
    set (SOURCE_FILES
        NSException.cl.cpp)

    add_library (Foundation STATIC ${SOURCE_FILES})

else (CL_COMPILER)
    set (SOURCE_FILES
        # From `FoundationLib.vcxproj`.
        _NSInvocation.x86.mm
        CFBridgeBase.mm
        CFHelpers.mm
        NSArray.mm
        NSAssertionHandler.mm
        NSAttributedString.mm
        NSBlockOperation.mm
        NSBundle.mm
        NSByteOrder.mm
        NSCalendar.mm
        NSCharacterSet.mm
        NSCFCharacterSet.mm
        NSCFError.mm
        NSCoder.mm
        NSColor.mm
        NSComparisonPredicate.mm
        NSCompoundPredicate.mm
        NSCondition.mm
        NSCountedSet.mm
        NSData.mm
        NSCFData.mm
        NSDate.mm
        NSDateComponents.mm
        NSDateFormatter.mm
        NSDecimal.mm
        NSDecimalNumber.mm
        NSDelayedPerform.mm
        NSDictionary.mm
        NSCFDictionary.mm
        NSEnumerator.mm
        NSError.mm
        NSException.mm
        NSExpression.mm
        NSExpressionFunction.mm
        NSExpressionVariable.mm
        NSExpressionConstantValue.mm
        NSExpressionEvaluatedObject.mm
        NSExpressionKeyPath.mm
        NSExpressionDefaultFunctions.mm
        NSFileHandle.mm
        _NSFileHandleNullDevice.mm
        NSFileManager.mm
        NSFormatter.mm
        NSHTTPCookie.mm
        NSHTTPCookieStorage.mm
        NSHTTPURLResponse.mm
        NSIndexPath.mm
        NSIndexSet.mm
        NSInputSource.mm
        NSInputStream.mm
        NSInvocation.mm
        NSInvocationOperation.mm
        NSJSONSerialization.mm
        NSKeyedArchiver.mm
        NSKeyedUnarchiver.mm
        NSLock.mm
        NSLog.mm
        NSMethodSignature.mm
        NSMutableArray.mm
        NSMutableAttributedString.mm
        NSMutableCharacterSet.mm
        NSMutableData.mm
        NSMutableDictionary.mm
        NSMutableSet.mm
        NSMutableURLRequest.mm
        NSNotificationCenter.mm
        NSNotificationQueue.mm
        NSNull.mm
        NSNumber.mm
        NSNumberFormatter.mm
        NSObject_NSKeyValueArrayAdapter.mm
        NSObject_NSKeyValueOrderedSetAdapter.mm
        NSObject_NSKeyValueSetAdapter.mm
        NSOperation.mm
        NSOperationQueue.mm
        NSOrderedPerform.mm
        NSOrderedSet.mm
        NSOutputStream.mm
        NSPathUtilities.mm
        NSPathUtilitiesInternal.mm
        NSPersistentDomain.mm
        NSPredicate.mm
        NSPredicateUtil.mm
        NSProcessInfo.mm
        NSPropertyListSerialization.mm
        NSRange.mm
        NSRegularExpression.mm
        NSRunLoop.mm
        NSRunLoopSource.mm
        NSRunLoopState.mm
        NSSelectInputSource.mm
        NSSelectSet.mm
        NSScanner.mm
        NSSet.mm
        NSCFSet.mm
        NSSocket.mm
        NSSortDescriptor.mm
        NSSSLHandler.mm
        NSStream.mm
        NSThread.mm
        NSTimer.mm
        NSTimeZone.mm
        NSURL.mm
        NSURLAuthenticationChallenge.mm
        NSURLCache.mm
        NSURLConnection.mm
        NSURLProtocol.mm
        NSURLProtocol_file.mm
        NSURLProtocol_WinHTTP.mm
        NSURLRequest.mm
        NSURLResponse.mm
        NSValue.mm
        NSUserDefaults.mm
        NSXMLParser.mm
        Utils.mm
        _NSURLSessionDownloadResumeInfo.mm
        NSConstantString.mm
        FormatPrintf.mm
        IcuHelper.mm
        NSMutableString.mm
        NSString.mm
        NSLocale.mm
        NSUUID.mm
        NSKVOSupport.mm
        NSKVOSwizzling.mm
        NSURLCredentialStorage.mm
        NSURLSessionConfiguration.mm
        NSURLSession.mm
        NSURLSessionTask.mm
        NSURLSessionDataTask.mm
        NSURLSessionDownloadTask.mm
        NSCachedURLResponse.mm
        NSURLCredential.mm
        NSURLProtectionSpace.mm
        NSLogging.mm
        NSProgress.mm
        NSRecursiveLock.mm
        NSObject.mm
        NSAutoreleasePool.mm
        NSBlocks.mm
        NSBooleanPredicate.mm
        NSTextCheckingResult.mm
        NSBundleResourceRequest.mm
        NSByteCountFormatter.mm
        NSCache.mm
        NSConditionLock.mm
        NSDataDetector.mm
        NSDateComponentsFormatter.mm
        NSDateIntervalFormatter.mm
        NSDecimalNumberHandler.mm
        NSDirectoryEnumerator.mm
        NSEnergyFormatter.mm
        NSErrorRecoveryAttempting.mm
        NSExtensionContext.mm
        NSExtensionItem.mm
        NSFileAccessIntent.mm
        NSFileCoordinator.mm
        NSFileSecurity.mm
        NSFileVersion.mm
        NSFileWrapper.mm
        NSHashTable.mm
        NSItemProvider.mm
        NSLengthFormatter.mm
        NSLinguisticTagger.mm
        NSMachPort.mm
        NSMapTable.mm
        NSMassFormatter.mm
        NSMessagePort.mm
        NSMetadataItem.mm
        NSMetadataQuery.mm
        NSMetadataQueryAttributeValueTuple.mm
        NSMetadataQueryResultGroup.mm
        NSMutableIndexSet.mm
        NSMutableOrderedSet.mm
        NSNetService.mm
        NSNetServiceBrowser.mm
        NSNotification.mm
        NSObject_NSCoding.mm
        NSOrthography.mm
        NSPersonNameComponents.mm
        NSPersonNameComponentsFormatter.mm
        NSPipe.mm
        NSPointerArray.mm
        NSPointerFunctions.mm
        NSPort.mm
        NSPortMessage.mm
        NSProxy.mm
        NSPurgeableData.mm
        NSUbiquitousKeyValueStore.mm
        NSUndoManager.mm
        NSURLComponents.mm
        NSURLQueryItem.mm
        NSURLSessionUploadTask.mm
        NSUserActivity.mm
        NSValueTransformer.mm
        NSZone.mm
        lex.yy.m
        rules.tab.m
        MainDispatcher.mm
        NSCFString.mm
        NSCFMutableString.mm
        NSCFAttributedString.mm
        NSCFNumber.mm
        NSCFDate.mm
        NSCFArray.mm
        NSCFURL.mm
        NSCFLocale.mm
        NSCFCalendar.mm
        NSCFCollectionSupport.mm
        _NSKeyValueCodingAggregateFunctions.mm
        NSCFTimeZone.mm
        NSCFOutputStream.mm
        NSCFInputStream.mm
        NSString+HSTRING.mm
        _NSUndoBasicAction.mm
        _NSUndoGroup.mm
        _NSUndoManagerStack.mm
        # From `Foundation.vcxproj`.
        NSObject_NSCoding.mm
        NSKeyValueCoding.mm
        NSString+HSTRING.mm
        NSDictionary+NSFileManagerExtensions.mm)

    # Build `_NSInvocation.x86.asm` (from `FoundationLib.vcxproj`).
    # TODO: Maybe actually use CMake's support for MASM.
    set (ASM_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/_NSInvocation.x86.asm")
    set (ASM_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/_NSInvocation.x86.asm.obj")
    add_custom_command (
        OUTPUT "${ASM_OUTPUT}"
        # `/safeseh` for `<UseSafeExceptionHandlers>`.
        COMMAND ml /nologo /safeseh /c /Fo"${ASM_OUTPUT}" "${ASM_INPUT}"
        DEPENDS "${ASM_INPUT}")

    add_library (Foundation SHARED ${SOURCE_FILES} "${ASM_OUTPUT}")
    woc_framework (Foundation)

    target_include_directories (Foundation PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Foundation PRIVATE
        ${WINOBJC_DEFS}
        # From `FoundationLib.vcxproj`.
        STARBOARD_PORT=1
        NO_STUBS
        NSBUILDINGFOUNDATION
        FOUNDATION_IMPEXP=
        "OBJC_HOOK=extern __declspec(dllimport)"
        DEPLOYMENT_TARGET_WINDOWS)

    target_compile_options (Foundation PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `FoundationLib.vcxproj`.
        -Wno-error -Werror=incomplete-implementation -Werror=return-type
        -Werror=objc-protocol-property-synthesis -Werror=protocol)

    target_link_libraries (Foundation PRIVATE
        ${WINOBJC_LIBS}
        woc-Logging
        pthread
        objc
        CoreFoundation
        dispatch
        Starboard
        CFNetwork
        woc-Foundation
        MobileCoreServices
        # From `Foundation.vcxproj`.
        libxml2 icudt icuin icuuc
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Foundation/dll/\
Foundation.def")

    # From `FoundationLib.vcxproj`.
    set_source_files_properties (NSUserActivity.mm PROPERTIES
        COMPILE_OPTIONS "${OBJC_ARC}")

# NOT CL_COMPILER
endif (CL_COMPILER)
