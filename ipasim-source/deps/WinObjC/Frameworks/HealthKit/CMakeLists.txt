if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `HealthKitLib.vcxproj`.
        HealthKitConstants.mm
        HKAnchoredObjectQuery.mm
        HKBiologicalSexObject.mm
        HKBloodTypeObject.mm
        HKCategorySample.mm
        HKCategoryType.mm
        HKCharacteristicType.mm
        HKCorrelation.mm
        HKCorrelationQuery.mm
        HKCorrelationType.mm
        HKDeletedObject.mm
        HKDevice.mm
        HKFitzpatrickSkinTypeObject.mm
        HKHealthStore.mm
        HKObject.mm
        HKObjectType.mm
        HKObserverQuery.mm
        HKQuantity.mm
        HKQuantitySample.mm
        HKQuantityType.mm
        HKQuery.mm
        HKSample.mm
        HKSampleQuery.mm
        HKSampleType.mm
        HKSource.mm
        HKSourceQuery.mm
        HKSourceRevision.mm
        HKStatistics.mm
        HKStatisticsCollection.mm
        HKStatisticsCollectionQuery.mm
        HKStatisticsQuery.mm
        HKUnit.mm
        HKWorkout.mm
        HKWorkoutEvent.mm
        HKWorkoutType.mm)

    add_library (HealthKit SHARED ${SOURCE_FILES})
    woc_framework (HealthKit)

    target_include_directories (HealthKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (HealthKit PRIVATE
        ${WINOBJC_DEFS}
        # From `HealthKitLib.vcxproj`.
        HEALTHKIT_IMPEXP=)

    target_compile_options (HealthKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (HealthKit PRIVATE
        ${WOCFX_LIBS}
        # From `HealthKit.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/HealthKit/dll/\
HealthKit.def")
endif (NOT CL_COMPILER)
