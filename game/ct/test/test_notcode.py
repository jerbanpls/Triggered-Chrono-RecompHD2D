#!/usr/bin/env python3
"""notcode.toml: labels that are not function entries.

1. Schema (funcs.load_notcode): kinds, evidence, no duplicate addresses.
2. No address is both not code and a registered function or unresolved.
3. Every `interior` entry is an instruction start of a compiled routine
   (some funcs.toml entry decodes an instruction there, named by `in`);
   every `data`/`sentinel`/`padding`/`text` entry is not an instruction
   start of any compiled routine.
4. Accounting: sync_symbols.validate() leaves a not-code address out of both
   the validated and the unresolved lists, and lists it as unresolved when it
   is not classified (so the known-function total, which counts unresolved,
   does not include not-code labels).
"""
import os
import sys
import tomllib

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # game/ct
REPO = os.path.dirname(os.path.dirname(GAME))
sys.path.insert(0, os.path.join(REPO, 'recomp'))
sys.path.insert(0, os.path.join(GAME, 'tools'))

import decode  # noqa: E402
import funcs  # noqa: E402
import sync_symbols  # noqa: E402

FUNCS = os.path.join(GAME, 'funcs.toml')
NOTCODE = os.path.join(GAME, 'notcode.toml')
UNRESOLVED = os.path.join(GAME, 'unresolved.toml')


def main() -> int:
    errors = []
    nc = funcs.load_notcode(NOTCODE)
    metas = funcs.load(FUNCS)
    rom = decode.load_rom()
    reg = funcs.Registry(rom, metas, FUNCS)
    with open(UNRESOLVED, 'rb') as f:
        unresolved = {u['addr'] for u in tomllib.load(f).get('unresolved', [])}
    registered = {fm.addr for fm in metas}

    starts: dict[int, set[str]] = {}
    for fm in metas:
        for st in fm.entry_states():
            for i in reg.function(fm.addr, st).insns:
                starts.setdefault(i.addr, set()).add(fm.name)

    for t in nc:
        a, name = t['addr'], t['name']
        if a in registered:
            errors.append(f'{name} ${a:06X}: also a registered function')
        if a in unresolved:
            errors.append(f'{name} ${a:06X}: also unresolved')
        if t['kind'] == 'interior':
            hosts = starts.get(a, set()) - {name}
            if not hosts:
                errors.append(f'{name} ${a:06X}: interior but no compiled routine has an '
                              'instruction there')
            elif t.get('in') not in hosts:
                errors.append(f'{name} ${a:06X}: `in` = {t.get("in")!r}, compiled by '
                              f'{sorted(hosts)[:3]}')
        elif a in starts:
            errors.append(f'{name} ${a:06X}: kind {t["kind"]} but compiled code starts an '
                          f'instruction there ({sorted(starts[a])[:3]})')

    # accounting: a not-code candidate is never reported as unresolved
    cand = sync_symbols.Candidate(0xC0FD00, 'ProbeLabel', 'dscotton')
    plain = sync_symbols.validate(rom, metas, {cand.addr: cand}, 'bankc0', FUNCS)
    held = sync_symbols.validate(rom, metas, {cand.addr: cand}, 'bankc0', FUNCS,
                                 frozenset({cand.addr}))
    if [c.addr for c, _ in plain.unresolved] != [cand.addr]:
        errors.append('validate: an unclassified stateless candidate should be unresolved')
    if held.unresolved or held.validated or held.conflicts:
        errors.append('validate: a not-code candidate must be neither validated nor unresolved')

    for e in errors:
        print('FAIL:', e)
    if not errors:
        kinds = {}
        for t in nc:
            kinds[t['kind']] = kinds.get(t['kind'], 0) + 1
        print('ok: notcode.toml', dict(sorted(kinds.items())))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
