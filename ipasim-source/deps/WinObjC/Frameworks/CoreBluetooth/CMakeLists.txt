if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `CoreBluetoothLib.vcxproj`.
        CBATTRequest.mm
        CBAttribute.mm
        CBCentral.mm
        CBCentralManager.mm
        CBCentralManagerDelegate.mm
        CBCharacteristic.mm
        CBConstants.mm
        CBDescriptor.mm
        CBMutableCharacteristic.mm
        CBMutableDescriptor.mm
        CBMutableService.mm
        CBPeer.mm
        CBPeripheral.mm
        CBPeripheralManager.mm
        CBPeripheralManagerDelegate.mm
        CBService.mm
        CBUUID.mm)

    add_library (CoreBluetooth SHARED ${SOURCE_FILES})
    woc_framework (CoreBluetooth)

    target_include_directories (CoreBluetooth PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (CoreBluetooth PRIVATE
        ${WINOBJC_DEFS}
        # From `CoreBluetoothLib.vcxproj`.
        COREBLUETOOTH_IMPEXP=)

    target_compile_options (CoreBluetooth PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `CoreBluetoothLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (CoreBluetooth PRIVATE
        ${WOCFX_LIBS}
        # From `CoreBluetooth.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/CoreBluetooth/dll/\
CoreBluetooth.def")
endif (NOT CL_COMPILER)
