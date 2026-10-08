if (CL_COMPILER)
    set (SOURCE_FILES
        # From `AccelerateLib.vcxproj`.
        ../../deps/3rdparty/CBLAS/cblas_caxpy.c
        ../../deps/3rdparty/CBLAS/cblas_ccopy.c
        ../../deps/3rdparty/CBLAS/cblas_cdotc_sub.c
        ../../deps/3rdparty/CBLAS/cblas_cdotu_sub.c
        ../../deps/3rdparty/CBLAS/cblas_cgbmv.c
        ../../deps/3rdparty/CBLAS/cblas_cgemm.c
        ../../deps/3rdparty/CBLAS/cblas_cgemv.c
        ../../deps/3rdparty/CBLAS/cblas_cgerc.c
        ../../deps/3rdparty/CBLAS/cblas_cgeru.c
        ../../deps/3rdparty/CBLAS/cblas_chemm.c
        ../../deps/3rdparty/CBLAS/cblas_chemv.c
        ../../deps/3rdparty/CBLAS/cblas_cher.c
        ../../deps/3rdparty/CBLAS/cblas_cher2.c
        ../../deps/3rdparty/CBLAS/cblas_cher2k.c
        ../../deps/3rdparty/CBLAS/cblas_cherk.c
        ../../deps/3rdparty/CBLAS/cblas_chpr.c
        ../../deps/3rdparty/CBLAS/cblas_chpr2.c
        ../../deps/3rdparty/CBLAS/cblas_cscal.c
        ../../deps/3rdparty/CBLAS/cblas_csscal.c
        ../../deps/3rdparty/CBLAS/cblas_cswap.c
        ../../deps/3rdparty/CBLAS/cblas_csymm.c
        ../../deps/3rdparty/CBLAS/cblas_csyr2k.c
        ../../deps/3rdparty/CBLAS/cblas_csyrk.c
        ../../deps/3rdparty/CBLAS/cblas_ctbsv.c
        ../../deps/3rdparty/CBLAS/cblas_ctpmv.c
        ../../deps/3rdparty/CBLAS/cblas_ctpsv.c
        ../../deps/3rdparty/CBLAS/cblas_ctrmm.c
        ../../deps/3rdparty/CBLAS/cblas_ctrmv.c
        ../../deps/3rdparty/CBLAS/cblas_ctrsm.c
        ../../deps/3rdparty/CBLAS/cblas_ctrsv.c
        ../../deps/3rdparty/CBLAS/cblas_dasum.c
        ../../deps/3rdparty/CBLAS/cblas_daxpy.c
        ../../deps/3rdparty/CBLAS/cblas_dcopy.c
        ../../deps/3rdparty/CBLAS/cblas_ddot.c
        ../../deps/3rdparty/CBLAS/cblas_dgbmv.c
        ../../deps/3rdparty/CBLAS/cblas_dgemm.c
        ../../deps/3rdparty/CBLAS/cblas_dgemv.c
        ../../deps/3rdparty/CBLAS/cblas_dger.c
        ../../deps/3rdparty/CBLAS/cblas_dnrm2.c
        ../../deps/3rdparty/CBLAS/cblas_drot.c
        ../../deps/3rdparty/CBLAS/cblas_drotg.c
        ../../deps/3rdparty/CBLAS/cblas_dscal.c
        ../../deps/3rdparty/CBLAS/cblas_dspr.c
        ../../deps/3rdparty/CBLAS/cblas_dspr2.c
        ../../deps/3rdparty/CBLAS/cblas_dswap.c
        ../../deps/3rdparty/CBLAS/cblas_dsymm.c
        ../../deps/3rdparty/CBLAS/cblas_dsymv.c
        ../../deps/3rdparty/CBLAS/cblas_dsyr.c
        ../../deps/3rdparty/CBLAS/cblas_dsyr2.c
        ../../deps/3rdparty/CBLAS/cblas_dsyr2k.c
        ../../deps/3rdparty/CBLAS/cblas_dsyrk.c
        ../../deps/3rdparty/CBLAS/cblas_dtbsv.c
        ../../deps/3rdparty/CBLAS/cblas_dtpmv.c
        ../../deps/3rdparty/CBLAS/cblas_dtpsv.c
        ../../deps/3rdparty/CBLAS/cblas_dtrmm.c
        ../../deps/3rdparty/CBLAS/cblas_dtrmv.c
        ../../deps/3rdparty/CBLAS/cblas_dtrsm.c
        ../../deps/3rdparty/CBLAS/cblas_dtrsv.c
        ../../deps/3rdparty/CBLAS/cblas_dzasum.c
        ../../deps/3rdparty/CBLAS/cblas_dznrm2.c
        ../../deps/3rdparty/CBLAS/cblas_icamax.c
        ../../deps/3rdparty/CBLAS/cblas_idamax.c
        ../../deps/3rdparty/CBLAS/cblas_isamax.c
        ../../deps/3rdparty/CBLAS/cblas_izamax.c
        ../../deps/3rdparty/CBLAS/cblas_sasum.c
        ../../deps/3rdparty/CBLAS/cblas_saxpy.c
        ../../deps/3rdparty/CBLAS/cblas_scasum.c
        ../../deps/3rdparty/CBLAS/cblas_scnrm2.c
        ../../deps/3rdparty/CBLAS/cblas_scopy.c
        ../../deps/3rdparty/CBLAS/cblas_sdot.c
        ../../deps/3rdparty/CBLAS/cblas_sgbmv.c
        ../../deps/3rdparty/CBLAS/cblas_sgemm.c
        ../../deps/3rdparty/CBLAS/cblas_sgemv.c
        ../../deps/3rdparty/CBLAS/cblas_sger.c
        ../../deps/3rdparty/CBLAS/cblas_snrm2.c
        ../../deps/3rdparty/CBLAS/cblas_srot.c
        ../../deps/3rdparty/CBLAS/cblas_srotg.c
        ../../deps/3rdparty/CBLAS/cblas_sscal.c
        ../../deps/3rdparty/CBLAS/cblas_sspr.c
        ../../deps/3rdparty/CBLAS/cblas_sspr2.c
        ../../deps/3rdparty/CBLAS/cblas_sswap.c
        ../../deps/3rdparty/CBLAS/cblas_ssymm.c
        ../../deps/3rdparty/CBLAS/cblas_ssymv.c
        ../../deps/3rdparty/CBLAS/cblas_ssyr.c
        ../../deps/3rdparty/CBLAS/cblas_ssyr2.c
        ../../deps/3rdparty/CBLAS/cblas_ssyr2k.c
        ../../deps/3rdparty/CBLAS/cblas_ssyrk.c
        ../../deps/3rdparty/CBLAS/cblas_stbsv.c
        ../../deps/3rdparty/CBLAS/cblas_stpmv.c
        ../../deps/3rdparty/CBLAS/cblas_stpsv.c
        ../../deps/3rdparty/CBLAS/cblas_strmm.c
        ../../deps/3rdparty/CBLAS/cblas_strmv.c
        ../../deps/3rdparty/CBLAS/cblas_strsm.c
        ../../deps/3rdparty/CBLAS/cblas_strsv.c
        ../../deps/3rdparty/CBLAS/cblas_zaxpy.c
        ../../deps/3rdparty/CBLAS/cblas_zcopy.c
        ../../deps/3rdparty/CBLAS/cblas_zdotc_sub.c
        ../../deps/3rdparty/CBLAS/cblas_zdotu_sub.c
        ../../deps/3rdparty/CBLAS/cblas_zdscal.c
        ../../deps/3rdparty/CBLAS/cblas_zgbmv.c
        ../../deps/3rdparty/CBLAS/cblas_zgemm.c
        ../../deps/3rdparty/CBLAS/cblas_zgemv.c
        ../../deps/3rdparty/CBLAS/cblas_zgerc.c
        ../../deps/3rdparty/CBLAS/cblas_zgeru.c
        ../../deps/3rdparty/CBLAS/cblas_zhemm.c
        ../../deps/3rdparty/CBLAS/cblas_zhemv.c
        ../../deps/3rdparty/CBLAS/cblas_zher.c
        ../../deps/3rdparty/CBLAS/cblas_zher2.c
        ../../deps/3rdparty/CBLAS/cblas_zher2k.c
        ../../deps/3rdparty/CBLAS/cblas_zherk.c
        ../../deps/3rdparty/CBLAS/cblas_zhpr.c
        ../../deps/3rdparty/CBLAS/cblas_zhpr2.c
        ../../deps/3rdparty/CBLAS/cblas_zscal.c
        ../../deps/3rdparty/CBLAS/cblas_zswap.c
        ../../deps/3rdparty/CBLAS/cblas_zsymm.c
        ../../deps/3rdparty/CBLAS/cblas_zsyr2k.c
        ../../deps/3rdparty/CBLAS/cblas_zsyrk.c
        ../../deps/3rdparty/CBLAS/cblas_ztbsv.c
        ../../deps/3rdparty/CBLAS/cblas_ztpmv.c
        ../../deps/3rdparty/CBLAS/cblas_ztpsv.c
        ../../deps/3rdparty/CBLAS/cblas_ztrmm.c
        ../../deps/3rdparty/CBLAS/cblas_ztrmv.c
        ../../deps/3rdparty/CBLAS/cblas_ztrsm.c
        ../../deps/3rdparty/CBLAS/cblas_ztrsv.c
        ../../deps/3rdparty/CBLAS/xerbla.c
        blasCDouble.cpp
        blasCFloat.cpp
        blasDouble.cpp
        blasFloat.cpp
        cblas_globals.c
        cblas_xerbla.c
        vDSP.cpp
        vImage.cpp)

    add_library (Accelerate STATIC ${SOURCE_FILES})

    target_include_directories (Accelerate PRIVATE
        ${WOCFX_INCLUDE_DIRS}
        # From `AccelerateLib.vcxproj`.
        ../../deps/3rdparty)

    target_compile_definitions (Accelerate PRIVATE
        ${WINOBJC_DEFS}
        # From `AccelerateLib.vcxproj`.
        ACCELERATE_IMPEXP=
        EIGEN_MPL2_ONLY)

    target_link_libraries (Accelerate PRIVATE
        Logging)

else (CL_COMPILER)

    set (SOURCE_FILES
        # From `AccelerateLib.vcxproj`.
        vImage.mm
        ErrorHandler.mm)

    add_library (Accelerate SHARED ${SOURCE_FILES})
    woc_framework (Accelerate)

    target_include_directories (Accelerate PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (Accelerate PRIVATE
        ${WINOBJC_DEFS}
        # From `AccelerateLib.vcxproj`.
        ACCELERATE_IMPEXP=)

    target_compile_options (Accelerate PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (Accelerate PRIVATE
        ${WOCFX_LIBS}
        # From `Accelerate.vcxproj`.
        woc-Accelerate
        CoreGraphics
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/Accelerate/dll/\
Accelerate.def")

# NOT CL_COMPILER
endif (CL_COMPILER)
