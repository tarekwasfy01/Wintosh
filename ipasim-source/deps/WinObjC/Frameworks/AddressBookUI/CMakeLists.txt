if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AddressBookUILib.vcxproj`.
        ABNewPersonViewController.mm
        ABPeoplePickerNavigationController.mm
        ABPersonViewController.mm
        ABUnknownPersonViewController.mm
        AddressBookUIFunctions.mm)

    add_library (AddressBookUI SHARED ${SOURCE_FILES})
    woc_framework (AddressBookUI)

    target_include_directories (AddressBookUI PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (AddressBookUI PRIVATE
        ${WINOBJC_DEFS}
        # From `AddressBookUILib.vcxproj`.
        STARBOARD_PORT=1
        ADDRESSBOOKUI_IMPEXP=)

    target_compile_options (AddressBookUI PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `AddressBookUILib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (AddressBookUI PRIVATE
        ${WOCFX_LIBS}
        # From `AddressBookUI.vcxproj`.
        UIKit
        # TODO: Add AddressBook.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AddressBookUI/dll/\
AddressBookUI.def")
endif (NOT CL_COMPILER)
