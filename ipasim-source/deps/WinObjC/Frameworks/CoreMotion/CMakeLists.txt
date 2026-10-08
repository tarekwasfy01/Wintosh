if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreMotionLib.vcxproj`.
        CMAccelerometerData.mm
        CMAltimeter.mm
        CMAltitudeData.mm
        CMAttitude.mm
        CMDeviceMotion.mm
        CMGyroData.mm
        CMLogItem.mm
        CMMagnetometerData.mm
        CMMotionActivity.mm
        CMMotionActivityManager.mm
        CMMotionManager.mm
        CMPedometer.mm
        CMPedometerData.mm
        CMRecordedAccelerometerData.mm
        CMSensorDataList.mm
        CMSensorRecorder.mm
        CMStepCounter.mm)

    add_objcuwp_libs ()

    add_library (CoreMotion SHARED ${SOURCE_FILES})
    woc_framework (CoreMotion)

    target_include_directories (CoreMotion PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreMotion PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreMotionLib.vcxproj`.
        COREMOTION_IMPEXP=)

    target_compile_options (CoreMotion PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `CoreMotionLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (CoreMotion PRIVATE
        ${WOCFX_LIBS}
        # From `CoreMotion.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreMotion/dll/\
CoreMotion.def")
endif (NOT CL_COMPILER)
