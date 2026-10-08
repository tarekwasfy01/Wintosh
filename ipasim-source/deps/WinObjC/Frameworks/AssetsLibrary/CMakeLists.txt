if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AssetsLibraryLib.vcxproj`.
        ALAsset.mm
        ALAssetRepresentation.mm
        ALAssetsGroup.mm
        ALAssetsLibrary.mm)

    add_library (AssetsLibrary SHARED ${SOURCE_FILES})
    woc_framework (AssetsLibrary)

    target_include_directories (AssetsLibrary PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (AssetsLibrary PRIVATE
        ${WINOBJC_DEFS}
        # From `AssetsLibraryLib.vcxproj`.
        ASSETSLIBRARY_IMPEXP=)

    target_compile_options (AssetsLibrary PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (AssetsLibrary PRIVATE
        ${WOCFX_LIBS}
        # From `AssetsLibrary.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AssetsLibrary/dll/\
AssetsLibrary.def")
endif (NOT CL_COMPILER)
