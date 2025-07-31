#!/bin/bash

# Performance benchmark script for Anoverif

set -e

echo "Anoverif Performance Benchmark"
echo "=============================="

# Check if system is running
if ! curl -f http://localhost:8080 >/dev/null 2>&1; then
    echo "Error: Anoverif server is not running on localhost:8080"
    echo "Please start the server first with: ./start.sh"
    exit 1
fi

if ! curl -f http://localhost:9090/health >/dev/null 2>&1; then
    echo "Error: Dummy API server is not running on localhost:9090"
    echo "Please start the server first with: ./start.sh"
    exit 1
fi

# Check if test client exists
if [ ! -f "build/test_client" ]; then
    echo "Error: Test client not found. Please run ./build.sh first"
    exit 1
fi

echo "System is ready. Starting benchmarks..."
echo ""

# Warm up
echo "Warming up..."
./build/test_client --load --requests 100 --concurrency 5 >/dev/null 2>&1
sleep 2

# Test configurations
declare -a CONFIGS=(
    "100 5"     # Light load
    "500 10"    # Medium load
    "1000 20"   # Heavy load
    "2000 50"   # Stress test
    "5000 100"  # Maximum load
)

echo "Running benchmark tests..."
echo ""

for config in "${CONFIGS[@]}"; do
    requests=$(echo $config | cut -d' ' -f1)
    concurrency=$(echo $config | cut -d' ' -f2)
    
    echo "Test: $requests requests with $concurrency concurrent connections"
    echo "--------------------------------------------------------------"
    
    # Run the test
    ./build/test_client --load --requests $requests --concurrency $concurrency
    
    echo ""
    echo "Waiting 5 seconds before next test..."
    sleep 5
    echo ""
done

# Get server statistics
echo "Final Server Statistics:"
echo "========================"

echo "Dummy API Stats:"
curl -s http://localhost:9090/stats | python3 -m json.tool 2>/dev/null || curl -s http://localhost:9090/stats

echo ""
echo "System Health:"
curl -s http://localhost:9090/health | python3 -m json.tool 2>/dev/null || curl -s http://localhost:9090/health

echo ""
echo ""
echo "Benchmark completed!"
echo ""
echo "Performance Summary:"
echo "- Tested with up to 5000 concurrent requests"
echo "- Maximum concurrency: 100 connections"
echo "- All tests include full end-to-end processing"
echo "- Results show anonymization + backend verification time"
