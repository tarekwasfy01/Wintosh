if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreFoundationAdditionsLib.vcxproj`.
        _NSCFNumber.mm
        _NSCFTemporaryRootObject.mm
        _NSCFString.mm
        _NSCFType.mm
        _CFLocaleInternal.mm
        CFStringTokenizer.mm)

    add_library (CoreFoundationAdditions STATIC ${SOURCE_FILES})
    woc_framework (CoreFoundationAdditions)

    target_include_directories (CoreFoundationAdditions PRIVATE
        ${WINOBJC_INCLUDE_DIRS}
        # From `CoreFoundationAdditionsLib.vcxproj`.
        # TODO: Share these among Frameworks.
        ../include
        ../CoreFoundation/Base.subproj)

    target_link_libraries (CoreFoundationAdditions PRIVATE
        woc-Logging)

    target_compile_options (CoreFoundationAdditions PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `CoreFoundationAdditionsLib.vcxproj`.
        "-include${CMAKE_CURRENT_SOURCE_DIR}/..\
/CoreFoundation/Base.subproj/CoreFoundation_Prefix.h") # For `<PrefixHeader>`.

    target_compile_definitions (CoreFoundationAdditions PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreFoundationAdditionsLib.vcxproj`.
        CF_BUILDING_CF=1
        NO_STUBS
        INCLUDE_OBJC=1
        DEPLOYMENT_TARGET_WINDOWS=1
        HAVE_STRUCT_TIMESPEC=1
        STARBOARD_PORT=1
        TARGET_OS_WIN32=1
        HAVE_MACH_ABSOLUTE_TIME=1
        INCLUDE_OBJC=1
        UNICODE
        DISPATCH_NO_LEGACY=1
        __XSI_VISIBLE=500
        TARGET_IPHONE_SIMULATOR=0
        __CONSTANT_CFSTRINGS__=1
        FOUNDATION_IMPEXP=
        COREFOUNDATION_IMPEXP=
        "OBJC_HOOK=extern __declspec(dllimport)")
endif (NOT CL_COMPILER)
