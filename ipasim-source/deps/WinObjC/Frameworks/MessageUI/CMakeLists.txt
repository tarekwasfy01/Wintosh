if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `MessageUILib.vcxproj`.
        MFMailComposeViewController.mm
        MFMessageComposeViewController.mm)

    add_library (MessageUI SHARED ${SOURCE_FILES})
    woc_framework (MessageUI)

    target_include_directories (MessageUI PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (MessageUI PRIVATE
        ${WINOBJC_DEFS}
        # From `MessageUILib.vcxproj`.
        MESSAGEUI_IMPEXP=)

    target_compile_options (MessageUI PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (MessageUI PRIVATE
        ${WOCFX_LIBS}
        # From `MessageUI.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/MessageUI/dll/\
MessageUI.def")
endif (NOT CL_COMPILER)
