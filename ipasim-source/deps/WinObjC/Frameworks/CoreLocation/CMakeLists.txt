if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreLocationLib.vcxproj`.
        CLLocation.mm
        CLLocationManager.mm
        CLBeacon.mm
        CLBeaconRegion.mm
        CLCircularRegion.mm
        CLFloor.mm
        CLGeocoder.mm
        CLHeading.mm
        CLPlacemark.mm
        CLRegion.mm
        CLVisit.mm
        CoreLocationConstants.mm
        CoreLocationFunctions.mm
        NSValue+CoreLocationAdditions.mm)

    add_library (CoreLocation SHARED ${SOURCE_FILES})
    woc_framework (CoreLocation)

    target_include_directories (CoreLocation PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreLocation PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreLocationLib.vcxproj`.
        STARBOARD_PORT=1
        CORELOCATION_IMPEXP=)

    target_compile_options (CoreLocation PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `CoreLocationLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (CoreLocation PRIVATE
        ${WOCFX_LIBS}
        # From `CoreLocation.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreLocation/dll/\
CoreLocation.def")
endif (NOT CL_COMPILER)
