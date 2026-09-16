import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


entry = module("entry", "docker/entrypoint.py")
updater = module("updater", "swarm/update.py")
PREFIX = "ghcr.io/test/server"
IMAGES = {role: PREFIX + "-" + role + "@sha256:" + "a" * 64 for role in ("server", "verify")}


class DeploymentTests(unittest.TestCase):
    def test_internal_peer_and_public_routing_are_separate(self):
        status = entry.configuration("status", "chat.example.com")
        self.assertEqual(status["ChatServer2"]["Host"], "chat.example.com")
        self.assertEqual(status["ChatServer2"]["Port"], "8091")
        for n in (1, 2):
            chat = entry.configuration("chat" + str(n), "chat.example.com")
            peer = chat["ChatServer" + str(3-n)]
            self.assertEqual(peer["Host"], "chat" + str(3-n))
            self.assertEqual(peer["Port"], str(50057-n))
            self.assertNotIn("Password", chat["MySQL"])

    def test_invalid_configuration(self):
        for role, host in (("invalid", "host"), ("gate", "host\n[Redis]")):
            with self.assertRaises(ValueError):
                entry.configuration(role, host)

    def test_only_immutable_allowlisted_images(self):
        manifest = {"schema": 1, "revision": "b"*40, "images": IMAGES.copy()}
        self.assertEqual(updater.validate(manifest, PREFIX), IMAGES)
        for bad in ("evil.io/image@sha256:" + "a"*64, PREFIX + "-server:latest"):
            manifest["images"]["server"] = bad
            with self.assertRaises(ValueError):
                updater.validate(manifest, PREFIX)

    def fixture(self):
        services = {}
        for role, kind in updater.ORDER:
            services["sakura_" + role] = {"Spec": {
                "Labels": {"io.sakura.autoupdate": kind},
                "Mode": {"Replicated": {"Replicas": 1}},
                "UpdateConfig": {"Order": "stop-first"},
                "TaskTemplate": {"ContainerSpec": {"Image": "old-" + role}}}}
        return services

    def test_sequential_rollout_and_noop(self):
        services, calls = self.fixture(), []
        def update(name, image):
            calls.append(name)
            services[name]["Spec"]["TaskTemplate"]["ContainerSpec"]["Image"] = image
        updater.rollout("sakura", IMAGES, services.__getitem__, update)
        self.assertEqual(calls, ["sakura_" + role for role, _ in updater.ORDER])
        calls.clear()
        updater.rollout("sakura", IMAGES, services.__getitem__, update)
        self.assertEqual(calls, [])

    def test_preflight_prevents_partial_unsafe_changes(self):
        services = self.fixture()
        services["sakura_chat2"]["Spec"]["UpdateConfig"]["Order"] = "start-first"
        calls = []
        with self.assertRaises(ValueError):
            updater.rollout("sakura", IMAGES, services.__getitem__, lambda *args: calls.append(args))
        self.assertEqual(calls, [])

    def test_failure_rolls_back_only_attempted_services(self):
        services, calls = self.fixture(), []
        def update(name, image):
            calls.append((name, image))
            services[name]["Spec"]["TaskTemplate"]["ContainerSpec"]["Image"] = image
            if name == "sakura_chat1" and image == IMAGES["server"]:
                raise RuntimeError("unhealthy")
        with self.assertRaises(RuntimeError):
            updater.rollout("sakura", IMAGES, services.__getitem__, update)
        self.assertEqual([name for name, _ in calls[4:]],
                         ["sakura_chat1", "sakura_gate", "sakura_status", "sakura_verify"])
        self.assertTrue(all(value["Spec"]["TaskTemplate"]["ContainerSpec"]["Image"].startswith("old-")
                            for value in services.values()))


if __name__ == "__main__":
    unittest.main()
