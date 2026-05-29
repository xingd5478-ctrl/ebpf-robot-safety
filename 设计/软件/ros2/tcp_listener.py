#!/usr/bin/env python3
"""WSL2 TCP listener → creates a virtual serial port bridge.
Usage: python3 tcp_listener.py 9998
Then:   python3 robot_control.py --serial socket://localhost:9999
"""
import socket, threading, sys, os, pty

TCP_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9998
VIRTUAL_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9999

# Create virtual serial port (server-side socket)
vserver = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
vserver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
vserver.bind(('127.0.0.1', VIRTUAL_PORT))
vserver.listen(1)

# Listen for Windows bridge
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(('0.0.0.0', TCP_PORT))
server.listen(1)
print(f'[wsl] Listening TCP:{TCP_PORT} for Windows bridge...')
print(f'[wsl] Virtual serial on 127.0.0.1:{VIRTUAL_PORT}')

win_sock, addr = server.accept()
print(f'[wsl] Windows bridge connected from {addr}')

vclient, _ = vserver.accept()
print(f'[wsl] Client connected to virtual serial')

def fwd(a, b, name):
    try:
        while True:
            data = a.recv(4096)
            if not data: break
            b.sendall(data)
    except: pass

t1 = threading.Thread(target=fwd, args=(win_sock, vclient, 'win->vser'), daemon=True)
t2 = threading.Thread(target=fwd, args=(vclient, win_sock, 'vser->win'), daemon=True)
t1.start(); t2.start()

try:
    while t1.is_alive() and t2.is_alive():
        t1.join(1)
except KeyboardInterrupt:
    pass
finally:
    win_sock.close(); vclient.close(); server.close(); vserver.close()
    print('[wsl] closed')
