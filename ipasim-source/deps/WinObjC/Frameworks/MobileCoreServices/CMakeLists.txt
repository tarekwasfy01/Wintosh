if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `MobileCoreServicesLib.vcxproj`.
        UTType.cpp
        UTTypeInternal.cpp)

    add_library (MobileCoreServices SHARED ${SOURCE_FILES})
    woc_framework (MobileCoreServices)

    target_include_directories (MobileCoreServices PRIVATE
        ${WINOBJC_INCLUDE_DIRS}
        # From `MobileCoreServicesLib.vcxproj`.
        ../include)

    target_compile_definitions (MobileCoreServices PRIVATE
        ${WINOBJC_DEFS}
        # From `MobileCoreServicesLib.vcxproj`.
        MOBILECORESERVICES_IMPEXP=)

    target_compile_options (MobileCoreServices PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (MobileCoreServices PRIVATE
        ${WINOBJC_LIBS}
        woc-Logging
        CoreFoundation
        # From `MobileCoreServices.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/MobileCoreServices/\
dll/MobileCoreServices.def") # For `<ModuleDefinitionFile>`.
endif (NOT CL_COMPILER)
