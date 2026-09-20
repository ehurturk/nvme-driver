#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-2222}"
DEST="${DEST:-root@localhost}"
DEST_DIR="nvme_driver"

FILES=(CMakeLists.txt include src tests design.md)

cd "$(dirname "$0")"

ssh -p "$PORT" "$DEST" "mkdir -p ~/$DEST_DIR"
scp -P "$PORT" -r "${FILES[@]}" "$DEST:$DEST_DIR/"

echo "Synced ${FILES[*]} -> $DEST:~/$DEST_DIR"
