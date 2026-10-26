import sys
def unxor(b):
    if b[:4] not in (b"\x00\xd1\xce\x00", b"\x00\xd1\xce\x01"): return b
    key=b[296:296+260]; d=b[556:]
    return bytes(key[i%257]^d[i]^0x7b for i in range(len(d)))
if __name__=='__main__':
    b=open(sys.argv[1],'rb').read(); out=unxor(b); open(sys.argv[2],'wb').write(out)
