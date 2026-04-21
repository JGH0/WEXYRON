#!/bin/bash
echo "Installing MinGW development packages..."

# Update package database
pacman -Sy --noconfirm

# Install base development tools
pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake

# Install OpenGL and graphics libraries
pacman -S --noconfirm mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm mingw-w64-ucrt-x86_64-glew

# Install additional libraries that might be needed
pacman -S --noconfirm mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-openssl

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
