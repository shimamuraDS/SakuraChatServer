#!/usr/bin/env python3
"""Poll one atomic release manifest; update only explicitly labelled app services."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

ORDER = (("verify", "verify"), ("status", "server"), ("gate", "server"),
         ("chat1", "server"), ("chat2", "server"))


def validate(manifest, prefix):
    if manifest.get("schema") != 1 or not re.fullmatch(r"[0-9a-f]{40}", manifest.get("revision", "")):
        raise ValueError("Invalid release metadata")
    images = manifest.get("images", {})
    if set(images) != {"server", "verify"}:
        raise ValueError("Invalid release roles")
    for role, image in images.items():
        if not isinstance(image, str) or not re.fullmatch(re.escape(prefix + "-" + role) + r"@sha256:[0-9a-f]{64}", image):
            raise ValueError("Image is outside the permitted repository or is not immutable")
    return images


def docker(*args):
    return subprocess.run(["docker", *args], check=True, text=True, stdout=subprocess.PIPE,
                          timeout=900).stdout.strip()


def inspect(name):
    return json.loads(docker("service", "inspect", name))[0]


def change(name, image):
    print("Updating", name, "to", image, flush=True)
    docker("service", "update", "--with-registry-auth", "--detach=false", "--image", image, name)
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        service = inspect(name)
        state = service.get("UpdateStatus", {}).get("State", "")
        if state == "completed" and service["Spec"]["TaskTemplate"]["ContainerSpec"]["Image"] == image:
            return
        if state in {"paused", "rollback_started", "rollback_paused", "rollback_completed"}:
            raise RuntimeError(name + " did not complete its update: " + state)
        time.sleep(2)
    raise RuntimeError(name + " update monitor timed out")


def rollout(stack, images, read=inspect, update=change):
    plan = []
    # Preflight every target before modifying any service.
    for role, kind in ORDER:
        name = stack + "_" + role
        service = read(name)
        spec = service["Spec"]
        if spec.get("Labels", {}).get("io.sakura.autoupdate") != kind:
            raise ValueError("Missing or incorrect update label: " + name)
        if service.get("UpdateStatus", {}).get("State") in {"updating", "rollback_started"}:
            raise ValueError("Service already updating: " + name)
        if role.startswith("chat") and (spec["Mode"].get("Replicated", {}).get("Replicas") != 1
                                       or spec.get("UpdateConfig", {}).get("Order") != "stop-first"):
            raise ValueError("Chat nodes require one replica and stop-first updates")
        old = spec["TaskTemplate"]["ContainerSpec"]["Image"]
        if old != images[kind]:
            plan.append((name, old, images[kind]))
    changed = []
    try:
        for name, old, new in plan:
            changed.append((name, old))
            update(name, new)
    except Exception as failure:
        errors = []
        for name, old in reversed(changed):
            try:
                if read(name)["Spec"]["TaskTemplate"]["ContainerSpec"]["Image"] != old:
                    update(name, old)
            except Exception:
                errors.append(name)
        if errors:
            raise RuntimeError("Manual recovery required for " + ", ".join(errors)) from failure
        raise


def release(prefix):
    tag = prefix + "-release:stable"
    docker("pull", tag)
    info = json.loads(docker("image", "inspect", tag))[0]
    digest = next(ref for ref in info["RepoDigests"] if ref.startswith(prefix + "-release@sha256:"))
    container = docker("create", digest, "/unused")
    try:
        with tempfile.TemporaryDirectory(prefix="sakura-release-") as directory:
            target = Path(directory) / "release.json"
            docker("cp", container + ":/release.json", str(target))
            if target.stat().st_size > 16384:
                raise ValueError("Oversized release manifest")
            manifest = json.loads(target.read_text())
            return digest, validate(manifest, prefix)
    finally:
        docker("rm", container)


def save(path, state):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state))
    os.replace(temporary, path)


def main():
    import fcntl  # This operator utility runs on the Linux Swarm manager.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stack", default="sakura")
    parser.add_argument("--prefix", default="ghcr.io/shimamurads/sakurachatserver")
    parser.add_argument("--apply", action="store_true", help="Without this, only resolve and display images")
    parser.add_argument("--retry-failed", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", args.stack) or not re.fullmatch(r"ghcr\.io/[a-z0-9_.-]+/[a-z0-9_.-]+", args.prefix):
        parser.error("Invalid stack name or GHCR prefix")
    os.umask(0o077)
    with open("/run/lock/sakura-update.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        state_dir = Path("/var/lib/sakura-updater")
        state_dir.mkdir(parents=True, exist_ok=True)
        path = state_dir / "state.json"
        state = json.loads(path.read_text()) if path.exists() else {}
        digest, images = release(args.prefix)
        if not args.apply:
            print(json.dumps(images, indent=2))
            return
        if state.get("applied") == digest:
            print("Already applied", digest)
            return
        if state.get("failed") == digest and not args.retry_failed:
            raise RuntimeError("Release blocked after failure; inspect logs, then use --retry-failed")
        # Pull every image before changing any running service.
        for image in images.values():
            docker("pull", image)
        try:
            rollout(args.stack, images)
        except Exception:
            state["failed"] = digest
            save(path, state)
            raise
        save(path, {"applied": digest})
        print("Release applied", digest)


if __name__ == "__main__":
    main()
