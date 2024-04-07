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

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define COMMA() ,

#define STR_LIST(...) {__VA_ARGS__, NULL}

#define CAT(a, ...) a ## __VA_ARGS__

#define FIRST(a, ...) a
#define SECOND(a, b, ...) b
#define THIRD(a, b, c, ...) c
#define REST(a, ...) __VA_ARGS__

#define IS_PROBE(...) SECOND(__VA_ARGS__, 0)
#define PROBE() ~, 1,

#define IS_PAREN(x) IS_PROBE(_IS_PAREN_PROBE x)
#define _IS_PAREN_PROBE(...) PROBE()

#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()

#define BITAND(x) CAT(_BITAND_, x)
#define _BITAND_0(x) 0
#define _BITAND_1(x) x

#define COMPL(x) CAT(_COMPL_, x)
#define _COMPL_0 1
#define _COMPL_1 0

#define BOOL(x) COMPL(NOT(x))

// need to define e.g. COMPARE_foo(x) x
#define COMPARABLE(x) IS_PAREN(CAT(_COMPARE_,x)(()))
#define _COMPARE(x, y) COMPL(IS_PAREN(_COMPARE_##x(_COMPARE_##y)(())))
#define EQUAL(x, y) BITAND(BITAND(COMPARABLE(x))(COMPARABLE(y)))(_COMPARE(x,y))
#define NOT_EQUAL(x, y) COMPL(EQUAL(x, y))

#define IF_EQUAL(x, y) IF(EQUAL(x, y))
#define IF_NOTEQ(x, y) IF(NOT_EQUAL(x, y))

#define IF(c) _IF(BOOL(c))
#define _IF(c) CAT(_IF_,c)
#define _IF_0(...)
#define _IF_1(...) __VA_ARGS__

#define _COMPARE_void(x) x

#define DLFUNC(NAME, TYPE, LIBS, ARGS, ARGN) \
static TYPE _load_##NAME ARGS; \
static TYPE (*_##NAME)ARGS = _load_##NAME; \
static int _has_##NAME () { \
  int i = 0; \
  char *filename; \
  void *handle = NULL; \
  char *libs[] = LIBS; \
  while (handle == NULL && (filename = libs[i++]) != NULL) { \
    handle = dlopen(filename, RTLD_LAZY); \
  } \
  _##NAME = (typeof(_##NAME))(intptr_t)(dlsym(handle != NULL ? handle : RTLD_NEXT, #NAME)); \
  return (_##NAME != NULL); \
} \
static TYPE _load_##NAME ARGS { \
  if (!_has_##NAME ()) { \
    fprintf(stderr, "libzshim: " #NAME " function not found, aborting\n"); \
    abort(); \
  } \
  IF_NOTEQ(TYPE, void)(return) _##NAME ARGN; \
}

// * NAME calls _impl_NAME
// * _impl_NAME initially points to _wrap_NAME
// * _wrap_NAME tries to save wrap_NAME to _impl_NAME, falling back to NAME,
//   then calls _impl_NAME
// * wrap_NAME is our wrapper function
// * _real_NAME initially points to _load_NAME
// * _load_NAME saves the real function as _real_NAME then calls _real_NAME
#define DLWRAP(NAME, TYPE, ARGS, ARGN) \
TYPE wrap_##NAME ARGS; \
static TYPE _load_##NAME ARGS; \
static TYPE _wrap_##NAME ARGS; \
static TYPE (*_real_##NAME)ARGS = _load_##NAME; \
static TYPE (*_impl_##NAME)ARGS = _wrap_##NAME; \
static TYPE _wrap_##NAME ARGS { \
  _impl_##NAME = (typeof(_impl_##NAME))(intptr_t)(dlsym(RTLD_DEFAULT, "wrap_" #NAME)); \
  if (_impl_##NAME == NULL) _impl_##NAME = (typeof(_impl_##NAME))(intptr_t)(dlsym(RTLD_NEXT, #NAME)); \
  IF_NOTEQ(TYPE, void)(return) _impl_##NAME ARGN; \
} \
static TYPE _load_##NAME ARGS { \
  _real_##NAME = (typeof(_real_##NAME))(intptr_t)(dlsym(RTLD_NEXT, #NAME)); \
  if (_real_##NAME == NULL) { \
    fprintf(stderr, "libzshim: " #NAME " function not found, aborting\n"); \
    abort(); \
  } \
  IF_NOTEQ(TYPE, void)(return) _real_##NAME ARGN; \
} \
TYPE NAME ARGS { IF_NOTEQ(TYPE, void)(return) _impl_##NAME ARGN; }
