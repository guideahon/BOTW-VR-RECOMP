#!/usr/bin/env python3
"""A copy of a save with another bow equipped, for tests that need one (a Lynel bow's three arrows: test_vr_bow.py
--others 2). In every slot of <src> (game_data.sav), the bow given is marked equipped and the others not; a slot
without that bow gets its equipped bow renamed to it. The source is never written.

  python tools/vr/save_equip.py <src save dir> <dst save dir> [Weapon_Bow_032]
Weapon_Bow_032 is the Savage Lynel Bow: LeadShot, three arrows 3 deg apart (its bgparamlist).
"""
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from botw_save_convert import HEADER, crc32_name, read_entries  # noqa: E402

ITEM, EQUIP = crc32_name('PorchItem'), crc32_name('PorchItem_EquipFlag')


def words_of(data, e, h):
    return [o for o in range(HEADER, len(data) - 4, 8) if struct.unpack_from(e + 'I', data, o)[0] == h]


def equip(path, bow):
    data = bytearray(open(path, 'rb').read())
    _, e, _ = read_entries(bytes(data))
    items, flags = words_of(data, e, ITEM), words_of(data, e, EQUIP)
    names = [b''.join(bytes(data[o + 4:o + 8]) for o in items[k:k + 16]).split(b'\0')[0].decode('ascii', 'replace')
             for k in range(0, len(items), 16)]
    if bow not in names:   # the equipped bow becomes this one
        held = [i for i, n in enumerate(names) if n.startswith('Weapon_Bow_') and struct.unpack_from(e + 'I', data, flags[i] + 4)[0]]
        if not held:
            raise SystemExit('%s: no %s and no bow equipped' % (path, bow))
        new = bow.encode().ljust(64, b'\0')
        for j, o in enumerate(items[held[0] * 16:held[0] * 16 + 16]):
            data[o + 4:o + 8] = new[j * 4:j * 4 + 4]
        names[held[0]] = bow
    at = names.index(bow)
    for i, n in enumerate(names):
        if n.startswith('Weapon_Bow_'):
            struct.pack_into(e + 'I', data, flags[i] + 4, 1 if i == at else 0)
    open(path, 'wb').write(data)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    bow = sys.argv[3] if len(sys.argv) > 3 else 'Weapon_Bow_032'
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    n = 0
    for root, _, files in os.walk(dst):
        if 'game_data.sav' in files:
            equip(os.path.join(root, 'game_data.sav'), bow)
            n += 1
    print('%s equipped in %d slots of %s' % (bow, n, dst))
    return 0 if n else 1


if __name__ == '__main__':
    sys.exit(main())
