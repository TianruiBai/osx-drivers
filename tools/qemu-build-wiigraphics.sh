#!/bin/sh

set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
REMOTE_HOST=${QEMU_TIGER_HOST:-192.168.64.6}
REMOTE_USER=${QEMU_TIGER_USER:-admin}
REMOTE_ROOT=${QEMU_TIGER_REMOTE_ROOT:-/tmp/osx-drivers}
BUILD_ARGS=

SSH_OPTS="-o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa -o PreferredAuthentications=password -o StrictHostKeyChecking=no"

REMOTE_PARENT=$(dirname "$REMOTE_ROOT")

if [ "$#" -gt 0 ]; then
  BUILD_ARGS=$(printf ' %s' "$@")
fi

echo "Syncing WiiGraphics sources to ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_ROOT}"
COPYFILE_DISABLE=1 tar -cf - -C "$REPO_ROOT" WiiGraphics common include | \
  ssh $SSH_OPTS "${REMOTE_USER}@${REMOTE_HOST}" \
    "rm -rf '$REMOTE_ROOT' && mkdir -p '$REMOTE_ROOT' '$REMOTE_PARENT' && tar -xf - -C '$REMOTE_ROOT'"

echo "Building WiiGraphics in Tiger QEMU"
ssh $SSH_OPTS "${REMOTE_USER}@${REMOTE_HOST}" \
  "cd '$REMOTE_ROOT/WiiGraphics' && (make clean >/dev/null 2>&1 || true) && make NATIVE_TIGER=1${BUILD_ARGS}"