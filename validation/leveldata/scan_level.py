import sys,os,pickle
sys.path.insert(0,''+__import__('os').path.dirname(__file__)+'')
from bundles import level_bundles
U='/mnt/user-data/uploads/Battlefield_3_Server/Data/Linux/Levels/'
for L in sys.argv[1:]:
    out=''+__import__('os').path.dirname(__file__)+'/%s.pkl'%L
    if os.path.exists(out): print(L,'done'); continue
    sb=U+'%s/%s.sb'%(L,L); toc=U+'%s/%s.toc'%(L,L)
    if not (os.path.exists(sb) and os.path.exists(toc)): print(L,'not staged'); continue
    bs=level_bundles(toc,sb)
    errs=[k for k,v in bs.items() if isinstance(v,tuple)]
    pickle.dump(bs,open(out,'wb'))
    print(L,len(set(n for v in bs.values() if isinstance(v,list) for n in v)),'errors',errs)
    os.remove(sb)
