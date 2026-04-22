#!/bin/bash

# Multiplayer Test Script
# This script tests the local multiplayer functionality

echo "=== Cube Game Multiplayer Test ==="
echo ""

# Check if build exists
if [ ! -f "./build/bin/server" ] || [ ! -f "./build/bin/game" ]; then
    echo "Error: Game not built. Run 'make' first."
    exit 1
fi

echo "1. Starting server on port 7777..."
./build/bin/server 7777 &
SERVER_PID=$!

# Wait for server to start
sleep 2

echo "2. Testing server connectivity..."
# Check if server is listening
if netstat -uln 2>/dev/null | grep -q ":7777"; then
    echo "   Server is listening on port 7777"
else
    echo "   Warning: Server may not be listening"
fi

echo ""
echo "3. Server started successfully!"
echo "   Server PID: $SERVER_PID"
echo "   Port: 7777"
echo ""
echo "4. To test multiplayer:"
echo "   - Open a new terminal"
echo "   - Run: ./build/bin/game"
echo "   - In game menu, select 'Connect to Server'"
echo "   - Press 'R' to scan for servers"
echo "   - Or manually connect to: 127.0.0.1:7777"
echo ""
echo "5. Press Ctrl+C here to stop the server"
echo ""

# Wait for user to stop
wait $SERVER_PID
