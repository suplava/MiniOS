"""MiniOS Viz Bridge - serve dashboard, poll viz_pipe.txt for VIZ events."""
import sys, os, json, time, threading, queue, struct, socketserver

os.chdir(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.getcwd())
event_queue = queue.Queue()
ws_clients = []
qemu_conn = None  # TCP connection to QEMU (for --tcp mode)
PIPE_FILE = os.path.join(ROOT, "viz_pipe.txt")

def parse_viz(line):
    idx = line.find("[VIZ]")
    if idx < 0: return None
    try: return json.loads(line[idx+5:].strip())
    except: return None

# === WebSocket ===
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
def ws_accept_key(key):
    import hashlib, base64
    return base64.b64encode(hashlib.sha1((key+GUID).encode()).digest()).decode()
def ws_send(conn, text):
    data = text.encode(); n = len(data)
    if n < 126: hdr = struct.pack(">BB", 0x81, n)
    elif n < 65536: hdr = struct.pack(">BBH", 0x81, 126, n)
    else: hdr = struct.pack(">BBQ", 0x81, 127, n)
    try: conn.sendall(hdr+data)
    except: pass

def ws_handle(conn):
    ws_clients.append(conn)
    try:
        while True:
            hdr = conn.recv(2)
            if not hdr: break
            op = hdr[0] & 0xF
            if op == 8: break
            if op == 9: conn.sendall(struct.pack(">BB", 0x8A, 0)); continue
            if op not in (1,2): continue
            masked = hdr[1] & 0x80
            length = hdr[1] & 0x7F
            if length == 126: length = struct.unpack(">H", conn.recv(2))[0]
            elif length == 127: length = struct.unpack(">Q", conn.recv(8))[0]
            mask = conn.recv(4) if masked else None
            payload = conn.recv(length)
            if mask: payload = bytes(b ^ mask[i%4] for i,b in enumerate(payload))
            # Forward text frames to QEMU
            if op == 1 and qemu_conn:
                try: qemu_conn.sendall(payload + b'\r\n')
                except: pass
    except: pass
    finally:
        if conn in ws_clients: ws_clients.remove(conn)
        try: conn.close()
        except: pass

# === HTTP ===
DASHBOARD = open("dashboard.html","rb").read()

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            data = self.request.recv(8192)
            if not data: return
            req = data.decode("utf-8","replace")
            lines = req.split("\r\n")
            if not lines: return
            p = lines[0].split()
            if len(p)<2: return
            path = p[1]
            print(f"[bridge] HTTP {p[0]} {path}", flush=True)
            if path == "/ws":
                key = ""
                for l in lines:
                    if l.lower().startswith("sec-websocket-key:"):
                        key = l.split(":",1)[1].strip()
                if key:
                    resp = f"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {ws_accept_key(key)}\r\n\r\n"
                    self.request.sendall(resp.encode())
                    ws_handle(self.request)
                return
            if path in ("/","/dashboard.html"):
                body = DASHBOARD
            else:
                self.request.sendall(b"HTTP/1.1 404\r\nContent-Length:0\r\n\r\n")
                return
            resp = f"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n"
            self.request.sendall(resp.encode()+body)
        except: pass
        finally:
            try: self.request.close()
            except: pass

# === TCP reader (QEMU mode) ===
def tcp_reader(port):
    """Bidirectional TCP proxy: QEMU serial <-> terminal, extract VIZ events."""
    import socket, select
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', port))
    sock.listen(1)
    print(f"[bridge] Waiting for QEMU TCP on port {port}...", flush=True)
    global qemu_conn
    conn, addr = sock.accept()
    qemu_conn = conn
    print(f"[bridge] QEMU connected. Type commands here or in dashboard.", flush=True)
    print("", flush=True)

    # Make stdin non-blocking on Windows
    if sys.platform == 'win32':
        import msvcrt
        def stdin_ready():
            return msvcrt.kbhit()
        def stdin_read():
            return msvcrt.getwch()
    else:
        import termios, tty, fcntl
        fd = sys.stdin.fileno()
        old_flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, old_flags | os.O_NONBLOCK)
        old_attrs = termios.tcgetattr(fd)
        tty.setraw(fd)
        def stdin_ready():
            return select.select([sys.stdin], [], [], 0)[0]
        def stdin_read():
            return sys.stdin.read(1)

    conn.setblocking(False)
    qemu_buf = b''
    stdin_buf = ''

    try:
        while True:
            # Read from QEMU -> terminal + parse VIZ
            try:
                data = conn.recv(4096)
                if data:
                    sys.stdout.write(data.decode('utf-8', 'replace'))
                    sys.stdout.flush()
                    qemu_buf += data
                    while b'\n' in qemu_buf:
                        line, qemu_buf = qemu_buf.split(b'\n', 1)
                        line_str = line.decode('utf-8', 'replace').strip()
                        if line_str:
                            event_queue.put({"type":"term","line":line_str})
                            evt = parse_viz(line_str)
                            if evt:
                                event_queue.put(evt)
                elif data == b'':
                    break
            except (BlockingIOError, socket.timeout):
                pass

            # Read from terminal -> QEMU
            if stdin_ready():
                ch = stdin_read()
                conn.sendall(ch.encode() if isinstance(ch, str) else ch)

            time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    finally:
        conn.close()
        sock.close()
        print("[bridge] QEMU disconnected", flush=True)

# === File poller ===
def file_poller():
    """Poll viz_pipe.txt for new VIZ events."""
    # Start from current end of file (skip old events)
    pos = os.path.getsize(PIPE_FILE) if os.path.exists(PIPE_FILE) else 0
    count = 0
    while True:
        try:
            if os.path.exists(PIPE_FILE):
                with open(PIPE_FILE, 'r', encoding='utf-8', errors='replace') as f:
                    f.seek(pos)
                    for line in f:
                        line = line.strip()
                        if line:
                            event_queue.put({"type":"term","line":line})
                            evt = parse_viz(line)
                            if evt:
                                event_queue.put(evt)
                                count += 1
                                if count <= 5 or count % 20 == 0:
                                    print(f"[bridge] read {count} events, last: {evt['type']}", flush=True)
                    pos = f.tell()
            time.sleep(0.3)
        except Exception as e:
            print(f"[bridge] file error: {e}", flush=True)
            time.sleep(0.5)

# === Broadcaster ===
def broadcaster():
    sent = 0
    while True:
        try:
            evt = event_queue.get(timeout=0.3)
            msg = json.dumps(evt)
            dead = []
            for c in ws_clients:
                try: ws_send(c, msg)
                except: dead.append(c)
            for d in dead:
                if d in ws_clients: ws_clients.remove(d)
            sent += 1
            if sent <= 5 or sent % 20 == 0:
                print(f"[bridge] broadcast {sent} events, clients={len(ws_clients)}", flush=True)
        except queue.Empty: pass

# === Main ===
mode = sys.argv[1] if len(sys.argv) > 1 else "file"
tcp_port = int(sys.argv[2]) if len(sys.argv) > 2 else 5678

if mode == "--tcp":
    print(f"[bridge] MiniOS Viz Bridge (TCP mode, port {tcp_port})", flush=True)
else:
    print("[bridge] MiniOS Viz Bridge (file poll mode)", flush=True)

# Start HTTP server FIRST
for port in range(8765, 8771):
    try:
        srv = socketserver.ThreadingTCPServer(("0.0.0.0", port), Handler)
        srv.daemon_threads = True
        print(f"[bridge] Dashboard: http://localhost:{port}", flush=True)
        break
    except OSError:
        print(f"[bridge] Port {port} in use, trying next...", flush=True)
else:
    print("[bridge] ERROR: all ports 8765-8770 in use!", flush=True)
    sys.exit(1)

# Start reader + broadcaster
if mode == "--tcp":
    threading.Thread(target=tcp_reader, args=(tcp_port,), daemon=True).start()
    print(f"[bridge] Start QEMU with: -serial tcp:localhost:{tcp_port},server", flush=True)
else:
    threading.Thread(target=file_poller, daemon=True).start()
    print("[bridge] Run MiniOS in ANOTHER terminal: .\\minios.exe", flush=True)
threading.Thread(target=broadcaster, daemon=True).start()

srv.serve_forever()
