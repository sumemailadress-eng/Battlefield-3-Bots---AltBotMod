import struct,sys
sys.path.insert(0,''+__import__('os').path.dirname(__file__)+'')
from dbo import load
def bundle_ebx(sb,off):
    size,magic,total,ebxc,resc,chc,stro,mo,ms=struct.unpack_from('>9I',sb,off)
    assert magic in (0x970d1c13,0x970d1c14),hex(magic)
    base=off+4; p=base+32+20*total
    names=[]
    for i in range(ebxc):
        no,osz,_=struct.unpack_from('>III',sb,p+12*i)
        assert no==0 or sb[base+stro+no-1]==0, (i,no)
        s=base+stro+no; e=sb.index(0,s); names.append(sb[s:e].decode('latin1'))
    return names
def level_bundles(toc,sb):
    t=load(toc); data=open(sb,'rb').read(); out={}
    for b in t['bundles']:
        try: out[b['id']]=bundle_ebx(data,b['offset'])
        except Exception as ex: out[b['id']]=('ERR',str(ex))
    return out
