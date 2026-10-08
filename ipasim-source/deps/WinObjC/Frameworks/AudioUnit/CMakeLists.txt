if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AudioUnitLib.vcxproj`.
        AUAudioUnit.mm
        AUAudioUnitBus.mm
        AUAudioUnitBusArray.mm
        AUAudioUnitPreset.mm
        AUAudioUnitV2Bridge.mm
        AudioComponent.mm
        AudioOutput.mm
        AudioUnit.mm
        AUParameter.mm
        AUParameterGroup.mm
        AUParameterNode.mm
        AUParameterTree.mm)

    add_library (AudioUnit SHARED ${SOURCE_FILES})
    woc_framework (AudioUnit)

    target_include_directories (AudioUnit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (AudioUnit PRIVATE
        ${WINOBJC_DEFS}
        # From `AudioUnitLib.vcxproj`.
        AUDIOUNIT_IMPEXP=)

    target_compile_options (AudioUnit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (AudioUnit PRIVATE
        ${WOCFX_LIBS}
        # From `AudioUnit.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AudioUnit/dll/\
AudioUnit.def")
endif (NOT CL_COMPILER)
