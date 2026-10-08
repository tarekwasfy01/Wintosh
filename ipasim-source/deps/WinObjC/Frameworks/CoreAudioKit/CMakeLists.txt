if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreAudioKitLib.vcxproj`.
        CABTMIDICentralViewController.mm
        CABTMIDILocalPeripheralViewController.mm)

    add_library (CoreAudioKit SHARED ${SOURCE_FILES})
    woc_framework (CoreAudioKit)

    target_include_directories (CoreAudioKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreAudioKit PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreAudioKitLib.vcxproj`.
        COREAUDIOKIT_IMPEXP=)

    target_compile_options (CoreAudioKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreAudioKit PRIVATE
        ${WOCFX_LIBS}
        # From `CoreAudioKit.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreAudioKit/dll/\
CoreAudioKit.def")
endif (NOT CL_COMPILER)
