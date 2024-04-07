#!/usr/bin/env python3

from sys import argv

import re

def chrs(n):
    return map(chr, range(ord('a'), ord('a')+n))

def args(fn_arg_types):
    z = zip(fn_arg_types, chrs(len(fn_arg_types)))
    return map(lambda x: ' '.join(x), z)

with open(argv[1]) as f:
    for line in map(str.strip, f):
        print(line)

        # "// int fn(int foo, float bar);" -> ('int', 'fn', 'int foo, float bar')
        m = re.fullmatch(
            r'//\s+(\w+)\s+(\w+)[(]([^)]*)[)];',
            line
        )

        if m:
            fn_arg_types = []
            fn_arg_names = []
            fn_type, fn_name, fn_args = m.groups()
            # "int foo, float bar" -> ('int foo', 'float bar')
            for part in re.split(r',\s*', fn_args):
                # "int foo" -> ('int', 'foo')
                m2 = re.fullmatch(r'(.+\W+)(\w+)', part)
                fn_arg_type, fn_arg_name = m2.groups()
                fn_arg_types.append(fn_arg_type.strip())
                fn_arg_names.append(fn_arg_name.strip())

            print(f'DLWRAP({fn_name}, {fn_type},')
            print(f'  ({", ".join(args(fn_arg_types))}),')
            print(f'({", ".join(chrs(len(fn_arg_types)))}))')
            print('')

            print(f'{fn_type} wrap_{fn_name}({fn_args}) {{')

            if len(fn_arg_types) and fn_arg_types[0] == 'z_streamp':
                print(f'  fprintf(stderr, "{fn_name}(%p)\\n", (void*){fn_arg_names[0]});')
            else:
                print(f'  fprintf(stderr, "{fn_name}\\n");')

            if fn_name.startswith('deflate') and len(fn_arg_types) and fn_arg_names[0] == 'strm':
                print(f'  z_shimp shim = unwrap_z_streamp(strm);')
                print(f'  {fn_type} ret = _real_{fn_name}({", ".join(fn_arg_names)});')
                print(f'  wrap_z_streamp(strm, shim);')
            else:
                print(f'  {fn_type} ret = _real_{fn_name}({", ".join(fn_arg_names)});')
            print('  return ret;')
            print('}')
            print('')
