#!/usr/bin/env bash
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

# Build and install HemeLB, the geometry tool and the Python tools on
# macOS (Apple Silicon or Intel) or Debian/Ubuntu Linux.
#
# Run with --help for the options. Nothing is installed with sudo except
# system packages from apt.

set -euo pipefail

REPO_URL="https://github.com/lepotatoguy/hemelb.git"
REPO_BRANCH="fix/hemelb-improvements"

PREFIX="$HOME/.local/hemelb"
JOBS=""
ENV_NAME="gmy-tool"
ENV_PREFIX=""
INSTALL_SYSTEM_DEPS=1
INSTALL_GMY_TOOL=1
INSTALL_GUI=1
RUN_TESTS=1

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

  --prefix DIR        Install HemeLB here (default: $PREFIX)
  --jobs N            Parallel build jobs (default: number of CPUs)
  --env-name NAME     Conda environment for the geometry tool (default: $ENV_NAME)
  --env-prefix DIR    Create the conda environment at DIR instead of by name
  --no-system-deps    Do not install Homebrew or apt packages
  --no-gmy-tool       Build HemeLB only; skip the geometry and Python tools
  --no-gui            Skip the macOS GUI launcher setup
  --no-tests          Do not run hemelb-tests after building
  -h, --help          Show this help

Run it from a HemeLB checkout, or from anywhere to clone
$REPO_URL ($REPO_BRANCH) into ./hemelb.
EOF
}

log() { printf '\n==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --env-name) ENV_NAME="$2"; shift 2 ;;
        --env-prefix) ENV_PREFIX="$2"; shift 2 ;;
        --no-system-deps) INSTALL_SYSTEM_DEPS=0; shift ;;
        --no-gmy-tool) INSTALL_GMY_TOOL=0; shift ;;
        --no-gui) INSTALL_GUI=0; shift ;;
        --no-tests) RUN_TESTS=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage; die "unknown option: $1" ;;
    esac
done

OS="$(uname -s)"
ARCH="$(uname -m)"
case "$OS" in
    Darwin|Linux) ;;
    *) die "unsupported operating system: $OS" ;;
esac

if [[ -z "$JOBS" ]]; then
    if [[ "$OS" == Darwin ]]; then JOBS="$(sysctl -n hw.ncpu)"; else JOBS="$(nproc)"; fi
fi

run_root() {
    if [[ "$(id -u)" -eq 0 ]]; then "$@"; else sudo "$@"; fi
}

# ---------------------------------------------------------------------------
# Source tree
# ---------------------------------------------------------------------------
find_source() {
    local script_dir
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [[ -f "$script_dir/../CMakeLists.txt" && -d "$script_dir/../Code" ]]; then
        SRC="$(cd "$script_dir/.." && pwd)"
        return
    fi
    if [[ ! -d hemelb ]]; then
        log "Cloning $REPO_URL ($REPO_BRANCH)"
        git clone --branch "$REPO_BRANCH" "$REPO_URL" hemelb
    fi
    SRC="$(cd hemelb && pwd)"
}

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
install_macos_deps() {
    if ! xcode-select -p >/dev/null 2>&1; then
        xcode-select --install || true
        die "Install the Xcode Command Line Tools (a dialog has opened), then rerun this script."
    fi
    if ! command -v brew >/dev/null 2>&1; then
        log "Installing Homebrew"
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        if [[ -x /opt/homebrew/bin/brew ]]; then
            eval "$(/opt/homebrew/bin/brew shellenv)"
        else
            eval "$(/usr/local/bin/brew shellenv)"
        fi
    fi
    log "Installing Homebrew packages"
    brew install cmake open-mpi boost metis ctemplate tinyxml2 pkg-config git
}

install_linux_deps() {
    if ! command -v apt-get >/dev/null 2>&1; then
        die "Only apt-based distributions are handled automatically. Install a C++20 compiler, CMake >= 3.13, MPI, Boost, TinyXML-2, ParMETIS, CTemplate and zlib, then rerun with --no-system-deps."
    fi
    log "Installing apt packages"
    run_root apt-get update
    run_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake git curl ca-certificates pkg-config \
        libopenmpi-dev openmpi-bin libboost-dev libtinyxml2-dev \
        libparmetis-dev libmetis-dev libctemplate-dev zlib1g-dev \
        libgl1 libglx0 libopengl0 libxt6
}

# ---------------------------------------------------------------------------
# HemeLB core
# ---------------------------------------------------------------------------
build_hemelb() {
    log "Building HemeLB into $PREFIX ($JOBS jobs)"
    mkdir -p "$PREFIX"
    cmake -S "$SRC" -B "$SRC/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DHEMELB_DEPENDENCIES_INSTALL_PREFIX="$PREFIX" \
        -DHEMELB_SUBPROJECT_MAKE_JOBS="$JOBS"
    cmake --build "$SRC/build" -j "$JOBS"
    [[ -x "$PREFIX/bin/hemelb" ]] || die "build finished but $PREFIX/bin/hemelb is missing"
    if [[ "$RUN_TESTS" -eq 1 ]]; then
        log "Running hemelb-tests"
        "$PREFIX/bin/hemelb-tests"
    fi
}

# ---------------------------------------------------------------------------
# Geometry tool and Python tools
# ---------------------------------------------------------------------------
find_conda() {
    if command -v conda >/dev/null 2>&1; then
        CONDA_BASE="$(conda info --base)"
    else
        local candidate
        for candidate in "$HOME/miniforge3" "$HOME/anaconda3" "$HOME/miniconda3"; do
            if [[ -x "$candidate/bin/conda" ]]; then CONDA_BASE="$candidate"; break; fi
        done
    fi
    if [[ -z "${CONDA_BASE:-}" ]]; then
        log "Installing Miniforge (conda) into $HOME/miniforge3"
        local installer="Miniforge3-$OS-$ARCH.sh"
        curl -fsSL --retry 5 --retry-connrefused -o "/tmp/$installer" \
            "https://github.com/conda-forge/miniforge/releases/latest/download/$installer"
        bash "/tmp/$installer" -b -p "$HOME/miniforge3"
        rm -f "/tmp/$installer"
        CONDA_BASE="$HOME/miniforge3"
    fi
    # shellcheck disable=SC1091
    source "$CONDA_BASE/etc/profile.d/conda.sh"
}

install_gmy_tool() {
    find_conda

    local env_args
    if [[ -n "$ENV_PREFIX" ]]; then env_args=(-p "$ENV_PREFIX"); else env_args=(-n "$ENV_NAME"); fi

    # VMTK 1.5 is only published for x86_64, so Apple Silicon uses an
    # Intel environment under Rosetta 2.
    if [[ "$OS" == Darwin && "$ARCH" == arm64 ]]; then
        if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
            log "Installing Rosetta 2"
            softwareupdate --install-rosetta --agree-to-license
        fi
        export CONDA_SUBDIR=osx-64
    fi

    if conda env list | awk '{print $NF}' | grep -qx "${ENV_PREFIX:-.*/envs/$ENV_NAME}"; then
        die "conda environment '${ENV_PREFIX:-$ENV_NAME}' already exists. Remove it (conda env remove ${env_args[*]}) or choose another with --env-name."
    fi

    # The lock file pins every package to an exact build, so the environment
    # is the same as the one tested. Other platforms solve the pinned
    # environment file instead.
    local subdir="" lock=""
    if [[ "$OS" == Darwin ]]; then subdir=osx-64
    elif [[ "$ARCH" == x86_64 ]]; then subdir=linux-64
    fi
    if [[ -n "$subdir" ]]; then lock="$SRC/geometry-tool/conda-lock/$subdir.txt"; fi
    log "Creating conda environment ${ENV_PREFIX:-$ENV_NAME}"
    if [[ -n "$lock" && -f "$lock" ]]; then
        conda create -y "${env_args[@]}" --file "$lock"
    else
        conda env create "${env_args[@]}" -f "$SRC/geometry-tool/conda-environment.yml"
    fi
    conda activate "${ENV_PREFIX:-$ENV_NAME}"
    if [[ -n "${CONDA_SUBDIR:-}" ]]; then
        conda config --env --set subdir "$CONDA_SUBDIR"
    fi

    # --no-deps: every dependency comes from conda at its locked version, so
    # pip must not upgrade or add anything. --no-build-isolation: build
    # against the environment's NumPy, Cython and pybind11.
    log "Installing the Python tools and geometry tool"
    rm -rf "$SRC/geometry-tool/_skbuild"
    # Use the environment's CGAL, Boost and VTK, not Homebrew or system copies.
    export CMAKE_PREFIX_PATH="$CONDA_PREFIX"
    python -m pip install --no-deps --no-build-isolation "$SRC/python-tools"
    python -m pip install --no-deps --no-build-isolation "$SRC/geometry-tool"

    if [[ "$OS" == Darwin && "$INSTALL_GUI" -eq 1 ]]; then
        log "Setting up the macOS GUI launcher"
        # Already in the lock file (wxPython needs it); pinned for the fallback.
        conda install -y -c conda-forge python.app=1.4
        python "$SRC/geometry-tool/macos-fix-gui-launcher.py"
    fi

    hlb-gmy-cli --help >/dev/null
    conda deactivate
}

# ---------------------------------------------------------------------------
main() {
    find_source
    log "HemeLB source: $SRC ($OS $ARCH)"
    if [[ "$INSTALL_SYSTEM_DEPS" -eq 1 ]]; then
        if [[ "$OS" == Darwin ]]; then install_macos_deps; else install_linux_deps; fi
    fi
    build_hemelb
    if [[ "$INSTALL_GMY_TOOL" -eq 1 ]]; then install_gmy_tool; fi

    cat <<EOF

HemeLB is installed in $PREFIX/bin. Add it to your PATH:
    export PATH="$PREFIX/bin:\$PATH"
EOF
    if [[ "$INSTALL_GMY_TOOL" -eq 1 ]]; then
        cat <<EOF
Activate the geometry and Python tools with:
    conda activate ${ENV_PREFIX:-$ENV_NAME}
Commands: hlb-gmy-gui, hlb-gmy-cli, hlb-pro2pr2, hlb-dump-extracted-properties
EOF
    fi
}

main
