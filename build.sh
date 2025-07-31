#!/bin/bash

# Build script for Anoverif

set -e

echo "Building Anoverif - Anonymous Verification System"

# Check if required dependencies are installed
echo "Checking dependencies..."

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake is required but not installed"
    echo "Install with: brew install cmake"
    exit 1
fi

# Check for pkg-config
if ! command -v pkg-config &> /dev/null; then
    echo "Error: pkg-config is required but not installed"
    echo "Install with: brew install pkg-config"
    exit 1
fi

# Check for libmicrohttpd
echo "Checking for libmicrohttpd..."
if ! pkg-config --exists libmicrohttpd; then
    echo "Error: libmicrohttpd is required but not installed"
    echo "Install with: brew install libmicrohttpd"
    exit 1
else
    echo "  libmicrohttpd found: $(pkg-config --modversion libmicrohttpd)"
fi

# Check for jsoncpp
echo "Checking for jsoncpp..."
if ! pkg-config --exists jsoncpp; then
    echo "Error: jsoncpp is required but not installed"
    echo "Install with: brew install jsoncpp"
    exit 1
else
    echo "  jsoncpp found: $(pkg-config --modversion jsoncpp)"
fi

# Check for OpenSSL
if ! brew list openssl &> /dev/null; then
    echo "Error: OpenSSL is required but not installed"
    echo "Install with: brew install openssl"
    exit 1
fi

# Check for curl
if ! command -v curl-config &> /dev/null; then
    echo "Error: curl development libraries are required but not installed"
    echo "Install with: brew install curl"
    exit 1
fi

echo "All dependencies found!"

# Create build directory
mkdir -p build
cd build

# Configure with cmake
echo "Configuring build..."
cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ..

# Build
echo "Building..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

echo "Build completed successfully!"
echo ""
echo "Executables created:"
echo "  - anon_server    (Main anonymization server)"
echo "  - dummy_api      (Test API server)"
echo "  - test_client    (Test client)"
echo ""
echo "To run:"
echo "  1. Start dummy API: ./dummy_api"
echo "  2. Start anon server: ./anon_server"
echo "  3. Test with client: ./test_client"
