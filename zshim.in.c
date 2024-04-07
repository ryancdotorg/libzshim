/* SPDX-License-Identifier: AGPL-3.0-or-later
 * This file is part of `libzshim` - yet another fine LD_PRELOAD hack.
 * Copyright ©2024 Ryan Castellucci <https://rya.nc/>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>. */

#define _GNU_SOURCE
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <zlib.h>
// libzopfli.so.1
#include <zopfli/zopfli.h>

#include "debugp.h"
#include "dlutil.h"

#define SHIM_MAGIC 0x00006c645348494dULL

typedef struct z_shim_s {
  uint64_t        magic;
  unsigned char   *ibuf;
  size_t          ibuf_sz;
  size_t          ibuf_off;

  unsigned char   *obuf;
  size_t          obuf_sz;
  size_t          obuf_off;

  ZopfliFormat    format;
  int             state;

  voidpf          opaque;
} z_shim;

typedef z_shim *z_shimp;

void free_z_shim(z_shimp);

void wrap_z_streamp(z_streamp strm, z_shimp shim) {
  if (shim != NULL) {
    if (strm->opaque != NULL) {
      uint64_t magic = ((z_shimp)strm->opaque)->magic;
      if (magic == SHIM_MAGIC) {
        free_z_shim(strm->opaque);
        strm->opaque = NULL;
      } else {
        debugp("strm->opaque: %p\n", (void*)strm->opaque);
      }
    }
    shim->opaque = strm->opaque;
    strm->opaque = shim;
  }
}

z_shimp unwrap_z_streamp(z_streamp strm) {
  z_shimp shim = strm->opaque;
  if (shim == NULL || shim->magic != SHIM_MAGIC) return NULL;
  strm->opaque = shim->opaque;
  return shim;
}

z_shimp init_z_shim(z_streamp strm, z_shimp shim) {
  if (NULL == shim) shim = unwrap_z_streamp(strm);

  if (NULL == shim) {
    if (NULL == (shim = malloc(sizeof(z_shim)))) {
      fprintf(stderr, "libzshim: could not allocate z_shim struct\n");
      abort();
    }

    debugp("allocated z_shim struct: %p", (void*)shim);

    // set up fresh struct
    shim->magic = SHIM_MAGIC;

    shim->ibuf_sz = 1<<24; // 16MiB
    if (NULL == (shim->ibuf = malloc(shim->ibuf_sz))) {
      abort(); // 16MiB
    }
  } else if (NULL != shim->obuf) {
    free(shim->obuf);
  }

  shim->ibuf_off = 0;

  shim->obuf = NULL;
  shim->obuf_sz = 0;
  shim->obuf_off = 0;

  shim->state = 0;

  wrap_z_streamp(strm, shim);

  return shim;
}

void free_z_shim(z_shimp shim) {
  if (shim != NULL) {
    free(shim->ibuf);
    shim->ibuf = NULL;
    if (shim->obuf != NULL) {
      free(shim->obuf);
      shim->obuf = NULL;
    }
    free(shim);
    debugp("freed z_shim struct: %p", (void*)shim);
  }
}


ZopfliFormat get_bits_format(int windowBits) {
  if (windowBits < 0) {
    return ZOPFLI_FORMAT_DEFLATE;
  } else if (windowBits > 15) {
    return ZOPFLI_FORMAT_GZIP;
  } else {
    return ZOPFLI_FORMAT_ZLIB;
  }
}

DLFUNC(ZopfliInitOptions, void,
  STR_LIST("libzopfli.so.1"),
  (ZopfliOptions *a),
(a))

DLFUNC(ZopfliCompress, void,
    STR_LIST("libzopfli.so.1"),
    (const ZopfliOptions *a, ZopfliFormat b, const unsigned char *c, size_t d, unsigned char **e, size_t *f),
(a, b, c, d, e, f))

char *flush_str[] = {
  "Z_NO_FLUSH", "Z_PARTIAL_FLUSH", "Z_SYNC_FLUSH",
  "Z_FULL_FLUSH", "Z_FINISH", "Z_BLOCK", "Z_TREES"
};

//+ int deflate(z_streamp strm, int flush);
DLWRAP(deflate, int,
  (z_streamp a, int b),
  (a, b)
)

int wrap_deflate(z_streamp strm, int flush) {
  if (flush != Z_NO_FLUSH && flush != Z_FINISH && flush >= 0 && flush <= 6) {
    debugp("deflate(%p, %s)\n", (void *)strm, flush_str[flush]);
  } else if (flush < 0 || flush > 6) {
    debugp("deflate(%p, %d)\n", (void *)strm, flush);
  }

  z_shimp shim = unwrap_z_streamp(strm);
  if (NULL == shim) {
    debugp("z_streamp unwrap failed");
    return _real_deflate(strm, flush);
  } else if (shim->state == -1) {
    int ret = _real_deflate(strm, flush);
    wrap_z_streamp(strm, shim);
    return ret;
  }

  // are we being fed data?
  if (strm->avail_in) {
    while (shim->ibuf_off + strm->avail_in > shim->ibuf_sz) {
      debugp("realloc buffer %zu -> %zu\n", shim->ibuf_sz, shim->ibuf_sz * 2);
      shim->ibuf_sz *= 2;
      shim->ibuf = realloc(shim->ibuf, shim->ibuf_sz);
      if (shim->ibuf == NULL) {
        fprintf(stderr, "libzshim: realloc of z_shim input buffer failed\n");
        abort();
      }
    }
    memcpy(shim->ibuf + shim->ibuf_off, strm->next_in, strm->avail_in);
    shim->ibuf_off += strm->avail_in;

    // indicate data consumed
    strm->total_in += strm->avail_in;
    strm->avail_in = 0;
  }

  if (flush == Z_FINISH) {
    if (shim->obuf_off == 0) {
      debugp("going to zopfli %zu bytes\n", strm->total_in);

      ZopfliOptions zopt[] = {0};
      _ZopfliInitOptions(zopt);

      // TODO: make this configurable - enviornment varibles?
      if (shim->ibuf_off < 8192) {
        zopt->numiterations = 1000;
      } else if (shim->ibuf_off < 32768) {
        zopt->numiterations = 398;
      } else if (shim->ibuf_off < 131072) {
        zopt->numiterations = 158;
      } else if (shim->ibuf_off < 524288) {
        zopt->numiterations = 63;
      } else if (shim->ibuf_off < 2097152) {
        zopt->numiterations = 25;
      } else {
        zopt->numiterations = 10;
      }

      _ZopfliCompress(
        zopt, shim->format,
        shim->ibuf, shim->ibuf_off,
        &(shim->obuf), &(shim->obuf_sz)
      );
    }

    debugp(
      "shim->obuf: %p @ %zu/%zu (%.3f%% %zu)\n",
      (void*)shim->obuf, shim->obuf_off, shim->obuf_sz,
      (100.0*(double)shim->obuf_sz) / (double)shim->ibuf_off, shim->ibuf_off
    );

    size_t left = shim->obuf_sz - shim->obuf_off;
    if (strm->avail_out >= left) {
      memcpy(strm->next_out, shim->obuf + shim->obuf_off, left);
      shim->obuf_off += left;
      strm->avail_out -= left;
      strm->total_out += left;

      wrap_z_streamp(strm, shim);
      return Z_STREAM_END;
    } else {
      memcpy(strm->next_out, shim->obuf + shim->obuf_off, strm->avail_out);
      shim->obuf_off += strm->avail_out;
      strm->avail_out = 0;
      strm->total_out += strm->avail_out;
    }
  }

  wrap_z_streamp(strm, shim);
  return Z_OK;
}

int _deflateInit2_(z_streamp, int, int, int, int, int, const char *, int);

//+ int deflateInit_(z_streamp stream, int level, const char *version, int stream_size);

DLWRAP(deflateInit_, int,
  (z_streamp a, int b, const char *c, int d),
(a, b, c, d))

int wrap_deflateInit_(z_streamp stream, int level, const char *version, int stream_size) {
  debugp("deflateInit_(%p)\n", (void*)stream);
  return _deflateInit2_(
    stream, level, Z_DEFLATED, MAX_WBITS, MAX_MEM_LEVEL,
    Z_DEFAULT_STRATEGY, version, stream_size
  );
}

//+ int deflateInit2_(z_streamp stream, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size);
DLWRAP(deflateInit2_, int,
  (z_streamp a, int b, int c, int d, int e, int f, const char *g, int h),
(a, b, c, d, e, f, g, h))

int wrap_deflateInit2_(z_streamp stream, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size) {
  debugp("deflateInit2_(%p)\n", (void*)stream);
  return _deflateInit2_(stream, level, method, windowBits, memLevel, strategy, version, stream_size);
}

int _deflateInit2_(z_streamp stream, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size) {
  int ret = _real_deflateInit2_(stream, level, method, windowBits, memLevel, strategy, version, stream_size);

  z_shimp shim = init_z_shim(stream, NULL);
  shim->format = get_bits_format(windowBits);

  return ret;
}

//+ int deflateEnd(z_streamp stream);
DLWRAP(deflateEnd, int,
  (z_streamp a),
(a))

int wrap_deflateEnd(z_streamp stream) {
  debugp("deflateEnd(%p)\n", (void*)stream);

  z_shimp shim = unwrap_z_streamp(stream);
  free_z_shim(shim);

  int ret = _real_deflateEnd(stream);
  return ret;
}

//+ int deflateReset(z_streamp strm);
DLWRAP(deflateReset, int,
  (z_streamp a),
(a))

int wrap_deflateReset(z_streamp stream) {
  debugp("deflateReset(%p)\n", (void*)stream);

  z_shimp shim = unwrap_z_streamp(stream);

  int ret = _real_deflateReset(stream);
  shim = init_z_shim(stream, shim);
  return ret;
}

// int compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
// int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);

// int deflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength);
// int deflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength);
// int deflateCopy(z_streamp dest, z_streamp source);
// int deflateParams(z_streamp strm, int level, int strategy);
// int deflateTune(z_streamp strm, int good_length, int max_lazy, int nice_length, int max_chain);
// uLong deflateBound(z_streamp strm, uLong sourceLen);
// int deflatePending(z_streamp strm, unsigned *pending, int *bits);
// int deflatePrime(z_streamp strm, int bits, int value);
// int deflateSetHeader(z_streamp strm, gz_headerp head);
