cmake_minimum_required(VERSION 3.20)

project(wintosh-windows LANGUAGES CXX)

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    enable_language(ASM)
    set(WINTOSH_STARTUP_OBJ
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/darling_windows_startup_x64.S")
elseif (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM_MASM)
    set(WINTOSH_STARTUP_ASM
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/darling_windows_startup_x64.asm")
    set(WINTOSH_STARTUP_OBJ
        "${CMAKE_CURRENT_BINARY_DIR}/darling_windows_startup_x64.obj")
    add_custom_command(
        OUTPUT "${WINTOSH_STARTUP_OBJ}"
        COMMAND "${CMAKE_ASM_MASM_COMPILER}" /c /nologo
            "/Fo${WINTOSH_STARTUP_OBJ}" "${WINTOSH_STARTUP_ASM}"
        DEPENDS "${WINTOSH_STARTUP_ASM}"
        VERBATIM)
endif()

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
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND WINTOSH_HOST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/abi/darling_windows_variadic_x64.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/abi/darling_windows_stub_trampoline_x64.S")
endif()
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND WINTOSH_HOST_SOURCES "${WINTOSH_STARTUP_OBJ}")
endif()
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

    # The broker test contains intentional blocked IPC cases. Keep a hung
    # runner from holding the release job forever while preserving a generous
    # window for slow GitHub-hosted Windows workers.
    set_tests_properties(
        wintosh_darling_windows_broker_smoke
        PROPERTIES TIMEOUT 120)

    add_executable(wintosh_broker core/darling_windows_broker.cpp)
    target_link_libraries(wintosh_broker PRIVATE wintosh_windows_host)

    # Public command-line entry point. This is the user-facing executable;
    # smoke targets remain separate verification programs.
    add_executable(wintosh runtime/darling_windows_runner.cpp)
    target_link_libraries(wintosh PRIVATE wintosh_windows_host)

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        get_filename_component(WINTOSH_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(WINTOSH_LD64_LLD "${WINTOSH_LLVM_BIN}/ld64.lld.exe")
        add_test(NAME wintosh_macho_printf_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/macho_printf_e2e.ps1"
                "$<TARGET_FILE:wintosh_macho_exit_smoke>"
                "$<TARGET_FILE:wintosh>"
                "${CMAKE_CURRENT_BINARY_DIR}/macho-printf-e2e.macho")
        set_tests_properties(wintosh_macho_printf_e2e PROPERTIES RUN_SERIAL TRUE)
        add_test(NAME wintosh_hello_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/hello_e2e.ps1"
                "$<TARGET_FILE:wintosh_macho_exit_smoke>"
                "$<TARGET_FILE:wintosh>"
                "${CMAKE_CXX_COMPILER}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hello/hello.c"
                "${CMAKE_CURRENT_BINARY_DIR}/hello.macho")
        set_tests_properties(wintosh_hello_e2e PROPERTIES RUN_SERIAL TRUE)
        add_test(NAME wintosh_guest_atexit_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/macho_atexit_e2e.ps1"
                -Generator "$<TARGET_FILE:wintosh_macho_exit_smoke>"
                -Runner "$<TARGET_FILE:wintosh>"
                -Fixture "${CMAKE_CURRENT_BINARY_DIR}/guest-atexit.macho")
        set_tests_properties(wintosh_guest_atexit_e2e PROPERTIES RUN_SERIAL TRUE)
        add_test(NAME wintosh_guest_cxa_atexit_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/macho_cxa_atexit_e2e.ps1"
                -Generator "$<TARGET_FILE:wintosh_macho_exit_smoke>"
                -Runner "$<TARGET_FILE:wintosh>"
                -Fixture "${CMAKE_CURRENT_BINARY_DIR}/guest-cxa-atexit.macho")
        set_tests_properties(wintosh_guest_cxa_atexit_e2e PROPERTIES RUN_SERIAL TRUE)
        add_test(NAME wintosh_darwin_start_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/macho_darwin_start_e2e.ps1"
                -Generator "$<TARGET_FILE:wintosh_macho_exit_smoke>"
                -Runner "$<TARGET_FILE:wintosh>"
                -Fixture "${CMAKE_CURRENT_BINARY_DIR}/darwin-start-e2e.macho")
        set_tests_properties(wintosh_darwin_start_e2e PROPERTIES RUN_SERIAL TRUE)
        add_test(NAME wintosh_darwin_start_argv_env_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/macho_darwin_start_env_e2e.ps1"
                -Generator "$<TARGET_FILE:wintosh_macho_exit_smoke>"
                -Runner "$<TARGET_FILE:wintosh>"
                -Fixture "${CMAKE_CURRENT_BINARY_DIR}/darwin-start-env-e2e.macho")
        set_tests_properties(wintosh_darwin_start_argv_env_e2e PROPERTIES RUN_SERIAL TRUE)
        add_test(NAME wintosh_fixture_source_ladder
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fixture-ladder/build-fixtures.ps1"
                -Clang "${CMAKE_CXX_COMPILER}"
                -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/fixture-ladder-objects"
                -Linker "${WINTOSH_LD64_LLD}")
        set_tests_properties(wintosh_fixture_source_ladder PROPERTIES RUN_SERIAL TRUE)
        add_test(NAME wintosh_argv_env_fixture_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fixture-ladder/argv-env-e2e.ps1"
                -Clang "${CMAKE_CXX_COMPILER}"
                -Linker "${WINTOSH_LD64_LLD}"
                -Runner "$<TARGET_FILE:wintosh>"
                -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/fixture-ladder-e2e")
        set_tests_properties(wintosh_argv_env_fixture_e2e PROPERTIES RUN_SERIAL TRUE)
        foreach(fixture_name IN ITEMS memory qsort-callback file-io)
            add_test(NAME wintosh_${fixture_name}_fixture_e2e
                COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fixture-ladder/fixture-e2e.ps1"
                    -Clang "${CMAKE_CXX_COMPILER}"
                    -Linker "${WINTOSH_LD64_LLD}"
                    -Runner "$<TARGET_FILE:wintosh>"
                    -FixtureName "${fixture_name}"
                    -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/fixture-ladder-e2e-${fixture_name}")
            set_tests_properties(wintosh_${fixture_name}_fixture_e2e PROPERTIES RUN_SERIAL TRUE)
        endforeach()
        add_test(NAME wintosh_crash_fixture_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fixture-ladder/fixture-e2e.ps1"
                -Clang "${CMAKE_CXX_COMPILER}"
                -Linker "${WINTOSH_LD64_LLD}"
                -Runner "$<TARGET_FILE:wintosh>"
                -FixtureName crash
                -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/fixture-ladder-e2e-crash"
                -ExpectedExit 139)
        set_tests_properties(wintosh_crash_fixture_e2e PROPERTIES RUN_SERIAL TRUE)
    endif()
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_test(NAME wintosh_darwin_start_msvc_fail_closed
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/macho_darwin_start_msvc_fail_closed.ps1"
                "$<TARGET_FILE:wintosh_macho_exit_smoke>"
                "$<TARGET_FILE:wintosh>"
                "${CMAKE_CURRENT_BINARY_DIR}/darwin-start-msvc.macho")
        set_tests_properties(wintosh_darwin_start_msvc_fail_closed PROPERTIES RUN_SERIAL TRUE)
    endif()

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
