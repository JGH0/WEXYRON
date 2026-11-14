#!/bin/bash
# Build and start the dedicated server.
#
# The server reads server_config.json from this directory. Server-side bots are
# configured there:
#
#   "bots": { "count": 4, "accuracy": 0.7 }
#
# count = how many bots to spawn (0 disables them), accuracy = 0..1 hit chance.
# Command line/environment can override it: BOTS_COUNT / BOTS_ACCURACY, or
# ./runServer.sh [port] [name] [maxPerMachine] [botCount] [botAccuracy]
set -e
cd "$(dirname "$0")"
make && ./build/bin/server "$@"
