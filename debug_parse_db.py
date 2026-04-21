#!/usr/bin/env python3
import struct
import sys

with open(sys.argv[1], 'rb') as f:
    data = f.read()

# BinFileHdr: 32 bytes
# magic(4) + version(2) + show_hidden(1) + show_system(1) + last_scan(8) + drive_count(4) + skipped_rescan(1) + encoding(1) + pad(2) + checksum(8)
magic, version, show_hidden, show_system, last_scan, drive_count, skipped_rescan, encoding, pad, checksum = struct.unpack_from('<IHBBqIBB2sQ', data, 0)
print(f"Magic: {hex(magic)} (expected {hex(0x4244434E)})")
print(f"Version: {version}")
print(f"Drive count: {drive_count}")
print(f"File size: {len(data)} bytes")

offset = 32  # After BinFileHdr
for d in range(drive_count):
    if offset + 80 > len(data):
        print(f"Truncated at drive {d}, offset {offset}")
        break
    # BinDriveHdr: 80 bytes
    # letter(1) + pad(3) + type(4) + label(64) + dir_count(4) + pool_size(4)
    letter, pad3, type_, label, dir_count, pool_size = struct.unpack_from('<B3sI64sII', data, offset)
    label_str = label.split(b'\x00')[0].decode('utf-8', errors='replace')
    print(f"Drive {d}: letter={chr(letter) if letter else '0'}, type={type_}, label='{label_str}', dir_count={dir_count}, pool_size={pool_size}")
    offset += 80
    
    if offset + dir_count * 12 > len(data):
        print(f"Truncated at dirs, offset {offset}")
        break
    
    # DirEntry array: 12 bytes each
    dirs = []
    for i in range(dir_count):
        parent, name_off, is_hidden, is_system, pad2 = struct.unpack_from('<iIBB2s', data, offset)
        dirs.append((parent, name_off))
        offset += 12
    
    if offset + pool_size > len(data):
        print(f"Truncated at pool, offset {offset}")
        break
    
    # Name pool
    name_pool = data[offset:offset+pool_size]
    offset += pool_size
    
    # Reconstruct paths
    def reconstruct(idx, parent_path):
        if idx < 0 or idx >= len(dirs):
            return
        parent, name_off = dirs[idx]
        name_end = name_pool.find(b'\x00', name_off)
        if name_end < 0:
            name_end = len(name_pool)
        name = name_pool[name_off:name_end].decode('utf-8', errors='replace')
        if parent == -1:
            path = label_str.rstrip('/') + '/' + name
        else:
            path = parent_path + '/' + name
        print(f"  dir[{idx}]: {path}")
        # Find children
        for j, (p, no) in enumerate(dirs):
            if p == idx:
                reconstruct(j, path)
    
    for i, (parent, name_off) in enumerate(dirs):
        if parent == -1:
            reconstruct(i, label_str.rstrip('/'))
