#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ $# -eq 0 ]]; then
  CONFIGS=(release)
else
  CONFIGS=()
  for arg in "$@"; do
    CONFIGS+=("${arg,,}")
  done
fi

ARCH="$(uname -m)"
RUNTIME_ROOT="${ROOT_DIR}/dotnet/RiveRenderer/runtimes/linux-${ARCH}/native"
ARTIFACT_ROOT="${ROOT_DIR}/artifacts/native/linux-${ARCH}"

PREMAKE_DIR="${ROOT_DIR}/extern/river-renderer/build/dependencies/premake-core/bin/release"
if [[ -x "${PREMAKE_DIR}/premake5" ]]; then
  export PATH="${PREMAKE_DIR}:${PATH}"
fi

mkdir -p "${RUNTIME_ROOT}"

SEARCH_DIRS=(
  "${ROOT_DIR}/extern/river-renderer/out"
  "${ROOT_DIR}/extern/river-renderer/renderer/out"
  "${ROOT_DIR}/extern/river-renderer/decoders/out"
)

LIB_PATTERNS=(
  librive_pls_renderer.a
  librive.a
  librive_decoders.a
  librive_harfbuzz.a
  librive_sheenbidi.a
  librive_yoga.a
  libminiaudio.a
  liblibpng.a
  liblibjpeg.a
  liblibwebp.a
  libzlib.a
)

find_native_lib() {
  local pattern="$1"
  local config="$2"
  local candidate=""
  local dir found
  for dir in "${SEARCH_DIRS[@]}"; do
    if [[ ! -d "${dir}" ]]; then
      continue
    fi
    while IFS= read -r found; do
      if [[ "${found}" == *"/${config}/"* ]]; then
        echo "${found}"
        return 0
      fi
      if [[ -z "${candidate}" ]]; then
        candidate="${found}"
      fi
    done < <(find "${dir}" -type f -name "${pattern}" -print 2>/dev/null | sort)
  done
  if [[ -n "${candidate}" ]]; then
    echo "${candidate}"
    return 0
  fi
  return 1
}

if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake is required" >&2
  exit 1
fi

for config in "${CONFIGS[@]}"; do
  echo "==> Preparing river-renderer workspace"
  (
    cd "${ROOT_DIR}/extern/river-renderer"
    if [[ ! -f premake5.lua ]]; then
      ln -sf premake5_v2.lua premake5.lua
    fi
    RIVE_PREMAKE_ARGS="--with_rive_text --with_rive_layout" ./build/build_rive.sh clean >/dev/null 2>&1 || true
    rm -rf out
  )

  echo "==> Building river-renderer (${config})"
  (
    cd "${ROOT_DIR}/extern/river-renderer"
    if [[ ! -f premake5.lua ]]; then
      ln -sf premake5_v2.lua premake5.lua
    fi
    RIVE_PREMAKE_ARGS="--with_rive_text --with_rive_layout" ./build/build_rive.sh "${config}"
  )

  echo "==> Building river-renderer GPU targets (${config})"
  (
    cd "${ROOT_DIR}/extern/river-renderer/renderer"
    RIVE_PREMAKE_ARGS="--with_rive_text --with_rive_layout" ../build/build_rive.sh clean >/dev/null 2>&1 || true
    rm -rf out
    RIVE_PREMAKE_ARGS="--with_rive_text --with_rive_layout" ../build/build_rive.sh "${config}"
  )

  BUILD_DIR="${ROOT_DIR}/renderer_ffi/build-linux-${config}"
  echo "==> Configuring renderer_ffi (${config})"
  cmake -S "${ROOT_DIR}/renderer_ffi" -B "${BUILD_DIR}" -G "Ninja" -DCMAKE_BUILD_TYPE="${config^}"

  echo "==> Building renderer_ffi (${config})"
  cmake --build "${BUILD_DIR}"

  FFI_LIB="${BUILD_DIR}/out/${config^}/librive_renderer_ffi.so"
  if [[ ! -f "${FFI_LIB}" ]]; then
    echo "error: expected ${FFI_LIB} to exist" >&2
    exit 1
  fi

  DEST_DIR="${RUNTIME_ROOT}/${config}"
  mkdir -p "${DEST_DIR}"
  cp "${FFI_LIB}" "${DEST_DIR}/"

  for pattern in "${LIB_PATTERNS[@]}"; do
    LIB_PATH="$(find_native_lib "${pattern}" "${config}")"
    if [[ -z "${LIB_PATH}" ]]; then
      echo "warning: missing ${pattern} for ${config}, skipping" >&2
      continue
    fi
    cp "${LIB_PATH}" "${DEST_DIR}/"
  done

  if [[ "${config}" == "release" ]]; then
    echo "==> Updating shared runtime payload (${RUNTIME_ROOT})"
    find "${DEST_DIR}" -maxdepth 1 -type f -exec cp "{}" "${RUNTIME_ROOT}/" \;
  fi

  STAGE_DIR="${ARTIFACT_ROOT}/${config}"
  mkdir -p "${STAGE_DIR}"
  cp -R "${DEST_DIR}"/* "${STAGE_DIR}/"
  (cd "${STAGE_DIR}" && ls -1 > manifest.txt)
done

echo "Build complete. Artifacts staged under ${RUNTIME_ROOT}";
