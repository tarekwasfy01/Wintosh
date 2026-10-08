if (CL_COMPILER)
    set (SOURCE_FILES
        # From `StarboardLib.vcxproj`.
        RawBuffer.cpp
        EbrStorageFile.cpp
        PathMapper.cpp
        AssetFile.cpp)
else (CL_COMPILER)
    set (SOURCE_FILES
        # From `StarboardLib.vcxproj`.
        #pthread.cpp # We use win-pthreads instead.
        CommonDigest.cpp
        CommonHMAC.cpp
        MurmurHash3.cpp
        pevents.cpp
        String.cpp
        PlatformSupport.cpp
        CommonCryptor.cpp
        DNSService.cpp
        EbrFile.cpp
        EbrIOFile.cpp
        EbrDevRandomFile.cpp)
endif (CL_COMPILER)

if (CL_COMPILER)
    add_library (Starboard STATIC ${SOURCE_FILES})
else (CL_COMPILER)
    add_library (Starboard SHARED ${SOURCE_FILES})
    woc_framework (Starboard)
endif (CL_COMPILER)

target_link_libraries (Starboard PRIVATE
    ${WINOBJC_LIBS}
    pthread)
if (CL_COMPILER)
    target_link_libraries (Starboard PRIVATE
        # From `sdk-build.props`.
        # TODO: Share those.
        WOCStdLib
        Logging)
else (CL_COMPILER)
    target_link_libraries (Starboard PRIVATE
        woc-WOCStdLib
        woc-Logging
        woc-Starboard
        # From `sdk-build.props`.
        -Wl,/force:multiple
        # TODO: Where does this come from?
        -Wl,/nodefaultlib:libcmt
        # From `Starboard.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Starboard/dll/\
Starboard.def")
endif (CL_COMPILER)

target_include_directories (Starboard PRIVATE
    ${WINOBJC_INCLUDE_DIRS}
    # From `StarboardLib.vcxproj`.
    ../include)

target_compile_definitions (Starboard PRIVATE
    ${WINOBJC_DEFS}
    # From `StarboardLib.vcxproj`.
    STARBOARD_PORT=1
    SB_IMPEXP=)

if (NOT CL_COMPILER)
    target_compile_options (Starboard PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # Warnings
        # TODO: Where do these come from?
        -Wno-c++17-extensions -Wno-nullability-completeness
        -Wno-unused-command-line-argument -Wno-c++17-compat-mangling
        -Wno-microsoft)
endif (NOT CL_COMPILER)
