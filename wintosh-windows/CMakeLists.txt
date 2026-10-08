cmake_minimum_required(VERSION 3.20)

project(wintosh-windows LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CPACK_PACKAGE_NAME "Wintosh")
set(CPACK_PACKAGE_VENDOR "Wintosh contributors")
set(CPACK_PACKAGE_VERSION "0.1.1")
set(CPACK_PACKAGE_FILE_NAME "Wintosh-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP")

# Wintosh's native implementation is independent from the local Darling
# checkout. Darling remains the upstream behavior and license reference.
file(GLOB_RECURSE WINTOSH_HOST_SOURCES
    core/*.cpp platform/*.cpp abi/*.cpp loader/*.cpp runtime/*.cpp
    frameworks/*.cpp process/*.cpp
)
list(REMOVE_ITEM WINTOSH_HOST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/core/darling_windows_broker.cpp"
)

add_library(wintosh_windows_host STATIC ${WINTOSH_HOST_SOURCES})
target_include_directories(wintosh_windows_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/abi ${CMAKE_CURRENT_SOURCE_DIR}/loader
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime ${CMAKE_CURRENT_SOURCE_DIR}/frameworks
    ${CMAKE_CURRENT_SOURCE_DIR}/process
)
target_compile_features(wintosh_windows_host PUBLIC cxx_std_20)

if (WIN32)
    target_compile_definitions(wintosh_windows_host PUBLIC
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(wintosh_windows_host PUBLIC
        kernel32 advapi32 psapi ws2_32
    )

    file(GLOB WINTOSH_SMOKE_SOURCES tests/*_smoke.cpp)
    foreach(smoke_source IN LISTS WINTOSH_SMOKE_SOURCES)
        get_filename_component(smoke_name ${smoke_source} NAME_WE)
        add_executable(wintosh_${smoke_name} ${smoke_source})
        target_link_libraries(wintosh_${smoke_name} PRIVATE wintosh_windows_host)
        add_test(NAME wintosh_${smoke_name}
            COMMAND wintosh_${smoke_name})
    endforeach()

    # These boundary tests intentionally exercise process-global state
    # (the CoreFoundation run loop and the signal registry). Keep them out of
    # the parallel worker pool so the suite measures their runtime contracts,
    # not cross-test interference from concurrent smoke processes.
    set_tests_properties(
        wintosh_corefoundation_smoke
        wintosh_signals_smoke
        PROPERTIES RUN_SERIAL TRUE)

    add_executable(wintosh_broker core/darling_windows_broker.cpp)
    target_link_libraries(wintosh_broker PRIVATE wintosh_windows_host)

    # Public command-line entry point. This is the user-facing executable;
    # smoke targets remain separate verification programs.
    add_executable(wintosh runtime/darling_windows_runner.cpp)
    target_link_libraries(wintosh PRIVATE wintosh_windows_host)

    install(TARGETS wintosh wintosh_broker
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE-WINTOSH-ORIGINAL-MIT.txt"
        DESTINATION .
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../licenses/"
        DESTINATION licenses
        FILES_MATCHING PATTERN "*.txt" PATTERN "*.csv" PATTERN "*.md"
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION .
        RENAME README-WINTOSH.md
    )
else()
    message(FATAL_ERROR "wintosh-windows is a Windows-only backend")
endif()

include(CPack)
