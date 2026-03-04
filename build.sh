#!/bin/bash

# Output file name
OUTPUT="SopenWorldEcho"

# Compile all files into a single binary
echo "Building project..."
g++ -std=c++17 -O2 -pthread SopenWorldEcho.cpp server.cpp client.cpp -o $OUTPUT

# Check result
if [ $? -eq 0 ]; then
echo "[SUCCESS] Project built: ./$OUTPUT"
echo "Starting server: ./$OUTPUT --server -p [ip]:port"
echo "Starting client: ./$OUTPUT --client [ip]:port"
else
echo "[ERROR] Build failed"
exit 1
fi