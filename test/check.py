import struct, sys
data = open(sys.argv[1], 'rb').read()
n = len(data) // 4096
ok = len(data) % 4096 == 0
for i in range(n - 1):
    ok &= data[i*4096:i*4096+16] == struct.pack('<8sII', b'NSPLXIMG', i, 0)
ok &= data[(n-1)*4096:(n-1)*4096+16] == struct.pack('<8sII', b'NSPLXEND', n, 1)
print(f'{n} chunks ({len(data)>>10} KiB), tags {"OK" if ok else "BAD"}')
