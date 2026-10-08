if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CloudKitLib.vcxproj`.
        CKAsset.mm
        CKContainer.mm
        CKDatabase.mm
        CKDatabaseOperation.mm
        CKDiscoverAllContactsOperation.mm
        CKDiscoveredUserInfo.mm
        CKDiscoverUserInfosOperation.mm
        CKFetchNotificationChangesOperation.mm
        CKFetchRecordChangesOperation.mm
        CKFetchRecordsOperation.mm
        CKFetchRecordZonesOperation.mm
        CKFetchSubscriptionsOperation.mm
        CKLocationSortDescriptor.mm
        CKMarkNotificationsReadOperation.mm
        CKModifyBadgeOperation.mm
        CKModifyRecordsOperation.mm
        CKModifyRecordZonesOperation.mm
        CKModifySubscriptionsOperation.mm
        CKNotification.mm
        CKNotificationID.mm
        CKNotificationInfo.mm
        CKOperation.mm
        CKQuery.mm
        CKQueryCursor.mm
        CKQueryNotification.mm
        CKQueryOperation.mm
        CKRecord.mm
        CKRecordID.mm
        CKRecordZone.mm
        CKRecordZoneID.mm
        CKRecordZoneNotification.mm
        CKReference.mm
        CKServerChangeToken.mm
        CKSubscription.mm
        CloudKitFrameworkConstants.mm)

    add_library (CloudKit SHARED ${SOURCE_FILES})
    woc_framework (CloudKit)

    target_include_directories (CloudKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CloudKit PRIVATE
        ${WINOBJC_DEFS}
        # From `CloudKitLib.vcxproj`.
        CLOUDKIT_IMPEXP=)

    target_compile_options (CloudKit PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `CloudKitLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (CloudKit PRIVATE
        ${WOCFX_LIBS}
        # From `CloudKit.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CloudKit/dll/\
CloudKit.def")
endif (NOT CL_COMPILER)
