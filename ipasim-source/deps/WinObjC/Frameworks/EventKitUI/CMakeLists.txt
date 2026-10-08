if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `EventKitUILib.vcxproj`.
        EKCalendarChooser.mm
        EKEventEditViewController.mm
        EKEventViewController.mm)

    add_library (EventKitUI SHARED ${SOURCE_FILES})
    woc_framework (EventKitUI)

    target_include_directories (EventKitUI PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (EventKitUI PRIVATE
        ${WINOBJC_DEFS}
        # From `EventKitUILib.vcxproj`.
        EVENTKITUI_IMPEXP=)

    target_compile_options (EventKitUI PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (EventKitUI PRIVATE
        ${WOCFX_LIBS}
        # From `EventKitUI.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/EventKitUI/dll/\
EventKitUI.def")
endif (NOT CL_COMPILER)
