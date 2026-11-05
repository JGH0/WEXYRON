#!/bin/bash
set -e

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

install_msys2() {
    local prefix="$1"
    echo ""
    echo "Installing MSYS2 development packages ($prefix)..."
    pacman -Sy --noconfirm
    pacman -S --noconfirm \
        "${prefix}-gcc" \
        "${prefix}-make" \
        "${prefix}-cmake" \
        "${prefix}-glfw" \
        "${prefix}-glm" \
        "${prefix}-glew" \
        "${prefix}-SDL2" \
        "${prefix}-openssl"
}

install_arch() {
    echo ""
    echo "Installing Arch Linux development packages..."
    sudo pacman -Sy --noconfirm \
        base-devel \
        gcc \
        make \
        cmake \
        glfw-x11 \
        glm \
        glew \
        sdl2 \
        openssl
}

install_debian() {
    echo ""
    echo "Installing Debian/Ubuntu development packages..."
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        g++ \
        make \
        cmake \
        libglfw3-dev \
        libglm-dev \
        uuid-dev \
        libglew-dev \
        libsdl2-dev \
        libssl-dev
}

# ── Platform detection ────────────────────────────────────────────────

if [ "$OS" = "Linux" ]; then
    # Check if pacman (Arch) or apt (Debian/Ubuntu) is available
    if command -v pacman &>/dev/null; then
        install_arch
    elif command -v apt-get &>/dev/null; then
        install_debian
    else
        echo "Unsupported Linux distribution. Please install the following manually:"
        echo "  gcc, make, cmake, glfw, glm, glew"
        exit 1
    fi
elif echo "$OS" | grep -qi "MINGW\|MSYS\|CYGWIN"; then
    # Running inside MSYS2 — offer environment selection
    echo "Installing MSYS2 development packages..."
    echo ""
    echo "Select MSYS2 environment:"
    echo "  1) UCRT64 (default)"
    echo "  2) MINGW64 (recommended for Makefile.windows)"
    read -p "Choice (1/2): " ENV_CHOICE
    ENV_CHOICE=${ENV_CHOICE:-1}
    if [ "$ENV_CHOICE" = "2" ]; then
        install_msys2 "mingw-w64-x86_64"
        echo "Selected: MINGW64"
    else
        install_msys2 "mingw-w64-ucrt-x86_64"
        echo "Selected: UCRT64"
    fi
else
    echo "Unsupported platform: $OS"
    echo "Please install the following manually:"
    echo "  gcc, make, cmake, glfw, glm, glew"
    exit 1
fi

# ── Verification ──────────────────────────────────────────────────────

echo ""
echo "Installation completed."
echo "Verifying tools..."

which gcc 2>/dev/null || echo "(gcc not found)"
which make 2>/dev/null || echo "(make not found)"
which mingw32-make 2>/dev/null || echo "(mingw32-make not found — expected on Linux or if mingw-w64-make not installed)"

echo ""
echo "GCC version:"
gcc --version 2>/dev/null | head -1 || echo "(gcc not found)"

echo ""
echo "Make version:"
make --version 2>/dev/null | head -1 || echo "(make not found)"
