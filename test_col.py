import struct
import os

with open("project/gta/models/gta3.img", "rb") as f:
    magic = f.read(4)
    if magic == b'VER2':
        count = struct.unpack('<I', f.read(4))[0]
        for i in range(count):
            f.seek(8 + i * 32)
            offset = struct.unpack('<I', f.read(4))[0] * 2048
            f.seek(8 + i * 32 + 8)
            name = f.read(24).split(b'\0')[0].decode('ascii')
            if name.lower() == "levelmap_1.col":
                print(f"Found {name} at {offset}")
                f.seek(offset)
                fourcc = f.read(4)
                print("Magic:", fourcc)
                size = struct.unpack('<I', f.read(4))[0]
                model_name = f.read(22).split(b'\0')[0].decode('ascii')
                print("Model Name:", model_name)
