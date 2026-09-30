#!/bin/sh
# birun installer
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/barshansarkar/birun/main/install.sh | sh
set -eu

REPO="barshansarkar/birun"
BIN="birun"
INSTALL_DIR="${BIRUN_INSTALL:-$HOME/.local/bin}"

uname_s=$(uname -s)
uname_m=$(uname -m)

case "$uname_s" in
    Linux*)  OS="linux"  ;;
    Darwin*) OS="darwin" ;;
    *)       echo "error: unsupported OS: $uname_s" >&2; exit 1 ;;
esac

case "$uname_m" in
    x86_64|amd64)  ARCH="x86_64"  ;;
    aarch64|arm64) ARCH="aarch64" ;;
    *)             echo "error: unsupported arch: $uname_m" >&2; exit 1 ;;
esac

if [ -z "${BIRUN_VERSION:-}" ]; then
    BIRUN_VERSION=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
        | grep '"tag_name"' \
        | sed -E 's/.*"tag_name": *"v?([^"]+)".*/\1/')
fi

if [ -z "$BIRUN_VERSION" ]; then
    echo "error: could not determine latest version" >&2
    exit 1
fi

echo "  installing birun v$BIRUN_VERSION ($OS-$ARCH)"

TARBALL="${BIN}-${BIRUN_VERSION}-${OS}-${ARCH}.tar.gz"
URL="https://github.com/$REPO/releases/download/v${BIRUN_VERSION}/${TARBALL}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "  downloading $URL"
if ! curl -fsSL "$URL" -o "$TMP/$TARBALL"; then
    echo "error: download failed" >&2
    exit 1
fi

tar -xzf "$TMP/$TARBALL" -C "$TMP"
mkdir -p "$INSTALL_DIR"
install -m 0755 "$TMP/$BIN" "$INSTALL_DIR/$BIN"

echo "  installed to $INSTALL_DIR/$BIN"

case ":$PATH:" in
    *":$INSTALL_DIR:"*) ;;
    *)
        echo
        echo "  note: $INSTALL_DIR is not in your PATH."
        echo "  add this to your shell profile:"
        echo
        echo "      export PATH=\"$INSTALL_DIR:\$PATH\""
        echo
        ;;
esac

"$INSTALL_DIR/$BIN" --version
