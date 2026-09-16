"""Disposable CI fixture: HTTP login, internal mTLS, and TLS chat login."""
import json
import os
import socket
import ssl
import struct
import subprocess
import urllib.request

assert os.environ.get("GITHUB_ACTIONS") == "true"
assert os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted"
password = "CI-smoke-only-password"
program = """
import ctypes, ctypes.util
s = ctypes.CDLL(ctypes.util.find_library('sodium'))
assert s.sodium_init() >= 0
out = ctypes.create_string_buffer(128)
p = b'CI-smoke-only-password'
s.crypto_pwhash_str.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_ulonglong,
                               ctypes.c_ulonglong, ctypes.c_size_t]
assert s.crypto_pwhash_str(out, p, len(p), 2, 67108864) == 0
print(out.value.decode())
"""
encoded = subprocess.check_output(["docker", "exec", os.environ["SMOKE_GATE_CONTAINER"],
                                   "python3", "-c", program], text=True).strip()
assert encoded.startswith("$argon2id$") and "'" not in encoded
sql = "INSERT INTO user(uid,name,email,pwd) VALUES(500001,'ci-smoke','smoke@example.invalid','" + encoded + "');"
subprocess.run(["docker", "exec", "-i", os.environ["SMOKE_MYSQL_CONTAINER"], "sh", "-c",
                'export MYSQL_PWD="$(cat /run/secrets/mysql_root_password)"; exec mysql -uroot skrchat'],
               input=sql, text=True, check=True)
context = ssl.create_default_context(cafile=os.environ["SMOKE_CA"])
request = urllib.request.Request("https://localhost/user_login", headers={"Content-Type": "application/json"},
                                 data=json.dumps({"email": "smoke@example.invalid", "passwd": password}).encode())
with urllib.request.urlopen(request, context=context, timeout=20) as response:
    login = json.load(response)
assert login["error"] == 0, "Login failed: " + str(login.get("error"))
assert login["host"] == "localhost" and int(login["port"]) in (8090, 8091)


def receive(sock, count):
    result = b""
    while len(result) < count:
        part = sock.recv(count - len(result))
        if not part:
            raise RuntimeError("Chat closed before login response")
        result += part
    return result


with socket.create_connection(("localhost", int(login["port"])), timeout=15) as raw:
    with context.wrap_socket(raw, server_hostname="localhost") as chat:
        payload = json.dumps({"uid": login["uid"], "token": login["token"]}).encode()
        chat.sendall(struct.pack("!HH", 1005, len(payload)) + payload)
        message_id, length = struct.unpack("!HH", receive(chat, 4))
        reply = json.loads(receive(chat, length))
        assert message_id == 1006 and reply["error"] == 0, "Chat login rejected"
print("HTTP authentication, Gate/Chat-to-Status mTLS, SQL credentials and chat TLS passed")
