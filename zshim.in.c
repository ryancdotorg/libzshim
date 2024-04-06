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

z_shimp init_z_shim() {
  z_shimp shim;

  if (NULL == (shim = malloc(sizeof(z_shim)))) abort();

  shim->magic = SHIM_MAGIC;

  if (NULL == (shim->ibuf = malloc(1<<28))) abort(); // 256MiB
  shim->ibuf_sz = 1<<28;
  shim->ibuf_off = 0;

  shim->obuf = NULL;
  shim->obuf_sz = 0;
  shim->obuf_off = 0;

  shim->state = 0;

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
  }
}

void wrap_z_streamp(z_streamp strm, z_shimp shim) {
  if (shim != NULL) {
    if (strm->opaque != NULL) {
      uint64_t magic = ((z_shimp)strm->opaque)->magic;
      if (magic == SHIM_MAGIC) {
        free_z_shim(strm->opaque);
        strm->opaque = NULL;
      } else {
        fprintf(stderr, "strm->opaque: %p\n", (void*)strm->opaque);
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
  "Z_NO_FLUSH",
  "Z_PARTIAL_FLUSH",
  "Z_SYNC_FLUSH",
  "Z_FULL_FLUSH",
  "Z_FINISH",
  "Z_BLOCK",
  "Z_TREES"
};

//+ int deflate(z_streamp strm, int flush);
DLWRAP(deflate, int,
  (z_streamp a, int b),
  (a, b)
)

int wrap_deflate(z_streamp strm, int flush) {
  z_shimp shim = unwrap_z_streamp(strm);

  if (flush != Z_NO_FLUSH && flush != Z_FINISH && flush >= 0 && flush <= 6) {
    fprintf(stderr, "deflate(%p, %s)\n", (void *)strm, flush_str[flush]);
  } else if (flush < 0 || flush > 6) {
    fprintf(stderr, "deflate(%p, %d)\n", (void *)strm, flush);
  }

  // are we being fed data?
  if (strm->avail_in) {
    while (shim->ibuf_off + strm->avail_in > shim->ibuf_sz) {
      shim->ibuf_sz *= 2;
      shim->ibuf = realloc(shim->ibuf, shim->ibuf_sz);
      if (shim->ibuf == NULL) {
        fprintf(stderr, "out of memory for zopfli\n");
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
      if (shim->obuf != NULL) {
        fprintf(stderr, "shim->obuf: %p\n", (void*)shim->obuf);
      }
      fprintf(stderr, "> going to zopfli %zu bytes\n", strm->total_in);
      ZopfliOptions zopt[] = {0};
      _ZopfliInitOptions(zopt);
      // TODO: make this configurable - enviornment varibles?
      if (shim->ibuf_off < 8192) {
        zopt->numiterations = 1000;
      } else if (shim->ibuf_off < 65536) {
        zopt->numiterations = 100;
      } else {
        zopt->numiterations = 15;
      }
      _ZopfliCompress(
        zopt, shim->format,
        shim->ibuf, shim->ibuf_off,
        &(shim->obuf), &(shim->obuf_sz)
      );
    }

    fprintf(stderr,
      "shim->obuf: %p @ %zu/%zu\n",
      (void*)shim->obuf, shim->obuf_off, shim->obuf_sz
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

      wrap_z_streamp(strm, shim);
      return Z_OK;
    }
  }

  wrap_z_streamp(strm, shim);
  return Z_OK;
}

//+ int deflateInit2_(z_streamp stream, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size);
DLWRAP(deflateInit2_, int,
  (z_streamp a, int b, int c, int d, int e, int f, const char * g, int h),
(a, b, c, d, e, f, g, h))

int wrap_deflateInit2_(z_streamp stream, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size) {
  //fprintf(stderr, "deflateInit2_(%p)\n", (void*)stream);

  int ret = _real_deflateInit2_(stream, level, method, windowBits, memLevel, strategy, version, stream_size);

  z_shimp shim = init_z_shim();
  wrap_z_streamp(stream, shim);
  shim->format = get_bits_format(windowBits);

  return ret;
}

//+ int deflateEnd(z_streamp stream);
DLWRAP(deflateEnd, int,
  (z_streamp a),
(a))

int wrap_deflateEnd(z_streamp stream) {
  //fprintf(stderr, "deflateEnd(%p)\n", (void*)stream);

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
  //fprintf(stderr, "deflateReset(%p)\n", (void*)stream);

  z_shimp shim = unwrap_z_streamp(stream);
  free_z_shim(shim);

  int ret = _real_deflateReset(stream);
  shim = init_z_shim();
  wrap_z_streamp(stream, shim);
  return ret;
}

// int compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
// int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);

// int deflateInit_(z_streamp stream, int level, const char *version, int stream_size);
// int deflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength);
// int deflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength);
// int deflateCopy(z_streamp dest, z_streamp source);
// int deflateParams(z_streamp strm, int level, int strategy);
// int deflateTune(z_streamp strm, int good_length, int max_lazy, int nice_length, int max_chain);
// uLong deflateBound(z_streamp strm, uLong sourceLen);
// int deflatePending(z_streamp strm, unsigned *pending, int *bits);
// int deflatePrime(z_streamp strm, int bits, int value);
// int deflateSetHeader(z_streamp strm, gz_headerp head);
