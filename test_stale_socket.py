import socket
import os

sock_path = "/tmp/ncd_1000_control.sock"
if os.path.exists(sock_path):
    os.unlink(sock_path)

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.bind(sock_path)
s.listen(1)
s.close()
print("created stale socket")
