import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else r"E:\llama\NewChangeDirectory\test\test_ui_exclusions.exe"
rva = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x1EAA0

with open(path, "rb") as f:
    # DOS header
    f.seek(0x3C)
    pe_offset = struct.unpack("<I", f.read(4))[0]
    # COFF header
    f.seek(pe_offset + 4)
    num_sections = struct.unpack("<H", f.read(2))[0]
    f.seek(pe_offset + 6)  # skip timestamp
    # optional header
    optional_header_offset = pe_offset + 24
    f.seek(optional_header_offset)
    magic = struct.unpack("<H", f.read(2))[0]
    image_base = struct.unpack("<Q", f.read(8))[0] if magic == 0x20b else struct.unpack("<I", f.read(4))[0]
    # section headers start after optional header
    # size of optional header is at pe_offset + 20
    f.seek(pe_offset + 20)
    optional_header_size = struct.unpack("<H", f.read(2))[0]
    section_table_offset = pe_offset + 24 + optional_header_size
    for i in range(num_sections):
        f.seek(section_table_offset + i * 40)
        name = f.read(8).rstrip(b'\x00').decode('ascii', errors='ignore')
        virtual_size = struct.unpack("<I", f.read(4))[0]
        virtual_address = struct.unpack("<I", f.read(4))[0]
        raw_size = struct.unpack("<I", f.read(4))[0]
        raw_offset = struct.unpack("<I", f.read(4))[0]
        if virtual_address <= rva < virtual_address + virtual_size:
            file_offset = raw_offset + (rva - virtual_address)
            f.seek(file_offset)
            data = f.read(256)
            # find null-terminated string
            null_pos = data.find(b'\x00')
            s = data[:null_pos].decode('utf-8', errors='replace')
            print(f"Section {name}, file_offset=0x{file_offset:X}, string={repr(s)}")
            break
    else:
        print("RVA not found in any section")
