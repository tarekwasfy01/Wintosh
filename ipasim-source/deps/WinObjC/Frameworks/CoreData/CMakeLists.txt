if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreDataLib.vcxproj`.
        CoreDataConstants.mm
        NSAtomicStore.mm
        NSAtomicStoreCacheNode.mm
        NSAttributeDescription.mm
        NSEntityDescription.mm
        NSEntityMapping.mm
        NSEntityMigrationPolicy.mm
        NSExpressionDescription.mm
        NSFetchedPropertyDescription.mm
        NSFetchedResultsController.mm
        NSFetchRequest.mm
        NSFetchRequestExpression.mm
        NSIncrementalStore.mm
        NSIncrementalStoreNode.mm
        NSManagedObject.mm
        NSManagedObjectContext.mm
        NSManagedObjectID.mm
        NSManagedObjectModel.mm
        NSManagedObjectModel-XMLParsing.mm
        NSMappingModel.mm
        NSMergeConflict.mm
        NSMergePolicy.mm
        NSMigrationManager.mm
        NSPersistentStore.mm
        NSPersistentStoreCoordinator.mm
        NSPersistentStoreRequest.mm
        NSPropertyDescription.mm
        NSPropertyMapping.mm
        NSRelationshipDescription.mm
        NSSaveChangesRequest.mm)

    add_library (CoreData SHARED ${SOURCE_FILES})
    woc_framework (CoreData)

    target_include_directories (CoreData PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreData PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreDataLib.vcxproj`.
        COREDATA_IMPEXP=)

    target_compile_options (CoreData PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (CoreData PRIVATE
        ${WOCFX_LIBS}
        # From `CoreData.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreData/dll/\
CoreData.def")
endif (NOT CL_COMPILER)
