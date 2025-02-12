/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include <stddef.h>
#include "zstd.h"

static const char invalidRepCode[] = {
  0x37, 0xa4, 0x30, 0xec, 0x2a, 0x00, 0x00, 0x00, 0x39, 0x10, 0xc0, 0xc2,
  0xa6, 0x00, 0x0c, 0x30, 0xc0, 0x00, 0x03, 0x0c, 0x30, 0x20, 0x72, 0xf8,
  0xb4, 0x6d, 0x4b, 0x9f, 0xfc, 0x97, 0x29, 0x49, 0xb2, 0xdf, 0x4b, 0x29,
  0x7d, 0x4a, 0xfc, 0x83, 0x18, 0x22, 0x75, 0x23, 0x24, 0x44, 0x4d, 0x02,
  0xb7, 0x97, 0x96, 0xf6, 0xcb, 0xd1, 0xcf, 0xe8, 0x22, 0xea, 0x27, 0x36,
  0xb7, 0x2c, 0x40, 0x46, 0x01, 0x08, 0x23, 0x01, 0x00, 0x00, 0x06, 0x1e,
  0x3c, 0x83, 0x81, 0xd6, 0x18, 0xd4, 0x12, 0x3a, 0x04, 0x00, 0x80, 0x03,
  0x08, 0x0e, 0x12, 0x1c, 0x12, 0x11, 0x0d, 0x0e, 0x0a, 0x0b, 0x0a, 0x09,
  0x10, 0x0c, 0x09, 0x05, 0x04, 0x03, 0x06, 0x06, 0x06, 0x02, 0x00, 0x03,
  0x00, 0x00, 0x02, 0x02, 0x00, 0x04, 0x06, 0x03, 0x06, 0x08, 0x24, 0x6b,
  0x0d, 0x01, 0x10, 0x04, 0x81, 0x07, 0x00, 0x00, 0x04, 0xb9, 0x58, 0x18,
  0x06, 0x59, 0x92, 0x43, 0xce, 0x28, 0xa5, 0x08, 0x88, 0xc0, 0x80, 0x88,
  0x8c, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x08, 0x00, 0x00, 0x00
};

typedef struct dictionary_s {
  const char *data;
  size_t size;
} dictionary;

static const dictionary dictionaries[] = {
  {invalidRepCode, sizeof(invalidRepCode)},
  {NULL, 0},
};

int main(int argc, const char** argv) {
  const dictionary *dict;
  for (dict = dictionaries; dict->data != NULL; ++dict) {
    ZSTD_CDict *cdict;
    ZSTD_DDict *ddict;
    cdict = ZSTD_createCDict(dict->data, dict->size, 1);
    if (cdict) {
      ZSTD_freeCDict(cdict);
      return 1;
    }
    ddict = ZSTD_createDDict(dict->data, dict->size);
    if (ddict) {
      ZSTD_freeDDict(ddict);
      return 2;
    }
  }

  (void)argc;
  (void)argv;
  return 0;
}
