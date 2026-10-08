set (SOURCE_FILES
    # From `WOCStdLibLib.vcxproj`.
    lib/UTSName.cpp
    lib/mach.cpp
    lib/String.cpp
    lib/TaskInfo.cpp
    lib/unistd.cpp
    lib/sched.cpp
    lib/sysctl.cpp
    lib/version.cpp
    lib/dlfcn.cpp
    lib/OSMisc.cpp
    lib/OSEndian.cpp
    lib/inet.cpp
    lib/random.cpp
    lib/time.cpp
    lib/OSSpinLock.cpp)

add_library (WOCStdLib SHARED ${SOURCE_FILES})
add_prep_dep (WOCStdLib)

target_include_directories (WOCStdLib PRIVATE
    # From `WOCStdLibLib.vcxproj`.
    ../include/WOCStdLib
    ../include)

target_link_libraries (WOCStdLib PRIVATE
    ${WINOBJC_LIBS}
    # From `WOCStdLib.vcxproj`.
    Logging
    "-def:${CMAKE_CURRENT_SOURCE_DIR}/dll/WOCStdLib.def")

target_compile_definitions (WOCStdLib PRIVATE
    ${WINOBJC_DEFS})
