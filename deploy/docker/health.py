"""Liveness only; schema correctness and mail delivery require functional checks."""
import os
import socket
import urllib.request

role = os.environ["SAKURA_ROLE"]
if role == "gate":
    with urllib.request.urlopen("http://127.0.0.1:8081/healthz", timeout=3) as response:
        assert response.status == 200
else:
    for port in {"status": [50052], "verify": [50051], "chat1": [8090, 50055], "chat2": [8091, 50056]}[role]:
        with socket.create_connection(("127.0.0.1", port), timeout=3):
            pass
