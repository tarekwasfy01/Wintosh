if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreAudioLib.vcxproj`.
        CoreAudioData.mm)

    add_library (CoreAudio SHARED ${SOURCE_FILES})
    woc_framework (CoreAudio)

    target_include_directories (CoreAudio PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreAudio PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreAudioLib.vcxproj`.
        COREAUDIO_IMPEXP=)

    target_compile_options (CoreAudio PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreAudio PRIVATE
        ${WOCFX_LIBS}
        # From `CoreAudio.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreAudio/dll/\
CoreAudio.def")
endif (NOT CL_COMPILER)
