# PowerShell script to install MSYS2 dependencies
Write-Host "Installing MinGW development packages..."

# Update package database
Write-Host "Updating package database..."
& "C:\msys64\ucrt64.exe" -c "pacman -Sy --noconfirm"

# Install base development tools
Write-Host "Installing base development tools..."
& "C:\msys64\ucrt64.exe" -c "pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake"

# Install OpenGL and graphics libraries
Write-Host "Installing graphics libraries..."
& "C:\msys64\ucrt64.exe" -c "pacman -S --noconfirm mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm mingw-w64-ucrt-x86_64-glew"

# Install additional libraries
Write-Host "Installing additional libraries..."
& "C:\msys64\ucrt64.exe" -c "pacman -S --noconfirm mingw-w64-ucrt-x86_64-SDL2"

Write-Host "Installation completed."
Write-Host "Verifying tools..."

# Check if tools are available
& "C:\msys64\ucrt64.exe" -c "which gcc"
& "C:\msys64\ucrt64.exe" -c "which make"
& "C:\msys64\ucrt64.exe" -c "which mingw32-make"

Write-Host "GCC version:"
& "C:\msys64\ucrt64.exe" -c "gcc --version | head -1"

Write-Host "Make version:"
& "C:\msys64\ucrt64.exe" -c "make --version | head -1"

Write-Host "Press any key to continue..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
