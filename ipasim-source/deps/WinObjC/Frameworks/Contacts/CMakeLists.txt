if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `ContactsLib.vcxproj`.
        CNContact.mm
        CNContactFetchRequest.mm
        CNContactFormatter.mm
        CNContactProperty.mm
        CNContactRelation.mm
        CNContactStore.mm
        CNContactVCardSerialization.mm
        CNContainer.mm
        CNGroup.mm
        CNInstantMessageAddress.mm
        CNLabeledValue.mm
        CNMutableContact.mm
        CNMutableGroup.mm
        CNMutablePostalAddress.mm
        CNPhoneNumber.mm
        CNPostalAddress.mm
        CNPostalAddressFormatter.mm
        CNSaveRequest.mm
        CNSocialProfile.mm
        ContactsConstants.mm)

    add_library (Contacts SHARED ${SOURCE_FILES})
    woc_framework (Contacts)

    target_include_directories (Contacts PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Contacts PRIVATE
        ${WINOBJC_DEFS}
        # From `ContactsLib.vcxproj`.
        CONTACTS_IMPEXP=)

    target_compile_options (Contacts PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (Contacts PRIVATE
        ${WOCFX_LIBS}
        # From `Contacts.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Contacts/dll/\
Contacts.def")
endif (NOT CL_COMPILER)
