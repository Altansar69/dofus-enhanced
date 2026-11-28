"""Generates src/core/contracts.hpp: the managed declaration behind every guarded
method address the sources use (`"name", argc, 0xRVA`).

The module resolves these declarations by name at run time (core/compat.cpp) and
compares the result with the pinned address. Files only: reads the sources and a
declarations file made offline from a saved copy of the game (the Contracts tool's
JSON). Never reads a game process.

    python tools/generate-contracts.py --contracts <contracts-new.json>
    python tools/generate-contracts.py --contracts <contracts-new.json> --check
"""
import argparse, collections, json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'src/core/contracts.hpp'
LITERAL = re.compile(r'"([^"\n]+)"\s*,\s*(\d+)\s*,\s*(0x[0-9A-Fa-f]+)')
# One address shared by more declarations than this is code folded by the linker
# across unrelated types (trivial constructors, getters): nothing to resolve by name.
FOLDED = 8


def split(text):
    """Top-level comma split of a parameter list."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in '<([': depth += 1
        elif c in '>)]': depth -= 1
        elif c == ',' and depth == 0: parts.append(text[start:i]); start = i + 1
    if text[start:]: parts.append(text[start:])
    return parts


def simple(name):
    """The run-time class name of a declared type: no namespace, declaring type,
    generic arguments or by-reference mark (il2cpp_class_get_name of the class
    il2cpp_class_from_type gives)."""
    name = name.strip()
    suffix = ''
    while name.endswith(('&', '*')): name = name[:-1]
    while name.endswith(']'):
        cut = name.rindex('['); suffix = name[cut:] + suffix; name = name[:cut]
    depth, out = 0, []
    for c in name:
        if c == '<': depth += 1
        elif c == '>': depth -= 1
        elif depth == 0: out.append(c)
    return re.split(r'[.+/]', ''.join(out))[-1] + suffix


def normalized(signature):
    _, _, returned, params = signature.split('|', 3)
    return simple(returned) + '(' + ','.join(simple(p) for p in split(params)) + ')'


def arity(signature):
    return len(split(signature.split('|', 3)[3]))


def literal(text):
    if any(ord(c) < 32 or ord(c) > 126 for c in text): return None
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--contracts', required=True, type=Path)
    parser.add_argument('--check', action='store_true', help='fail if the committed header is not the generated one')
    args = parser.parse_args()
    wanted = {}
    for path in sorted((ROOT / 'src').rglob('*')):
        if path.suffix not in ('.cpp', '.hpp') or path.name in ('signatures.hpp', 'contracts.hpp'): continue
        for m in LITERAL.finditer(path.read_text(encoding='utf-8')):
            address = int(m[3], 16)
            if address >= 0x500000: wanted.setdefault((address, m[1], int(m[2])), path.name)
    addresses = {k[0] for k in wanted}
    byAddress = collections.defaultdict(list)
    types = set()
    for row in json.loads(args.contracts.read_text(encoding='utf-8')):
        if row['kind'] == 'type': types.add((row['assembly'], row['type']))
        elif row['kind'] == 'method':
            address = int(row['address'].get('RVA', '0'), 16)
            if address in addresses: byAddress[address].append(row)
    lines, counts = [], collections.Counter()
    for (address, name, argc), source in sorted(wanted.items()):
        owners = [r for r in byAddress[address] if r['name'] == name and arity(r['signature']) == argc]
        if not owners: counts['undeclared'] += 1; print('undeclared', hex(address), name, argc, source); continue
        if len(owners) > FOLDED: counts['folded'] += 1; continue
        for row in sorted(owners, key=lambda r: (r['assembly'], r['type'])):
            full = row['type']
            # The namespace ends where the longest declared enclosing type begins.
            chain = [full]
            while True:
                head = chain[0]
                cut = max(head.rfind('.'), head.rfind('+'), head.rfind('/'))
                if cut < 0 or (row['assembly'], head[:cut]) not in types: break
                chain[0:1] = [head[:cut], head[cut + 1:]]
            outer = chain[0]
            space, _, top = outer.rpartition('.')
            fields = [literal(row['assembly']), literal(space), literal('+'.join([top] + chain[1:])), literal(name),
                      literal(normalized(row['signature']))]
            if None in fields: counts['not ascii'] += 1; continue
            generic = '`' in full
            counts['generic' if generic else 'entries'] += 1
            lines.append('    {0x%X, %s, %s, %s, %s, %d, %s, %s},' % (address, fields[0], fields[1], fields[2], fields[3], argc,
                                                                   fields[4], 'true' if generic else 'false'))
    text = ('#pragma once\n#include <cstdint>\n'
            '// Generated by tools/generate-contracts.py from the declarations of the pinned\n'
            '// client: do not edit. One line per managed declaration of a guarded method the\n'
            '// sources name; `signature` is Return(Parameters) in run-time class names.\n'
            'struct MethodContract {\n    uintptr_t rva; const char* image; const char* space; const char* type; const char* name;\n'
            '    int argc; const char* signature; bool generic;\n};\n'
            'inline constexpr MethodContract MethodContracts[] = {\n' + '\n'.join(lines) + '\n};\n')
    print(json.dumps(counts))
    if args.check:
        if OUTPUT.read_text(encoding='utf-8') != text: sys.exit('contracts.hpp is not the generated file')
        print('contracts.hpp is up to date')
    else:
        OUTPUT.write_text(text, encoding='utf-8', newline='\n')


main()
