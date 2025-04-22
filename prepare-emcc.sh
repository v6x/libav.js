#!/usr/bin/env bash
set -euo pipefail

VERSION=3.1.74
EMSDK_DIR="${PWD}/.emsdk"

install_emsdk() {
  git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
  cd "$EMSDK_DIR"

  # use system python
  export EMSDK_PYTHON=$(which python3)

  # install with system python
  ./emsdk install "$VERSION" --embedded
  echo "installed emscripten $VERSION"

  # activate
  ./emsdk activate "$VERSION"
  echo "activated emscripten $VERSION"

  cd - > /dev/null
}

prepare_env() {
  # shellcheck disable=SC1091
  source "$EMSDK_DIR/emsdk_env.sh"
}

if [ ! -d "$EMSDK_DIR" ] || [ ! -s "$EMSDK_DIR/emsdk_env.sh" ]; then
  install_emsdk
else
  prepare_env
  if ! emcc --version | grep -q "^emcc .* $VERSION"; then
    rm -rf "$EMSDK_DIR"
    install_emsdk
  fi
fi

prepare_env

emcc --version | grep -q "^emcc .* $VERSION" \
  && echo "emscripten $VERSION ready" \
  || { echo "Failed to prepare emcc $VERSION" >&2; exit 1; }