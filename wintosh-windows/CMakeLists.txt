cmake_minimum_required(VERSION 3.20)

project(wintosh-windows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Wintosh's native implementation is independent from the local Darling
# checkout. Darling remains the upstream behavior and license reference.
file(GLOB_RECURSE WINTOSH_HOST_SOURCES CONFIGURE_DEPENDS
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

    file(GLOB WINTOSH_SMOKE_SOURCES CONFIGURE_DEPENDS tests/*_smoke.cpp)
    foreach(smoke_source IN LISTS WINTOSH_SMOKE_SOURCES)
        get_filename_component(smoke_name ${smoke_source} NAME_WE)
        add_executable(wintosh_${smoke_name} ${smoke_source})
        target_link_libraries(wintosh_${smoke_name} PRIVATE wintosh_windows_host)
    endforeach()

    add_executable(wintosh_broker core/darling_windows_broker.cpp)
    target_link_libraries(wintosh_broker PRIVATE wintosh_windows_host)
else()
    message(FATAL_ERROR "wintosh-windows is a Windows-only backend")
endif()
