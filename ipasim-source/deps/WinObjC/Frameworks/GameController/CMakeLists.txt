if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `GameControllerLib.vcxproj`.
        GCController.mm
        GCControllerAxisInput.mm
        GCControllerButtonInput.mm
        GCControllerDirectionPad.mm
        GCControllerElement.mm
        GCExtendedGamepad.mm
        GCExtendedGamepadSnapshot.mm
        GCGamepad.mm
        GCGamepadSnapshot.mm
        GCMotion.mm
        GCSnapshot.mm)

    add_library (GameController SHARED ${SOURCE_FILES})
    woc_framework (GameController)

    target_include_directories (GameController PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (GameController PRIVATE
        ${WINOBJC_DEFS}
        # From `GameControllerLib.vcxproj`.
        GAMECONTROLLER_IMPEXP=)

    target_compile_options (GameController PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (GameController PRIVATE
        ${WOCFX_LIBS}
        # From `GameController.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/GameController/dll/\
GameController.def")
endif (NOT CL_COMPILER)
