import struct, sys, zlib


def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat, plte, trns = 8, b'', None, None
    while pos < len(d):
        n, kind = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            plte = [tuple(body[i:i + 3]) for i in range(0, n, 3)]
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    assert depth == 8 and interlace == 0, (depth, interlace)
    bpp = {6: 4, 2: 3, 3: 1, 4: 2, 0: 1}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]; line = bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line); prev = line
    px = []
    for line in rows:
        r = []
        for x in range(w):
            if ctype == 6: r.append(tuple(line[x * 4:x * 4 + 4]))
            elif ctype == 2: r.append(tuple(line[x * 3:x * 3 + 3]) + (255,))
            elif ctype == 3:
                k = line[x]; r.append(plte[k] + ((trns[k] if trns and k < len(trns) else 255),))
            elif ctype == 4: r.append((line[x * 2],) * 3 + (line[x * 2 + 1],))
            else: r.append((line[x],) * 3 + (255,))
        px.append(r)
    return w, h, px


def write_cur(path, w, h, px, size=32):
    assert w <= size and h <= size
    grid = [[(0, 0, 0, 0)] * size for _ in range(size)]
    for y in range(h):
        for x in range(w):
            grid[y][x] = px[y][x]
    hot = next((x, y) for y in range(size) for x in range(size) if grid[y][x][3] > 0)
    xor = b''.join(bytes((b, g, r, a)) for row in reversed(grid) for (r, g, b, a) in row)
    mask_stride = ((size + 31) // 32) * 4
    mask = b''
    for row in reversed(grid):
        bits = bytearray(mask_stride)
        for x, (_, _, _, a) in enumerate(row):
            if a == 0: bits[x // 8] |= 0x80 >> (x % 8)
        mask += bytes(bits)
    dib = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0) + xor + mask
    head = struct.pack('<HHH', 0, 2, 1) + struct.pack('<BBBBHHII', size, size, 0, 0, hot[0], hot[1], len(dib), 22)
    open(path, 'wb').write(head + dib)
    return hot


w, h, px = read_png(sys.argv[1])
print('png', w, h, 'hotspot', write_cur(sys.argv[2], w, h, px))
