set (SOURCE_FILES
    # From `LoggingLib.vcxproj`.
    lib/ErrorHandling.cpp
    lib/LoggingInternal.cpp
    lib/LoggingNative.cpp
    lib/LoggingTesting.cpp
    # From `Logging.vcxproj`.
    dll/Telemetry.cpp)

add_library (Logging SHARED ${SOURCE_FILES})
add_prep_dep (Logging)

target_include_directories (Logging
    # From `LoggingLib.vcxproj`.
    PRIVATE
        ../include/WOCStdLib
        ../include
    PUBLIC
        include)

target_link_libraries (Logging PRIVATE
    # From `Logging.vcxproj`.
    AppInsights # For `<ProjectReference>`.
    # For `<ModuleDefinitionFile>`.
    "-def:${CMAKE_CURRENT_SOURCE_DIR}/dll/Logging.def")

set_source_files_properties (dll/Telemetry.cpp PROPERTIES
    # From `Logging.vcxproj`.
    COMPILE_OPTIONS "${COMPILE_AS_WINRT}")
