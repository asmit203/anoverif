#!/bin/bash

# Start script for the complete Anoverif system

set -e

echo "Starting Anoverif System"

# Check if executables exist
if [ ! -f "build/dummy_api" ] || [ ! -f "build/anon_server" ]; then
    echo "Error: Executables not found. Please run ./build.sh first"
    exit 1
fi

# Function to cleanup background processes
cleanup() {
    echo "Shutting down services..."
    if [ ! -z "$DUMMY_PID" ]; then
        kill $DUMMY_PID 2>/dev/null || true
    fi
    if [ ! -z "$ANON_PID" ]; then
        kill $ANON_PID 2>/dev/null || true
    fi
    exit 0
}

# Set trap to cleanup on exit
trap cleanup SIGINT SIGTERM

# Start dummy API server
echo "Starting dummy API server on port 9090..."
./build/dummy_api --port 9090 &
DUMMY_PID=$!
sleep 2

# Start anonymization server
echo "Starting anonymization server on port 8080..."
./build/anon_server --port 8080 --backend http://localhost:9090/verify &
ANON_PID=$!
sleep 2

echo ""
echo "System is now running!"
echo ""
echo "Services:"
echo "  - Dummy API Server: http://localhost:9090"
echo "  - Anonymization Server: http://localhost:8080"
echo ""
echo "Test endpoints:"
echo "  - Health check: curl http://localhost:9090/health"
echo "  - Statistics: curl http://localhost:9090/stats"
echo "  - Verify request: curl -X POST http://localhost:8080/verify -H 'Content-Type: application/json' -d '{\"idval\":\"test123\"}'"
echo ""
echo "Or run the test client:"
echo "  ./build/test_client"
echo "  ./build/test_client --load --requests 1000 --concurrency 50"
echo ""
echo "Press Ctrl+C to stop all services"

# Wait for background processes
wait
