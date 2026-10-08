if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AudioToolboxLib.vcxproj`.
        AudioFile.mm
        CAFDecoder.mm
        stb_vorbis.c
        AudioConverter.mm
        AudioFileStream.mm
        AudioFormat.mm
        AudioQueue.mm
        AudioSession.mm
        AudioToolboxDebugging.mm
        AudioUnitProcessingGraph.mm
        ExtendedAudioFile.mm
        MusicSequence.mm
        MusicTrack.mm
        SystemSound.mm)

    add_library (AudioToolbox SHARED ${SOURCE_FILES})
    woc_framework (AudioToolbox)

    target_include_directories (AudioToolbox PRIVATE
        ${WOCFX_INCLUDE_DIRS}
        # TODO: Add this one couple levels higher. Needed in, e.g.,
        # `SystemSound.mm`.
        "C:/Program Files (x86)/Windows Kits/10/Extension SDKs/WindowsMobile/\
10.0.15063.0/Include/WinRT")

    target_compile_definitions (AudioToolbox PRIVATE
        ${WINOBJC_DEFS}
        # From `AudioToolboxLib.vcxproj`.
        AUDIOTOOLBOX_IMPEXP=)

    target_compile_options (AudioToolbox PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `AudioToolboxLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (AudioToolbox PRIVATE
        ${WOCFX_LIBS}
        # From `AudioToolbox.vcxproj`.
        mfuuid.lib
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AudioToolbox/dll/\
AudioToolbox.def")
endif (NOT CL_COMPILER)
