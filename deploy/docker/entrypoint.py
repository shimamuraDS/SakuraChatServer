"""Container configuration; secret values are never written to the generated config."""
import configparser
import ipaddress
import os
from pathlib import Path
import re
import socket
import time


def configuration(role, host):
    if role not in {"gate", "status", "chat1", "chat2", "verify"}:
        raise ValueError("Invalid SAKURA_ROLE")
    if not re.fullmatch(r"[a-zA-Z0-9.-]+", host):
        raise ValueError("SAKURA_PUBLIC_HOST must be a hostname or IPv4 address")
    sections = {
        "GateServer": {"Port": "8081"},
        "VarifyServer": {"Host": "verify", "Port": "50051"},
        "StatusServer": {"Host": "status", "Port": "50052"},
        "MySQL": {"Host": "mysql", "Port": "3306", "Schema": "skrchat"},
        "Redis": {"Host": "redis", "Port": "6379"},
    }
    if role == "status":
        sections["ChatServers"] = {"Name": "ChatServer1,ChatServer2"}
        for n in (1, 2):
            sections[f"ChatServer{n}"] = {"Name": f"ChatServer{n}", "Host": host, "Port": str(8089 + n)}
    elif role in {"chat1", "chat2"}:
        n = int(role[-1])
        sections["SelfServer"] = {"Name": f"ChatServer{n}", "Host": role,
                                  "Port": str(8089 + n), "RPCPort": str(50054 + n)}
        sections["PeerServer"] = {"Servers": f"ChatServer{3-n}"}
        sections[f"ChatServer{3-n}"] = {"Name": f"ChatServer{3-n}", "Host": f"chat{3-n}",
                                       "Port": str(50054 + 3 - n)}
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read_dict(sections)
    return cfg


def main():
    if os.environ.get("SAKURA_SECURITY_MODE") != "production":
        raise ValueError("Containers require production security mode")
    role = os.environ["SAKURA_ROLE"]
    cfg = configuration(role, os.environ.get("SAKURA_PUBLIC_HOST", "localhost"))
    if "SAKURA_TRUSTED_PROXY_CIDR" in os.environ:
        network = ipaddress.ip_network(os.environ["SAKURA_TRUSTED_PROXY_CIDR"])
        if network.version != 4 or network.prefixlen < 16 or not network.is_private:
            raise ValueError("Trusted proxy network must be a narrow private IPv4 subnet")
    for name in ("MYSQL_PASSWORD", "REDIS_PASSWORD", "VERIFY_SERVICE_KEY", "SMTP_USER", "SMTP_PASSWORD"):
        key = "SAKURA_" + name
        path = os.environ.get(key + "_FILE")
        if path:
            if key in os.environ:
                raise ValueError("Ambiguous secret source: " + key)
            value = Path(path).read_text().rstrip("\r\n")
            if not value or "\n" in value or "\r" in value:
                raise ValueError("Invalid secret: " + key)
            os.environ[key] = value
    os.umask(0o077)
    path = "/tmp/sakura-config"
    with open(path, "w") as stream:
        if role == "verify":
            stream.write("{}")
        else:
            cfg.write(stream)
    os.environ["SAKURA_CONFIG_PATH"] = path
    # Swarm does not wait for dependencies; avoid constructing empty connection pools.
    dependencies = [("redis", 6379)]
    if role in {"gate", "chat1", "chat2"}:
        dependencies.append(("mysql", 3306))
    for host, port in dependencies:
        deadline = time.monotonic() + 120
        while True:
            try:
                with socket.create_connection((host, port), timeout=3):
                    break
            except OSError:
                if time.monotonic() >= deadline:
                    raise RuntimeError("Dependency unavailable: " + host) from None
                time.sleep(2)
    if role == "verify":
        os.environ["SAKURA_REDIS_HOST"] = "redis"
        os.environ["SAKURA_VERIFY_BIND"] = "0.0.0.0:50051"
        os.execvp("node", ["node", "/opt/sakura/verify/server.js"])
    binary = {"gate": "GateServer", "status": "StatusServer", "chat1": "ChatServer", "chat2": "ChatServer"}[role]
    os.execv("/opt/sakura/bin/" + binary, [binary])


if __name__ == "__main__":
    main()
