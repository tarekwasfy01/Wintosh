if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreFoundationLib.vcxproj`.
        Base.subproj/CFBase.c
        Base.subproj/CFFileUtilities.c
        Base.subproj/CFPlatform.c
        Base.subproj/CFRuntime.c
        Base.subproj/CFSortFunctions.c
        Base.subproj/CFSystemDirectories.c
        Base.subproj/CFUtilities.c
        Base.subproj/CFUUID.c
        Collections.subproj/CFArray.c
        Collections.subproj/CFBag.c
        Collections.subproj/CFBasicHash.c
        Collections.subproj/CFBinaryHeap.c
        Collections.subproj/CFBitVector.c
        Collections.subproj/CFData.c
        Collections.subproj/CFDictionary.c
        Collections.subproj/CFSet.c
        Collections.subproj/CFStorage.c
        Collections.subproj/CFTree.c
        Error.subproj/CFError.c
        Locale.subproj/CFCalendar.c
        Locale.subproj/CFDateFormatter.c
        Locale.subproj/CFLocale.c
        Locale.subproj/CFLocaleIdentifier.c
        Locale.subproj/CFLocaleKeys.c
        Locale.subproj/CFNumberFormatter.c
        NumberDate.subproj/CFBigNumber.c
        NumberDate.subproj/CFDate.c
        NumberDate.subproj/CFNumber.c
        NumberDate.subproj/CFTimeZone.c
        Parsing.subproj/CFBinaryPList.c
        Parsing.subproj/CFOldStylePList.c
        Parsing.subproj/CFPropertyList.c
        Parsing.subproj/CFXMLInputStream.c
        Parsing.subproj/CFXMLNode.c
        Parsing.subproj/CFXMLParser.c
        Parsing.subproj/CFXMLTree.c
        PlugIn.subproj/CFBundle.c
        PlugIn.subproj/CFBundle_Binary.c
        PlugIn.subproj/CFBundle_Grok.c
        PlugIn.subproj/CFBundle_InfoPlist.c
        PlugIn.subproj/CFBundle_Locale.c
        PlugIn.subproj/CFBundle_Resources.c
        PlugIn.subproj/CFBundle_Strings.c
        PlugIn.subproj/CFPlugIn.c
        PlugIn.subproj/CFPlugIn_Factory.c
        PlugIn.subproj/CFPlugIn_Instance.c
        PlugIn.subproj/CFPlugIn_PlugIn.c
        Preferences.subproj/CFApplicationPreferences.c
        Preferences.subproj/CFPreferences.c
        RunLoop.subproj/CFRunLoop.c
        RunLoop.subproj/CFSocket.c
        Stream.subproj/CFConcreteStreams.c
        Stream.subproj/CFSocketStream.c
        Stream.subproj/CFStream.c
        String.subproj/CFAttributedString.c
        String.subproj/CFBurstTrie.c
        String.subproj/CFCharacterSet.c
        String.subproj/CFRegularExpression.c
        String.subproj/CFRunArray.c
        String.subproj/CFString.c
        String.subproj/CFStringEncodings.c
        String.subproj/CFStringScanner.c
        String.subproj/CFStringTransform.c
        String.subproj/CFStringUtilities.c
        StringEncodings.subproj/CFBuiltinConverters.c
        StringEncodings.subproj/CFICUConverters.c
        StringEncodings.subproj/CFPlatformConverters.c
        StringEncodings.subproj/CFStringEncodingConverter.c
        StringEncodings.subproj/CFStringEncodingDatabase.c
        StringEncodings.subproj/CFUniChar.c
        StringEncodings.subproj/CFUnicodeDecomposition.c
        StringEncodings.subproj/CFUnicodePrecomposition.c
        URL.subproj/CFURL.c
        URL.subproj/CFURLAccess.c
        URL.subproj/CFURLComponents.c
        URL.subproj/CFURLComponents_URIParser.c)

    add_library (CoreFoundation SHARED ${SOURCE_FILES})
    woc_framework (CoreFoundation)

    target_include_directories (CoreFoundation PRIVATE
        ${WINOBJC_INCLUDE_DIRS}
        # From `CoreFoundationLib.vcxproj` (minus those from above).
        ../include
        ../../include/WOCStdLib
        Base.SubProj
        AppServices.SubProj
        Collections.SubProj
        Error.SubProj
        Locale.SubProj
        NumberDate.SubProj
        Parsing.SubProj
        Plugin.SubProj
        Preferences.SubProj
        RunLoop.SubProj
        Stream.SubProj
        String.SubProj
        StringEncodings.SubProj
        URL.SubProj)

    target_compile_definitions (CoreFoundation PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreFoundationLib.vcxproj`.
        CF_BUILDING_CF=1
        TARGET_OS_WIN32=1
        DEPLOYMENT_TARGET_WINDOWS=1
        HAVE_STRUCT_TIMESPEC=1
        INCLUDE_OBJC=1
        UNICODE
        DISPATCH_NO_LEGACY=1
        __XSI_VISIBLE=500
        TARGET_IPHONE_SIMULATOR=0
        __CONSTANT_CFSTRINGS__=1
        WIL_IGNORE_OBJC_EXCEPTIONS)

    target_compile_options (CoreFoundation PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `CoreFoundationLib.vcxproj`.
        # For `<PrefixHeader>`.
        "-include\
${CMAKE_CURRENT_SOURCE_DIR}/Base.subproj/CoreFoundation_Prefix.h"
        -xobjective-c++ # For `<CompileAs>CompileAsObjCpp</CompileAs>`.
        # Warnings
        -Wno-error -Werror=incomplete-implementation
        -Werror=objc-protocol-property-synthesis -Werror=protocol
        -Wno-duplicate-decl-specifier -Wno-nonnull -Wno-receiver-expr
        -Wno-msvc-include -Wno-deprecated-declarations -Wno-switch)

    target_link_libraries (CoreFoundation PRIVATE
        ${WINOBJC_LIBS}
        # From `CoreFoundation.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreFoundation/dll/\
CoreFoundation.def" # For `<ModuleDefinitionFile>`.
        CoreFoundationAdditions # For `<ProjectReference>`.
        # For `<AdditionalDependencies>`.
        mincore.lib icudt icuin icuuc
        # TODO: Add `libxml2.lib`.
        # Probably from WinObjC NuGet packages originally (specified in
        # `sdk-build.props`).
        woc-Logging
        woc-WOCStdLib
        objc
        pthread
        dispatch
        # TODO: Headers from `libobjc` are actually mostly duplicates of those
        # from WOCStdLib. We should use the `libobjc` ones.
        objc
        woc-WinObjCRT
        Starboard) # TODO: This is not in `lld-link`'s `.tlog` file!
endif (NOT CL_COMPILER)
