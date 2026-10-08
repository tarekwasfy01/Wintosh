if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `TwitterLib.vcxproj`.
        TWRequest.mm
        TWTweetComposeViewController.mm)

    add_library (Twitter SHARED ${SOURCE_FILES})
    woc_framework (Twitter)

    target_include_directories (Twitter PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Twitter PRIVATE
        ${WINOBJC_DEFS}
        # From `TwitterLib.vcxproj`.
        STARBOARD_PORT=1
        TWITTER_IMPEXP=)

    target_compile_options (Twitter PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `TwitterLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (Twitter PRIVATE
        ${WOCFX_LIBS}
        # From `Twitter.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Twitter/dll/\
Twitter.def")
endif (NOT CL_COMPILER)
