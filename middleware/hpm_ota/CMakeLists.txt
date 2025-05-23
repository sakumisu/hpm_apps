# Copyright (c) 2021-2024 HPMicro
# SPDX-License-Identifier: BSD-3-Clause
sdk_inc(inc)
sdk_inc(core)
sdk_inc(core/bsdiff)
sdk_inc(core/lzma)

sdk_src(core/hpm_ota.c)
sdk_src(core/hpm_appheader.c)

sdk_inc(port)
sdk_src(port/hpm_flash.c)
sdk_src(port/hpm_hashs.c)
add_subdirectory(port)

if(DEFINED CONFIG_USE_RAM_TYPE)
if (${CONFIG_USE_RAM_TYPE})
    sdk_compile_definitions("-DCONFIG_USE_RAM_TYPE=1")
endif()
endif()

if(DEFINED CONFIG_USE_FLASH_1M)
    if (${CONFIG_USE_FLASH_1M})
        sdk_compile_definitions("-DCONFIG_USE_FLASH_1M=1")
    endif()
endif()

if(DEFINED CONFIG_DIFF_IMAGE_ENABLE)
    if (${CONFIG_DIFF_IMAGE_ENABLE})
        sdk_compile_definitions("-DCONFIG_DIFF_IMAGE_ENABLE=1")
        sdk_inc(core/bsdiff)
        sdk_inc(core/lzma)
        sdk_src(core/bsdiff/bspatch.c)
        sdk_src(core/bsdiff/vFile.c)
        sdk_src(core/lzma/7zFile.c)
        sdk_src(core/lzma/LzFind.c)
        sdk_src(core/lzma/lzma_decompress.c)
        sdk_src(core/lzma/LzmaDec.c)
        sdk_src(core/lzma/LzmaEnc.c)
        sdk_src(core/hpm_diffmgr.c)
        sdk_src(core/hpm_lzma.c)
    endif()
endif()