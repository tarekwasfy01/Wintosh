set (SOURCE_FILES
    # From `unicorn.vcxproj`
    list.c
    qemu/accel.c
    qemu/glib_compat.c
    qemu/hw/core/machine.c
    qemu/hw/core/qdev.c
    qemu/qapi/qapi-dealloc-visitor.c
    qemu/qapi/qapi-visit-core.c
    qemu/qapi/qmp-input-visitor.c
    qemu/qapi/qmp-output-visitor.c
    qemu/qapi/string-input-visitor.c
    qemu/qemu-log.c
    qemu/qemu-timer.c
    qemu/qobject/qbool.c
    qemu/qobject/qdict.c
    qemu/qobject/qerror.c
    qemu/qobject/qfloat.c
    qemu/qobject/qint.c
    qemu/qobject/qlist.c
    qemu/qobject/qstring.c
    qemu/qom/container.c
    qemu/qom/cpu.c
    qemu/qom/object.c
    qemu/qom/qom-qobject.c
    qemu/tcg-runtime.c
    qemu/util/aes.c
    qemu/util/bitmap.c
    qemu/util/bitops.c
    qemu/util/crc32c.c
    qemu/util/cutils.c
    qemu/util/error.c
    qemu/util/getauxval.c
    qemu/util/host-utils.c
    qemu/util/module.c
    qemu/util/oslib-win32.c
    qemu/util/qemu-error.c
    qemu/util/qemu-thread-win32.c
    qemu/util/qemu-timer-common.c
    qemu/vl.c
    uc.c)

add_library (unicorn SHARED ${SOURCE_FILES})
add_prep_dep (unicorn)

target_include_directories (unicorn
    # From `unicorn.vcxproj`
    PRIVATE msvc/unicorn qemu qemu/include qemu/tcg)
library_headers (unicorn
    # From `unicorn.vcxproj`
    include)

target_compile_definitions (unicorn PRIVATE
    # From `unicorn.vcxproj`
    WIN32
    $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
    _WINDOWS
    _USRDLL
    UNICORN_DLL_EXPORTS
    UNICORN_SHARED
    _CRT_SECURE_NO_WARNINGS
    inline=__inline
    __func__=__FUNCTION__
    __x86_64__
    UNICORN_HAS_ARM
    # UNICORN_HAS_ARM64
    # UNICORN_HAS_M68K
    # UNICORN_HAS_MIPS
    # UNICORN_HAS_MIPS64
    # UNICORN_HAS_MIPSEL
    # UNICORN_HAS_MIPS64EL
    # UNICORN_HAS_SPARC
    # UNICORN_HAS_X86
    WIN32_LEAN_AND_MEAN)

add_library (arm-softmmu STATIC
    # From `arm-softmmu.vcxproj`
    qemu/cpu-exec.c
    qemu/cpus.c
    qemu/cputlb.c
    qemu/exec.c
    qemu/fpu/softfloat.c
    qemu/hw/arm/tosa.c
    qemu/hw/arm/virt.c
    qemu/ioport.c
    qemu/memory.c
    qemu/memory_mapping.c
    qemu/target-arm/cpu.c
    qemu/target-arm/crypto_helper.c
    qemu/target-arm/helper.c
    qemu/target-arm/iwmmxt_helper.c
    qemu/target-arm/neon_helper.c
    qemu/target-arm/op_helper.c
    qemu/target-arm/psci.c
    qemu/target-arm/translate.c
    qemu/target-arm/unicorn_arm.c
    qemu/tcg/optimize.c
    qemu/tcg/tcg.c
    qemu/translate-all.c)
add_prep_dep (arm-softmmu)

target_include_directories (arm-softmmu PRIVATE
    # From `arm-softmmu.vcxproj`
    msvc/unicorn/arm-softmmu msvc/unicorn qemu qemu/include qemu/tcg
    qemu/tcg/i386 qemu/target-arm include)

target_compile_definitions (arm-softmmu PRIVATE
    # From `arm-softmmu.vcxproj`
    WIN32
    $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
    _LIB
    __i386__
    _CRT_SECURE_NO_WARNINGS
    inline=__inline
    __func__=__FUNCTION__
    NEED_CPU_H
    WIN32_LEAN_AND_MEAN)

target_compile_options (arm-softmmu PRIVATE
    -Wno-macro-redefined
    # From `arm-softmmu.vcxproj`
    -includearm.h)

add_library (armeb-softmmu STATIC
    # From `armeb-softmmu.vcxproj`
    qemu/cpu-exec.c
    qemu/cpus.c
    qemu/cputlb.c
    qemu/exec.c
    qemu/fpu/softfloat.c
    qemu/hw/arm/tosa.c
    qemu/hw/arm/virt.c
    qemu/ioport.c
    qemu/memory.c
    qemu/memory_mapping.c
    qemu/target-arm/cpu.c
    qemu/target-arm/crypto_helper.c
    qemu/target-arm/helper.c
    qemu/target-arm/iwmmxt_helper.c
    qemu/target-arm/neon_helper.c
    qemu/target-arm/op_helper.c
    qemu/target-arm/psci.c
    qemu/target-arm/translate.c
    qemu/target-arm/unicorn_arm.c
    qemu/tcg/optimize.c
    qemu/tcg/tcg.c
    qemu/translate-all.c)
add_prep_dep (armeb-softmmu)

target_include_directories (armeb-softmmu PRIVATE
    # From `armeb-softmmu.vcxproj`
    msvc/unicorn/armeb-softmmu msvc/unicorn qemu qemu/include qemu/tcg
    qemu/tcg/i386 qemu/target-arm include)

target_compile_definitions (armeb-softmmu PRIVATE
    # From `armeb-softmmu.vcxproj`
    WIN32
    $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
    _LIB
    __i386__
    _CRT_SECURE_NO_WARNINGS
    inline=__inline
    __func__=__FUNCTION__
    NEED_CPU_H
    WIN32_LEAN_AND_MEAN)

target_compile_options (armeb-softmmu PRIVATE
    -Wno-macro-redefined
    # From `armeb-softmmu.vcxproj`
    -includearmeb.h)

# From `unicorn.vcxproj`
target_link_libraries (unicorn arm-softmmu armeb-softmmu)
