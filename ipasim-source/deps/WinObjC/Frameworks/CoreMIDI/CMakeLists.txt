if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreMIDILib.vcxproj`.
        MIDINetworkConnection.mm
        MIDINetworkHost.mm
        MIDINetworkSession.mm
        MIDIServices.mm
        MIDISetup.mm
        MIDIThruConnection.mm)

    add_library (CoreMIDI SHARED ${SOURCE_FILES})
    woc_framework (CoreMIDI)

    target_include_directories (CoreMIDI PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreMIDI PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreMIDILib.vcxproj`.
        COREMIDI_IMPEXP=)

    target_compile_options (CoreMIDI PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreMIDI PRIVATE
        ${WOCFX_LIBS}
        # From `CoreMIDI.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreMIDI/dll/\
CoreMIDI.def")
endif (NOT CL_COMPILER)
