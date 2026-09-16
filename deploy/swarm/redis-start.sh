#!/bin/sh
set -eu
# Bootstrap generates hexadecimal passwords; reject config syntax characters.
password=$(cat /run/secrets/redis_password)
case "$password" in ''|*[!a-f0-9]*) echo 'Invalid Redis secret format' >&2; exit 1;; esac
umask 077
printf 'bind 0.0.0.0\nprotected-mode yes\nappendonly yes\ndir /data\nrequirepass %s\n' "$password" > /tmp/redis.conf
chown redis:redis /tmp/redis.conf
unset password
exec /usr/local/bin/docker-entrypoint.sh redis-server /tmp/redis.conf
