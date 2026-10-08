set (SOURCE_FILES
    # From `libdispatch.vcxproj`
    platform/windows/libkern/OSAtomic.c
    platform/windows/platform.c
    platform/windows/pthread.c
    platform/windows/semaphore.c
    platform/windows/stdio.c
    platform/windows/strings.c
    platform/windows/sys/time.c
    platform/windows/time.c
    platform/windows/unistd.c
    src/queue.c
    src/queue_kevent.c
    src/semaphore.c
    src/shims/time.c
    src/shims/tsd.c
    src/source.c
    src/source_kevent.c
    src/time.c
    platform/windows/pthread_workqueue_winrt.cpp
    src/apply.c
    src/benchmark.c
    src/continuation_cache.c
    src/debug.c
    src/interop.c
    src/legacy.c
    src/object.c
    src/once.c
    src/shims/mach.c)

add_library (dispatch SHARED ${SOURCE_FILES})
add_prep_dep (dispatch)

target_compile_definitions (dispatch PRIVATE
    # From `/src/objc/CMakeLists.txt`
    OBJC_PORT
    __OBJC2__=1
    # From `libdispatch.vcxproj`
    WIN32
    $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
    $<$<CONFIG:DEBUG>:DISPATCH_DEBUG>
    _WINDLL
    _LIB
    HAS_TIMESPEC
    # Probably `ClangCompile`'s defaults
    WINOBJC)
set_source_files_properties (
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/pthread_workqueue_winrt.cpp"
    PROPERTIES COMPILE_OPTIONS -UWINOBJC)

target_include_directories (dispatch PRIVATE
    # From `libdispatch.vcxproj`
    "${CMAKE_CURRENT_SOURCE_DIR}"
    # From `/src/objc/CMakeLists.txt`
    "${SOURCE_DIR}/src/objc/include"
    "${SOURCE_DIR}/deps/apple-headers"
    "${SOURCE_DIR}/deps/libclosure")

target_compile_options (dispatch PRIVATE -Wno-nullability-completeness
    -Wno-incompatible-pointer-types
    # From `ClangCompile`'s defaults
    -fblocks
    # From `/src/objc/CMakeLists.txt`
    -fobjc-runtime=macosx-10.13.0)

target_link_libraries (dispatch PRIVATE
    $<IF:$<CONFIG:Debug>,msvcrtd,msvcrt> WindowsApp.lib User32.lib objc
    IpaSimLibrary)
