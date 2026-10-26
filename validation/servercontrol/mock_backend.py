"""A small BF-Portable backend (the /api/ pages Server Control uses) for testing the LAN servers window.
Usage: mock_backend.py <port> <name>   (serves http://127.0.0.1:<port>/api/; prints READY when listening)"""
import hashlib, json, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

port, name = int(sys.argv[1]), sys.argv[2]
KEY = 'key-' + name
users = {'Player1': hashlib.md5(b'pw1').hexdigest()}
servers = [{'gid': '7' + str(port % 100), 'gnam': name + ' Conquest', 'level': 'MP_001', 'levellocation': 'ConquestLarge0',
            'PLAYERS': '3/64', 'mail': 'bf3_1'}]

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, obj):
        b = json.dumps(obj).encode(); self.send_response(code)
        self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def keyok(self, q): return q.get('key', [''])[0] == KEY or self.headers.get('X-API-Key') == KEY
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query)
        if u.path == '/api/generate_key.php': return self.send(200, {'success': True, 'api_key': KEY})
        if u.path == '/api/get_servers_bf3.php':
            return self.send(200, servers) if self.keyok(q) else self.send(401, {'status': 'error', 'message': 'bad key'})
        self.send(404, {'status': 'error'})
    def do_POST(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query)
        f = urllib.parse.parse_qs(self.rfile.read(int(self.headers.get('Content-Length', 0))).decode())
        user, pw = f.get('username', [''])[0], f.get('password', [''])[0]
        if u.path == '/api/register_api.php':
            if not self.keyok(q): return self.send(401, {'status': 'error', 'message': 'bad key'})
            if user in users: return self.send(409, {'status': 'error', 'message': 'Username already exists'})
            users[user] = hashlib.md5(pw.encode()).hexdigest(); return self.send(200, {'status': 'success'})
        if u.path == '/api/login_api.php':
            if users.get(user) == hashlib.md5(pw.encode()).hexdigest():
                return self.send(200, {'status': 'success', 'AuthCode': 'auth-' + name + '-' + user, 'user_id': str(100 + list(users).index(user))})
            return self.send(200, {'status': 'error', 'message': 'Invalid username or password'})
        self.send(404, {'status': 'error'})

s = ThreadingHTTPServer(('127.0.0.1', port), H); print('READY', flush=True); s.serve_forever()
