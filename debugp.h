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
#define debugp(...) _debugp(__FILE__, __func__, __LINE__, __VA_ARGS__)
static void _debugp(const char *file, const char *func, unsigned int line, const char *fmt, ...) {
  char f[256];
  va_list args;
  va_start(args, fmt);
  size_t n = strnlen(fmt, sizeof(f) - 1);
  strncpy(f, fmt, n);
  // remove newline from format string
  f[n-(f[n-1] == '\n' ? 1 : 0)] = 0;
  fprintf(stderr, "%s(%s:%u,%d): ", file, func, line, errno);
  vfprintf(stderr, f, args);
  fprintf(stderr, "\n");
}
#else
#define debugp(...) do {} while (0)
#endif
