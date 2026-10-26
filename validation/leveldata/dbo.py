import struct,sys
sys.path.insert(0,''+__import__('os').path.dirname(__file__)+'')
from unxor import unxor
def varint(b,p):
    r=0;s=0
    while True:
        c=b[p];p+=1;r|=(c&0x7f)<<s;s+=7
        if not c&0x80: return r,p
def cstr(b,p):
    e=b.index(0,p);return b[p:e].decode('latin1'),e+1
def read(b,p):
    t=b[p];p+=1
    if t==0: return None,None,p
    name=None
    if not t&0x80: name,p=cstr(b,p)
    t&=0x1f
    if t in(1,2):
        n,p=varint(b,p);end=p+n
        items=[] if t==1 else {}
        while p<end:
            k,v,p=read(b,p)
            if k is None and v is None and p>=end: break
            if v is None and k is None: break
            if t==1: items.append(v)
            else: items[k]=v
        return name,items,end
    if t==6: return name,b[p]!=0,p+1
    if t==7:
        n,p=varint(b,p);return name,b[p:p+n-1].decode('latin1'),p+n
    if t==8: return name,struct.unpack_from('<i',b,p)[0],p+4
    if t==9: return name,struct.unpack_from('<q',b,p)[0],p+8
    if t==0xf: return name,b[p:p+16].hex(),p+16
    if t==0x10: return name,b[p:p+20].hex(),p+20
    if t==0x13:
        n,p=varint(b,p);return name,b[p:p+n],p+n
    raise ValueError('type %x at %d'%(t,p))
def load(path,obf=True):
    b=open(path,'rb').read()
    if obf: b=unxor(b)
    return read(b,0)[1]
