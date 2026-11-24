cmake_minimum_required(VERSION 3.20)

project(wintosh-windows LANGUAGES CXX)

set(WINTOSH_DYNARMIC_SOURCE_DIR "" CACHE PATH
    "Dynarmic source checkout used for the optional x64-host AArch64 backend")
set(WINTOSH_DYNARMIC_BUILD_DIR "" CACHE PATH
    "Dynarmic CMake build directory containing its Windows static libraries")

set(WINTOSH_X64_TARGET FALSE)
set(WINTOSH_ARM64_TARGET FALSE)
set(WINTOSH_I386_TARGET FALSE)
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    # The guest ABI shims and startup trampolines below are specifically
    # x86_64. Do not infer x64 from pointer width: Windows ARM64 is also 64-bit.
    set(WINTOSH_X64_TARGET TRUE)
    foreach(arch_hint IN ITEMS CMAKE_SYSTEM_PROCESSOR CMAKE_GENERATOR_PLATFORM
            CMAKE_CXX_COMPILER_TARGET CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        if ("${${arch_hint}}" MATCHES "[Aa][Rr][Mm]64|[Aa][Aa]64|[Aa]arch64")
            set(WINTOSH_X64_TARGET FALSE)
            set(WINTOSH_ARM64_TARGET TRUE)
        endif()
    endforeach()
elseif (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # A Win32 process runs i386 Darwin instructions directly. Darwin and
    # Windows share the stack-based i386 cdecl convention at C boundaries.
    set(WINTOSH_I386_TARGET TRUE)
endif()

if (WINTOSH_X64_TARGET AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    enable_language(ASM)
    set(WINTOSH_CLANG_ASM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/darling_windows_startup_x64.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/abi/darling_windows_variadic_x64.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/abi/darling_windows_stub_trampoline_x64.S"
    )
    set(WINTOSH_CLANG_ASM_OBJECTS)
    foreach(asm_source IN LISTS WINTOSH_CLANG_ASM_SOURCES)
        get_filename_component(asm_name "${asm_source}" NAME_WE)
        set(asm_object "${CMAKE_CURRENT_BINARY_DIR}/${asm_name}_clang.obj")
        add_custom_command(
            OUTPUT "${asm_object}"
            COMMAND "${CMAKE_ASM_COMPILER}" /c /nologo
                "/Fo${asm_object}" "${asm_source}"
            DEPENDS "${asm_source}"
            VERBATIM
        )
        list(APPEND WINTOSH_CLANG_ASM_OBJECTS "${asm_object}")
    endforeach()
elseif (WINTOSH_X64_TARGET)
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
elseif (WINTOSH_I386_TARGET)
    enable_language(ASM_MASM)
    set(WINTOSH_I386_STARTUP_ASM
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/darling_windows_startup_i386.asm")
    set(WINTOSH_I386_STARTUP_OBJ
        "${CMAKE_CURRENT_BINARY_DIR}/darling_windows_startup_i386.obj")
    add_custom_command(
        OUTPUT "${WINTOSH_I386_STARTUP_OBJ}"
        COMMAND "${CMAKE_ASM_MASM_COMPILER}" /c /nologo
            "/Fo${WINTOSH_I386_STARTUP_OBJ}" "${WINTOSH_I386_STARTUP_ASM}"
        DEPENDS "${WINTOSH_I386_STARTUP_ASM}"
        VERBATIM)
endif()

if (WINTOSH_ARM64_TARGET AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(WINTOSH_ARM64_ASSEMBLER_TARGET "${CMAKE_CXX_COMPILER_TARGET}")
    if (NOT WINTOSH_ARM64_ASSEMBLER_TARGET)
        set(WINTOSH_ARM64_ASSEMBLER_TARGET "aarch64-pc-windows-msvc")
    endif()
    set(WINTOSH_ARM64_STARTUP_ASM
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/darling_windows_startup_arm64.S")
    set(WINTOSH_ARM64_OBJC_ASM
        "${CMAKE_CURRENT_SOURCE_DIR}/abi/darling_windows_objc_variadic_arm64.S")
    set(WINTOSH_ARM64_STARTUP_OBJ
        "${CMAKE_CURRENT_BINARY_DIR}/darling_windows_startup_arm64.obj")
    set(WINTOSH_ARM64_OBJC_OBJ
        "${CMAKE_CURRENT_BINARY_DIR}/darling_windows_objc_variadic_arm64.obj")
    add_custom_command(
        OUTPUT "${WINTOSH_ARM64_STARTUP_OBJ}"
        COMMAND "${CMAKE_CXX_COMPILER}" "--target=${WINTOSH_ARM64_ASSEMBLER_TARGET}" /c /nologo
            "/Fo${WINTOSH_ARM64_STARTUP_OBJ}" "${WINTOSH_ARM64_STARTUP_ASM}"
        DEPENDS "${WINTOSH_ARM64_STARTUP_ASM}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${WINTOSH_ARM64_OBJC_OBJ}"
        COMMAND "${CMAKE_CXX_COMPILER}" "--target=${WINTOSH_ARM64_ASSEMBLER_TARGET}" /c /nologo
            "/Fo${WINTOSH_ARM64_OBJC_OBJ}" "${WINTOSH_ARM64_OBJC_ASM}"
        DEPENDS "${WINTOSH_ARM64_OBJC_ASM}"
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
file(GLOB_RECURSE WINTOSH_HOST_SOURCES CONFIGURE_DEPENDS
    core/*.cpp platform/*.cpp abi/*.cpp loader/*.cpp runtime/*.cpp
    frameworks/*.cpp process/*.cpp
)
if (WINTOSH_X64_TARGET AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND WINTOSH_HOST_SOURCES ${WINTOSH_CLANG_ASM_OBJECTS})
elseif (WINTOSH_X64_TARGET)
    list(APPEND WINTOSH_HOST_SOURCES "${WINTOSH_STARTUP_OBJ}")
elseif (WINTOSH_I386_TARGET)
    list(APPEND WINTOSH_HOST_SOURCES "${WINTOSH_I386_STARTUP_OBJ}")
elseif (WINTOSH_ARM64_TARGET AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND WINTOSH_HOST_SOURCES
        "${WINTOSH_ARM64_STARTUP_OBJ}"
        "${WINTOSH_ARM64_OBJC_OBJ}")
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

if (WINTOSH_DYNARMIC_SOURCE_DIR OR WINTOSH_DYNARMIC_BUILD_DIR)
    if (NOT WINTOSH_X64_TARGET)
        message(FATAL_ERROR
            "The optional Dynarmic A64 guest backend currently targets x64 Windows hosts")
    endif()
    if (NOT WINTOSH_DYNARMIC_SOURCE_DIR OR NOT WINTOSH_DYNARMIC_BUILD_DIR)
        message(FATAL_ERROR
            "Set both WINTOSH_DYNARMIC_SOURCE_DIR and WINTOSH_DYNARMIC_BUILD_DIR")
    endif()
    if (NOT EXISTS "${WINTOSH_DYNARMIC_SOURCE_DIR}/src/dynarmic/interface/A64/a64.h")
        message(FATAL_ERROR "WINTOSH_DYNARMIC_SOURCE_DIR is not a Dynarmic checkout")
    endif()

    foreach(library IN ITEMS dynarmic fmt mcl Zydis Zycore)
        string(TOUPPER "${library}" library_upper)
        if (library STREQUAL "Zydis")
            set(library_directory "${WINTOSH_DYNARMIC_BUILD_DIR}/externals/zydis")
        elseif (library STREQUAL "Zycore")
            set(library_directory "${WINTOSH_DYNARMIC_BUILD_DIR}/externals/zydis/zycore")
        elseif (library STREQUAL "mcl")
            set(library_directory "${WINTOSH_DYNARMIC_BUILD_DIR}/externals/mcl/src")
        elseif (library STREQUAL "fmt")
            set(library_directory "${WINTOSH_DYNARMIC_BUILD_DIR}/externals/fmt")
        else()
            set(library_directory "${WINTOSH_DYNARMIC_BUILD_DIR}/src/dynarmic")
        endif()
        find_library(WINTOSH_DYNARMIC_${library_upper}_LIBRARY
            NAMES "${library}"
            PATHS "${library_directory}"
            NO_DEFAULT_PATH)
        if (NOT WINTOSH_DYNARMIC_${library_upper}_LIBRARY)
            message(FATAL_ERROR
                "Missing ${library} library in ${library_directory}; build Dynarmic first")
        endif()
        list(APPEND WINTOSH_DYNARMIC_LIBRARIES
            "${WINTOSH_DYNARMIC_${library_upper}_LIBRARY}")
    endforeach()

    target_sources(wintosh_windows_host PRIVATE runtime/darling_windows_a64_backend.cpp)
    target_include_directories(wintosh_windows_host PRIVATE
        "${WINTOSH_DYNARMIC_SOURCE_DIR}/src")
    target_compile_definitions(wintosh_windows_host PUBLIC WINTOSH_HAS_DYNARMIC=1)
    target_link_libraries(wintosh_windows_host PRIVATE ${WINTOSH_DYNARMIC_LIBRARIES})
endif()

if (WINTOSH_ARM64_TARGET AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_definitions(wintosh_windows_host PRIVATE
        DARLING_ARM64_GUEST_BRIDGE=1)
endif()

if (WIN32)
    target_compile_definitions(wintosh_windows_host PUBLIC
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(wintosh_windows_host PUBLIC
        kernel32 advapi32 psapi ws2_32 ole32 shell32 uuid windowscodecs user32 gdi32 comctl32 comdlg32
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
    # The dyld smoke launches the CLI from its own output directory.
    add_dependencies(wintosh_dyld_smoke wintosh)

    if (WINTOSH_X64_TARGET AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        get_filename_component(WINTOSH_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(WINTOSH_LD64_LLD "${WINTOSH_LLVM_BIN}/ld64.lld.exe")
        set(WINTOSH_LLVM_OBJDUMP "${WINTOSH_LLVM_BIN}/llvm-objdump.exe")
        add_test(NAME wintosh_real_import_lazy_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/macho_real_import_lazy_e2e.ps1"
                -Clang "${CMAKE_CXX_COMPILER}"
                -Linker "${WINTOSH_LD64_LLD}"
                -Objdump "${WINTOSH_LLVM_OBJDUMP}"
                -Runner "$<TARGET_FILE:wintosh>"
                -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/real-import-lazy-e2e")
        set_tests_properties(wintosh_real_import_lazy_e2e PROPERTIES RUN_SERIAL TRUE)
        if (WINTOSH_DYNARMIC_SOURCE_DIR AND WINTOSH_DYNARMIC_BUILD_DIR)
            add_test(NAME wintosh_arm64_host_import_e2e
                COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm64_import_e2e.ps1"
                    -Clang "${CMAKE_CXX_COMPILER}"
                    -Linker "${WINTOSH_LD64_LLD}"
                    -Runner "$<TARGET_FILE:wintosh>"
                    -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/arm64-host-import-e2e")
            set_tests_properties(wintosh_arm64_host_import_e2e
                PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
        endif()
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
        add_test(NAME wintosh_corefoundation_observer_e2e
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/corefoundation_observer_e2e.ps1"
                -Clang "${CMAKE_CXX_COMPILER}"
                -Linker "${WINTOSH_LD64_LLD}"
                -Runner "$<TARGET_FILE:wintosh>"
                -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/corefoundation-observer-e2e")
        set_tests_properties(wintosh_corefoundation_observer_e2e
            PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
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
    if (WINTOSH_X64_TARGET AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
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
