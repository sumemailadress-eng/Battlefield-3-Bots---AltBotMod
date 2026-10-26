"""A small Frostbite (BF3) RCON server for testing AltBotMod Server Control. One instance per port; prints nothing.
Usage: mock_rcon.py <port> <password> <statefile>   (state is dumped as JSON after every command)"""
import json, socket, struct, sys, threading

port, password, statefile = int(sys.argv[1]), sys.argv[2], sys.argv[3]
VALID = json.load(open(sys.argv[4]))   # {map: [modes]}
state = {'maps': [['XP5_001', 'ConquestLarge0', 1], ['XP5_002', 'ConquestLarge0', 1]], 'current': 0, 'next': 1,
         'round': 0, 'log': [], 'vars': {'gameModeCounter': '100'}, 'saved': None,
         'players': [['Player1', '1', '2', '5', '3', '1200', '40'], ['[BOT] Medic US', '1', '1', '2', '7', '300', '0'],
                     ['[BOT] Recon RU', '2', '3', '9', '1', '900', '0']]}
lock = threading.Lock()

def words_to_packet(seq, words):
    body = b''.join(struct.pack('<I', len(w.encode())) + w.encode() + b'\0' for w in words)
    return struct.pack('<III', seq, 12 + len(body), len(words)) + body

def read_exact(c, n):
    b = b''
    while len(b) < n:
        r = c.recv(n - len(b))
        if not r: raise EOFError
        b += r
    return b

def handle(words, authed):
    cmd = words[0]
    if cmd == 'login.plainText':
        return (['OK'], True) if len(words) > 1 and words[1] == password else (['InvalidPassword'], False)
    if not authed: return ['LogInRequired'], False
    m = state['maps']
    if cmd == 'serverInfo':
        cur = m[state['current']] if m else ['', '']
        return ['OK', 'Test Server', '3', '64', cur[1], cur[0], str(state['round']), '2', '2', '187.5', '154', '0', 'Playing',
                'true', 'false', 'false', '1000', '321', '', '', '', '0', '', ''], True
    if cmd == 'mapList.list':
        out = ['OK', str(len(m)), '3']
        for e in m: out += [e[0], e[1], str(e[2])]
        return out, True
    if cmd == 'mapList.getMapIndices': return ['OK', str(state['current']), str(state['next'])], True
    if cmd == 'mapList.add':
        if words[1] not in VALID: return ['InvalidMap'], True
        if words[2] not in VALID[words[1]]: return ['InvalidGameModeOnMap'], True
        m.append([words[1], words[2], int(words[3]) if len(words) > 3 else 1]); return ['OK'], True
    if cmd == 'mapList.clear': m.clear(); state['current'] = state['next'] = 0; return ['OK'], True
    if cmd == 'mapList.remove':
        i = int(words[1])
        if i < 0 or i >= len(m): return ['InvalidMapIndex'], True
        m.pop(i); return ['OK'], True
    if cmd == 'mapList.save': state['saved'] = [list(e) for e in m]; return ['OK'], True
    if cmd == 'mapList.setNextMapIndex':
        i = int(words[1])
        if i < 0 or i >= len(m): return ['InvalidMapIndex'], True
        state['next'] = i; return ['OK'], True
    if cmd == 'mapList.runNextRound':
        state['current'] = state['next']; state['next'] = (state['current'] + 1) % max(1, len(m)); state['round'] = 0; return ['OK'], True
    if cmd in ('mapList.restartRound',): state['round'] = 0; return ['OK'], True
    if cmd == 'mapList.endRound': return (['OK'], True) if words[1] in ('1', '2') else (['InvalidArguments'], True)
    if cmd in ('admin.listPlayers', 'listPlayers'):
        fields = ['name', 'guid', 'teamId', 'squadId', 'kills', 'deaths', 'score', 'rank', 'ping']
        out = ['OK', str(len(fields))] + fields + [str(len(state['players']))]
        for p in state['players']: out += [p[0], 'EA_X', p[1], p[2], p[3], p[4], p[5], '10', p[6]]
        return out, True
    if cmd.startswith('vars.'):
        k = cmd[5:]
        if len(words) > 1 and k in ('hud',): return ['CommandIsReadOnly'], True   # (as a ranked BF3 server answers for some)
        if len(words) > 1: state['vars'][k] = words[1]; return ['OK'], True
        return ['OK', state['vars'].get(k, '')], True
    if cmd == 'admin.say': return ['OK'], True
    if cmd == 'admin.kickPlayer':
        state['players'] = [p for p in state['players'] if p[0] != words[1]]; return ['OK'], True
    return ['UnknownCommand'], True

def serve(c):
    authed = False
    try:
        while True:
            seq, size, n = struct.unpack('<III', read_exact(c, 12))
            body = read_exact(c, size - 12); words = []; o = 0
            for _ in range(n):
                l = struct.unpack_from('<I', body, o)[0]; words.append(body[o + 4:o + 4 + l].decode()); o += 5 + l
            with lock:
                out, authed2 = handle(words, authed)
                if words[0] == 'login.plainText': authed = authed2
                state['log'].append(words)
                json.dump(state, open(statefile, 'w'))
            c.sendall(words_to_packet((seq & 0x3fffffff) | 0x40000000 | (seq & 0x80000000), out))
    except (EOFError, ConnectionError, OSError):
        pass
    finally:
        c.close()

s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(('127.0.0.1', port)); s.listen(8)
json.dump(state, open(statefile, 'w'))
while True:
    c, _ = s.accept(); threading.Thread(target=serve, args=(c,), daemon=True).start()
