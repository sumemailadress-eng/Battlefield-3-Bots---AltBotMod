import re
def need_names(path='/mnt/user-data/uploads/Battlefield_3_Server/BotMod_loadouts.txt'):
    need={}
    kit=None
    for l in open(path):
        m=re.match(r'LOADOUT (\d+) (\S+)',l)
        if m: kit=m.group(2)+'#'+m.group(1)
        for a,b in re.findall(r'[0-9A-F]{8}:(\S+)|^P \d+ [0-9A-F]+ (\S+)',l):
            x=a or b
            need.setdefault(x,set()).add(kit)
    return need
