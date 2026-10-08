if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `HomeKitLib.vcxproj`.
        HMAccessory.mm
        HMAccessoryBrowser.mm
        HMAction.mm
        HMActionSet.mm
        HMCharacteristic.mm
        HMCharacteristicMetadata.mm
        HMCharacteristicWriteAction.mm
        HMHome.mm
        HMHomeManager.mm
        HMRoom.mm
        HMService.mm
        HMServiceGroup.mm
        HMTimerTrigger.mm
        HMTrigger.mm
        HMUser.mm
        HMZone.mm
        HomeKitConstants.mm)

    add_library (HomeKit SHARED ${SOURCE_FILES})
    woc_framework (HomeKit)

    target_include_directories (HomeKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (HomeKit PRIVATE
        ${WINOBJC_DEFS}
        # From `HomeKitLib.vcxproj`.
        HOMEKIT_IMPEXP=)

    target_compile_options (HomeKit PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `HomeKitLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (HomeKit PRIVATE
        ${WOCFX_LIBS}
        # From `HomeKit.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/HomeKit/dll/\
HomeKit.def")
endif (NOT CL_COMPILER)
