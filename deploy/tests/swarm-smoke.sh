#!/usr/bin/env bash
set -euo pipefail
# Destructive SQL is allowed only on an empty, ephemeral GitHub-hosted runner.
test "${GITHUB_ACTIONS:-}" = true
test "${RUNNER_ENVIRONMENT:-}" = github-hosted
test "$(docker info --format '{{.Swarm.LocalNodeState}}')" = inactive
test -z "$(docker volume ls --format '{{.Name}}' | grep -E '^sakura_(mysql|redis)$' || true)"
repo=$(pwd)
temporary=$(mktemp -d)
trap 'docker stack services sakura || true; docker service ps --no-trunc sakura_edge || true; docker service logs --tail 30 sakura_edge || true; docker service logs --tail 30 sakura_gate || true; docker service logs --tail 30 sakura_chat1 || true' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -days 30 -subj /CN=localhost \
  -addext subjectAltName=DNS:localhost -keyout "$temporary/public.key" -out "$temporary/public.crt" 2>/dev/null
docker run --rm --entrypoint nginx \
  --mount "type=bind,src=$repo/deploy/swarm/nginx.conf,target=/etc/nginx/nginx.conf,readonly" \
  --mount "type=bind,src=$temporary/public.crt,target=/run/secrets/public_cert,readonly" \
  --mount "type=bind,src=$temporary/public.key,target=/run/secrets/public_key,readonly" \
  nginx:1.28-alpine -t
docker swarm init >/dev/null
printf 'smoke@example.invalid\nci-only-unused-smtp-password\n' | python3 deploy/swarm/init-secrets.py \
  --public-host localhost --public-cert "$temporary/public.crt" --public-key "$temporary/public.key" \
  --directory "$temporary/secrets"
export SAKURA_PUBLIC_HOST=localhost SAKURA_SMTP_HOST=smtp.example.invalid
docker volume create sakura_mysql
docker volume create sakura_redis
cd deploy/swarm
docker stack config -c stack.yml >/dev/null
docker stack deploy --resolve-image never -c stack.yml sakura
cd "$repo"
mysql_container=''
for i in $(seq 1 90); do
  mysql_container=$(docker ps --filter label=com.docker.swarm.service.name=sakura_mysql -q | head -1)
  if test -n "$mysql_container" && docker exec "$mysql_container" sh -c \
    'export MYSQL_PWD="$(cat /run/secrets/mysql_root_password)"; mysql -uroot -e "SELECT 1"' >/dev/null 2>&1; then break; fi
  sleep 2
done
test -n "$mysql_container"
test "$(docker exec "$mysql_container" sh -c \
  'export MYSQL_PWD="$(cat /run/secrets/mysql_root_password)"; mysql -uroot -Nse "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=\"skrchat\""')" = 0
for sql in database/schema.sql database/002_chat_history.sql database/003_privacy.sql database/004_message_deletion.sql database/005_message_retention.sql database/006_private_chat.sql; do
  docker exec -i "$mysql_container" sh -c \
    'export MYSQL_PWD="$(cat /run/secrets/mysql_root_password)"; exec mysql -uroot skrchat' < "$sql"
done
for i in $(seq 1 120); do
  replicas=$(docker stack services sakura --format '{{.Replicas}}')
  if test "$(printf '%s\n' "$replicas" | grep -Ec '^(1/1|2/2)$')" = 8; then break; fi
  sleep 2
done
test "$(docker stack services sakura --format '{{.Replicas}}' | grep -Ec '^(1/1|2/2)$')" = 8
curl --fail --retry 20 --retry-all-errors --retry-delay 2 --cacert "$temporary/public.crt" https://localhost/healthz
for port in 8090 8091; do
  timeout 10 openssl s_client -connect "localhost:$port" -servername localhost \
    -CAfile "$temporary/public.crt" -verify_return_error </dev/null
done
# Exercise actual SQL credentials, Status RPC mutual TLS and node routing with a fixture account.
gate_container=$(docker ps --filter label=com.docker.swarm.service.name=sakura_gate -q | head -1)
test -n "$gate_container"
docker exec "$gate_container" python3 /opt/sakura/health.py
SMOKE_GATE_CONTAINER="$gate_container" SMOKE_MYSQL_CONTAINER="$mysql_container" SMOKE_CA="$temporary/public.crt" \
  python3 deploy/tests/http-smoke.py
redis_container=$(docker ps --filter label=com.docker.swarm.service.name=sakura_redis -q | head -1)
test "$(docker exec "$redis_container" sh -c \
  'export REDISCLI_AUTH="$(cat /run/secrets/redis_password)"; redis-cli HLEN _logincount')" = 2
# Each logical node must survive a stop-first replacement and re-register.
docker service update --force --detach=false sakura_chat1
docker service update --force --detach=false sakura_chat2
test "$(docker exec "$redis_container" sh -c \
  'export REDISCLI_AUTH="$(cat /run/secrets/redis_password)"; redis-cli HLEN _logincount')" = 2
echo 'Swarm startup, SQL migrations, TLS entrypoints and sequential node restart passed.'
