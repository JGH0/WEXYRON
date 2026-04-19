# Windows Build Troubleshooting

## "mingw32-make not found" Error

### Quick Fix Options

#### Option 1: Use build_simple.bat
Run `build_simple.bat` instead of `build.bat`. This script will:
- Automatically search for MinGW installations
- Try multiple make commands
- Provide detailed error messages

#### Option 2: Install MinGW-w64 Properly

**Step 1: Install MSYS2**
1. Download from [https://www.msys2.org/](https://www.msys2.org/)
2. Run the installer
3. Keep default installation location (usually `C:\msys64`)

**Step 2: Install MinGW-w64**
1. Open "MSYS2 MINGW64" (not regular MSYS2)
2. Run these commands:
   ```bash
   pacman -Syu
   pacman -S mingw-w64-x86_64-toolchain
   pacman -S mingw-w64-x86_64-glfw
   pacman -S mingw-w64-x86_64-glm
   ```

**Step 3: Add to Windows PATH**
1. Press Windows key, type "Environment Variables"
2. Click "Edit the system environment variables"
3. Click "Environment Variables..."
4. Under "System variables", find "Path"
5. Click "Edit...", then "New"
6. Add: `C:\msys64\mingw64\bin`
7. Click OK on all windows
8. **Restart Command Prompt/PowerShell**

**Step 4: Verify Installation**
Open new Command Prompt and run:
```cmd
mingw32-make --version
gcc --version
```

### Alternative: Use Visual Studio

If MinGW setup is problematic, you can use Visual Studio:

1. Install Visual Studio Community (free)
2. Install "Desktop development with C++"
3. Use the provided CMakeLists.txt:
   ```cmd
   mkdir build
   cd build
   cmake ..
   cmake --build .
   ```

### Common Installation Paths

If MinGW is installed but not in PATH, check these locations:
- `C:\msys64\mingw64\bin\mingw32-make.exe`
- `C:\msys64\ucrt64\bin\mingw32-make.exe`
- `C:\mingw64\bin\mingw32-make.exe`

### Testing the Build

After setup, try building:
```cmd
# Option 1: Use the simple build script
build_simple.bat

# Option 2: Use standard build script
build.bat

# Option 3: Manual build
mingw32-make -f Makefile.windows
```

### If Build Still Fails

Check for these common issues:

**Missing Libraries:**
```bash
# In MSYS2 MINGW64 terminal
pacman -S mingw-w64-x86_64-glfw
pacman -S mingw-w64-x86_64-glm
```

**Wrong Architecture:**
- Ensure you're using MINGW64 (not MINGW32)
- Check that you're building for 64-bit

**Path Issues:**
- Restart Command Prompt after adding to PATH
- Use `where mingw32-make` to verify it's found
- Try full path: `C:\msys64\mingw64\bin\mingw32-make.exe`

**Permission Issues:**
- Run Command Prompt as Administrator
- Check antivirus isn't blocking the tools

### Getting Help

If you're still stuck:
1. Run `build_simple.bat` and copy the full output
2. Check the [WINDOWS_SETUP.md](WINDOWS_SETUP.md) guide
3. Verify MSYS2 is properly installed
4. Make sure you're using the MINGW64 terminal, not regular MSYS2

The most reliable approach is using `build_simple.bat` as it handles most common setup issues automatically.
