import struct
with open("project/gta/models/gta3.img", "rb") as f:
    f.seek(24592384) # Offset of levelmap_1.col
    fourcc = f.read(4)
    size = struct.unpack('<I', f.read(4))[0]
    name = f.read(22).split(b'\0')[0].decode('ascii')
    model_id = struct.unpack('<H', f.read(2))[0]
    print(f"Header: {fourcc}, size: {size}, name: {name}, id: {model_id}")
    
    # Let's just dump the next 100 bytes as hex
    data = f.read(size)
    print("Data:", data.hex())
