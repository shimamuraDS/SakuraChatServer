#!/usr/bin/env python3
"""One-time secret initialization on a new single-node Swarm manager."""
import argparse
import getpass
import os
from pathlib import Path
import secrets
import subprocess


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--public-cert", required=True, type=Path, help="PEM full certificate chain")
    parser.add_argument("--public-key", required=True, type=Path)
    parser.add_argument("--public-host", required=True)
    parser.add_argument("--directory", type=Path, default=Path("/etc/sakura/secrets"))
    args = parser.parse_args()
    existing = run("docker", "secret", "ls", "--format", "{{.Name}}").decode().splitlines()
    if any(name.startswith("sakura_") for name in existing):
        raise RuntimeError("Sakura secrets already exist. Use a planned versioned rotation, not initialization.")
    run("openssl", "x509", "-in", str(args.public_cert), "-noout", "-checkhost", args.public_host)
    run("openssl", "x509", "-in", str(args.public_cert), "-noout", "-checkend", "604800")
    certificate_key = run("openssl", "x509", "-in", str(args.public_cert), "-pubkey", "-noout")
    private_key = run("openssl", "pkey", "-in", str(args.public_key), "-pubout")
    if certificate_key != private_key:
        raise ValueError("Public certificate and private key do not match")
    os.umask(0o077)
    directory = args.directory.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    values = {name: secrets.token_hex(32) for name in
              ("redis_password", "mysql_password", "mysql_root_password", "verify_key")}
    values["smtp_user"] = input("SMTP account: ").strip()
    values["smtp_password"] = getpass.getpass("SMTP authorization password (hidden): ")
    if not all(values.values()):
        raise ValueError("Empty credentials are not allowed")
    files = {}
    for name, value in values.items():
        files[name] = directory / name
        files[name].write_text(value)
    ca_key, ca_cert = directory / "ca.key", directory / "ca.crt"
    run("openssl", "req", "-x509", "-newkey", "rsa:3072", "-nodes", "-days", "3650",
        "-subj", "/CN=Sakura Internal RPC CA", "-keyout", str(ca_key), "-out", str(ca_cert),
        "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    files["rpc_ca"] = ca_cert
    for role in ("gate", "status", "chat1", "chat2", "verify"):
        identity = "chat" if role.startswith("chat") else role
        key, csr, cert, ext = (directory / (role + suffix) for suffix in (".key", ".csr", ".crt", ".ext"))
        ext.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
                       "extendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=DNS:" + role + ",DNS:sakura-" + identity + "\n")
        run("openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=" + role,
            "-keyout", str(key), "-out", str(csr))
        run("openssl", "x509", "-req", "-in", str(csr), "-CA", str(ca_cert), "-CAkey", str(ca_key),
            "-CAcreateserial", "-days", "365", "-sha256", "-extfile", str(ext), "-out", str(cert))
        files[role + "_cert"] = cert
        files[role + ("_key_tls" if role == "verify" else "_key")] = key
    files["public_cert"] = args.public_cert.resolve()
    files["public_key"] = args.public_key.resolve()
    for name, path in files.items():
        run("docker", "secret", "create", "sakura_" + name + "_v1", str(path))
    print("Secrets created. Securely back up", directory, "; keep the CA private key offline.")
    print("Internal service certificates expire in 365 days. Schedule versioned rotation before expiry.")


if __name__ == "__main__":
    main()
