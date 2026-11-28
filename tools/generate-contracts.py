"""Generates src/core/contracts.hpp: the managed declaration behind every guarded
method address the sources use (`"name", argc, 0xRVA`) and behind every field
offset they name (`FieldAt("type", "field", 0xNN)`, `UnboxedFieldAt`).

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
FIELD = re.compile(r'\b(FieldAt|UnboxedFieldAt|GenericFieldAt)\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\)')


def declared(assembly, full, types):
    """(namespace, "Outer+Inner"): the namespace ends where the longest declared enclosing type begins."""
    chain = [full]
    while True:
        head = chain[0]
        cut = max(head.rfind('.'), head.rfind('+'), head.rfind('/'))
        if cut < 0 or (assembly, head[:cut]) not in types: break
        chain[0:1] = [head[:cut], head[cut + 1:]]
    space, _, top = chain[0].rpartition('.')
    return space, '+'.join([top] + chain[1:])


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
    wanted, named = {}, {}
    for path in sorted((ROOT / 'src').rglob('*')):
        if path.suffix not in ('.cpp', '.hpp') or path.name in ('signatures.hpp', 'contracts.hpp', 'runtime.hpp'): continue
        text = path.read_text(encoding='utf-8')
        for m in LITERAL.finditer(text):
            address = int(m[3], 16)
            if address >= 0x500000: wanted.setdefault((address, m[1], int(m[2])), path.name)
        for m in FIELD.finditer(text):
            named.setdefault((m[1], m[2], m[3], int(m[4], 0)), path.name)
    addresses = {k[0] for k in wanted}
    byAddress = collections.defaultdict(list)
    types = set()
    fieldNames = {(k[1].split(':', 1)[-1], k[2]) for k in named}
    byField = collections.defaultdict(list)
    for row in json.loads(args.contracts.read_text(encoding='utf-8')):
        if row['kind'] == 'type': types.add((row['assembly'], row['type']))
        elif row['kind'] == 'method':
            address = int(row['address'].get('RVA', '0'), 16)
            if address in addresses: byAddress[address].append(row)
        elif (row['type'], row['name']) in fieldNames: byField[(row['type'], row['name'])].append(row)
    lines, counts = [], collections.Counter()
    for (address, name, argc), source in sorted(wanted.items()):
        owners = [r for r in byAddress[address] if r['name'] == name and arity(r['signature']) == argc]
        if not owners: counts['undeclared'] += 1; print('undeclared', hex(address), name, argc, source); continue
        if len(owners) > FOLDED: counts['folded'] += 1; continue
        for row in sorted(owners, key=lambda r: (r['assembly'], r['type'])):
            full = row['type']
            space, chain = declared(row['assembly'], full, types)
            fields = [literal(row['assembly']), literal(space), literal(chain), literal(name),
                      literal(normalized(row['signature']))]
            if None in fields: counts['not ascii'] += 1; continue
            generic = '`' in full
            counts['generic' if generic else 'entries'] += 1
            lines.append('    {0x%X, %s, %s, %s, %s, %d, %s, %s},' % (address, fields[0], fields[1], fields[2], fields[3], argc,
                                                                   fields[4], 'true' if generic else 'false'))
    fieldLines, failures = set(), []
    for (form, owner, field, offset), source in sorted(named.items()):
        assembly, _, type_ = owner.rpartition(':')
        rows = [r for r in byField[(type_, field)] if not assembly or r['assembly'] == assembly]
        where = '%s: %s("%s", "%s", 0x%X)' % (source, form, owner, field, offset)
        if len(rows) != 1: failures.append(('no such field ' if not rows else 'type declared by several assemblies ') + where); continue
        row = rows[0]
        if row.get('flags', 0) & 0x10: failures.append('static field ' + where); continue
        if form == 'GenericFieldAt':
            if '`' not in type_: failures.append('not a generic type ' + where)
            counts['generic fields'] += 1; continue
        if '`' in type_: failures.append('generic type, use GenericFieldAt ' + where); continue
        stated = int(row['address']['Offset'], 16)
        if stated != offset + (0x10 if form == 'UnboxedFieldAt' else 0): failures.append('declared at 0x%X ' % stated + where); continue
        space, chain = declared(row['assembly'], type_, types)
        parts = [literal(row['assembly']), literal(space), literal(chain), literal(field)]
        if None in parts: counts['not ascii'] += 1; continue
        fieldLines.add('    {%s, %s, %s, %s, 0x%X},' % (*parts, stated))
    if failures: sys.exit('\n'.join(failures))
    counts['fields'] = len(fieldLines)
    text = ('#pragma once\n#include <cstddef>\n#include <cstdint>\n'
            '// Generated by tools/generate-contracts.py from the declarations of the pinned\n'
            '// client: do not edit. One line per managed declaration of a guarded method the\n'
            '// sources name; `signature` is Return(Parameters) in run-time class names.\n'
            'struct MethodContract {\n    uintptr_t rva; const char* image; const char* space; const char* type; const char* name;\n'
            '    int argc; const char* signature; bool generic;\n};\n'
            'inline constexpr MethodContract MethodContracts[] = {\n' + '\n'.join(lines) + '\n};\n'
            '// One line per instance field the sources read by offset: the offset from the\n'
            '// object, as il2cpp_field_get_offset gives it (an unboxed value starts 0x10 later).\n'
            'struct FieldContract { const char* image; const char* space; const char* type; const char* name; size_t offset; };\n'
            'inline constexpr FieldContract FieldContracts[] = {\n' + '\n'.join(sorted(fieldLines)) +
            '\n    {nullptr, nullptr, nullptr, nullptr, 0}, // end mark: the array is never empty\n};\n')
    print(json.dumps(counts))
    if args.check:
        if OUTPUT.read_text(encoding='utf-8') != text: sys.exit('contracts.hpp is not the generated file')
        print('contracts.hpp is up to date')
    else:
        OUTPUT.write_text(text, encoding='utf-8', newline='\n')


main()
