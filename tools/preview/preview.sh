#!/bin/sh
# Renders the pet's moods on the host into tools/preview/pets.png.
set -e
cd "$(dirname "$0")"
CXX=${CXX:-/Library/Developer/CommandLineTools/usr/bin/clang++}
SDK=$(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk 2>/dev/null || true)
$CXX -std=c++17 ${SDK:+-isysroot "$SDK"} -Ishim -I../../firmware/src \
  preview.cpp ../../firmware/src/pet.cpp -o /tmp/claude-pet-preview
/tmp/claude-pet-preview
sips -s format png pets.ppm --out pets.png >/dev/null && rm pets.ppm
echo "wrote $(pwd)/pets.png"
