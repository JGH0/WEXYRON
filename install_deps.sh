#!/bin/bash
echo "Installing MSYS2 development packages..."
echo ""
echo "Select MSYS2 environment:"
echo "  1) UCRT64 (default)"
echo "  2) MINGW64 (recommended for Makefile.windows)"
read -p "Choice (1/2): " ENV_CHOICE
ENV_CHOICE=${ENV_CHOICE:-1}

if [ "$ENV_CHOICE" = "2" ]; then
    PKG_PREFIX="mingw-w64-x86_64"
    echo "Selected: MINGW64"
else
    PKG_PREFIX="mingw-w64-ucrt-x86_64"
    echo "Selected: UCRT64"
fi
echo ""

# Update package database
pacman -Sy --noconfirm

# Install base development tools
pacman -S --noconfirm ${PKG_PREFIX}-gcc ${PKG_PREFIX}-make ${PKG_PREFIX}-cmake

# Install OpenGL and graphics libraries
pacman -S --noconfirm ${PKG_PREFIX}-glfw ${PKG_PREFIX}-glm ${PKG_PREFIX}-glew

# Install additional libraries that might be needed
pacman -S --noconfirm ${PKG_PREFIX}-SDL2 ${PKG_PREFIX}-openssl

echo "Installation completed."
echo "Verifying tools..."

# Check if tools are available
which gcc
which make
which mingw32-make

echo "GCC version:"
gcc --version | head -1

echo "Make version:"
make --version | head -1
