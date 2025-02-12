/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

/**
 * This fuzz target performs a zstd round-trip test (compress & decompress) with
 * a dictionary, compares the result with the original, and calls abort() on
 * corruption.
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fuzz_helpers.h"
#include "zstd_helpers.h"

static const int kMaxClevel = 19;

static ZSTD_CCtx *cctx = NULL;
static ZSTD_DCtx *dctx = NULL;
static uint32_t seed;

static size_t roundTripTest(void *result, size_t resultCapacity,
                            void *compressed, size_t compressedCapacity,
                            const void *src, size_t srcSize)
{
    ZSTD_dictContentType_e dictContentType = ZSTD_dct_auto;
    FUZZ_dict_t dict = FUZZ_train(src, srcSize, &seed);
    size_t cSize;
    if ((FUZZ_rand(&seed) & 15) == 0) {
        int const cLevel = FUZZ_rand(&seed) % kMaxClevel;

        cSize = ZSTD_compress_usingDict(cctx,
                compressed, compressedCapacity,
                src, srcSize,
                dict.buff, dict.size,
                cLevel);
    } else {
        dictContentType = FUZZ_rand32(&seed, 0, 2);
        FUZZ_setRandomParameters(cctx, srcSize, &seed);
        /* Disable checksum so we can use sizes smaller than compress bound. */
        FUZZ_ZASSERT(ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0));
        FUZZ_ZASSERT(ZSTD_CCtx_loadDictionary_advanced(
                cctx, dict.buff, dict.size,
                (ZSTD_dictLoadMethod_e)FUZZ_rand32(&seed, 0, 1),
                dictContentType));
        cSize = ZSTD_compress2(cctx, compressed, compressedCapacity, src, srcSize);
    }
    FUZZ_ZASSERT(cSize);
    FUZZ_ZASSERT(ZSTD_DCtx_loadDictionary_advanced(
        dctx, dict.buff, dict.size,
        (ZSTD_dictLoadMethod_e)FUZZ_rand32(&seed, 0, 1),
        dictContentType));
    {
        size_t const ret = ZSTD_decompressDCtx(
                dctx, result, resultCapacity, compressed, cSize);
        free(dict.buff);
        return ret;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *src, size_t size)
{
    size_t const rBufSize = size;
    void* rBuf = malloc(rBufSize);
    size_t cBufSize = ZSTD_compressBound(size);
    void* cBuf;

    seed = FUZZ_seed(&src, &size);
    /* Half of the time fuzz with a 1 byte smaller output size.
     * This will still succeed because we force the checksum to be disabled,
     * giving us 4 bytes of overhead.
     */
    cBufSize -= FUZZ_rand32(&seed, 0, 1);
    cBuf = malloc(cBufSize);

    if (!cctx) {
        cctx = ZSTD_createCCtx();
        FUZZ_ASSERT(cctx);
    }
    if (!dctx) {
        dctx = ZSTD_createDCtx();
        FUZZ_ASSERT(dctx);
    }

    {
        size_t const result =
            roundTripTest(rBuf, rBufSize, cBuf, cBufSize, src, size);
        FUZZ_ZASSERT(result);
        FUZZ_ASSERT_MSG(result == size, "Incorrect regenerated size");
        FUZZ_ASSERT_MSG(!memcmp(src, rBuf, size), "Corruption!");
    }
    free(rBuf);
    free(cBuf);
#ifndef STATEFUL_FUZZING
    ZSTD_freeCCtx(cctx); cctx = NULL;
    ZSTD_freeDCtx(dctx); dctx = NULL;
#endif
    return 0;
}
