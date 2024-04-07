#pragma once
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

#ifndef NDEBUG
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#define debugp(...) _debugp(__FILE__, __func__, __LINE__, __VA_ARGS__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void _debugp(const char *file, const char *func, unsigned int line, const char *fmt, ...) {
  int saved_errno = errno;
  // ANSI SGR escape code parameters, e.g. `31` for red
  // https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_.28Select_Graphic_Rendition.29_parameters
  char *color = getenv("DEBUGP_COLOR");
  int fd = STDERR_FILENO;

#if __STDC_VERSION__ >= 199900L
  size_t n = strlen(fmt);
  char modified_fmt[n + 1];
#else
  char modified_fmt[65536];
  size_t n = strnlen(fmt, sizeof(modified_fmt) - 1);
#endif
  // copy format *without* null terminator
  memcpy(modified_fmt, fmt, n);

  va_list args;
  va_start(args, fmt);
  // add null terminator, stripping newline if present
  modified_fmt[n-(modified_fmt[n-1] == '\n' ? 1 : 0)] = '\0';
  // set color if supplied via environment
  if (color != NULL) dprintf(fd, "\033[%sm", color);
  // line header
  dprintf(fd, "%s(%s:%u,%d): ", file, func, line, errno);
  // actual content
  vdprintf(fd, modified_fmt, args);
  // reset color if required, and print and ending newline
  dprintf(fd, color != NULL ? "\033[0m\n" : "\n");
  fdatasync(fd);
  errno = saved_errno;
}
#pragma GCC diagnostic pop
#else
// no-op
#define debugp(...) do {} while (0)
#endif
