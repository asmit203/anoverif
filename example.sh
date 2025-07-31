#!/bin/bash

# Example usage of Anoverif system

echo "Anoverif Example Usage"
echo "====================="

# Check if servers are running
if ! curl -f http://localhost:8080 >/dev/null 2>&1; then
    echo "Starting Anoverif system..."
    ./start.sh &
    sleep 5
fi

echo "1. Basic verification request:"
echo "------------------------------"

curl -X POST http://localhost:8080/verify \
  -H "Content-Type: application/json" \
  -d '{"idval": "user123"}' \
  -w "\nResponse Time: %{time_total}s\n"

echo ""
echo "2. Another verification with different data:"
echo "--------------------------------------------"

curl -X POST http://localhost:8080/verify \
  -H "Content-Type: application/json" \
  -d '{"idval": "sensitive_email@example.com"}' \
  -w "\nResponse Time: %{time_total}s\n"

echo ""
echo "3. Error case - missing parameter:"
echo "----------------------------------"

curl -X POST http://localhost:8080/verify \
  -H "Content-Type: application/json" \
  -d '{"wrong_param": "test"}' \
  -w "\nResponse Time: %{time_total}s\n"

echo ""
echo "4. Backend API statistics:"
echo "-------------------------"

curl http://localhost:9090/stats

echo ""
echo ""
echo "5. Backend API health check:"
echo "---------------------------"

curl http://localhost:9090/health

echo ""
echo ""
echo "Example completed!"
echo ""
echo "Key Points:"
echo "- Original 'idval' is never stored or logged"
echo "- Each request is hashed with salt before forwarding"
echo "- Backend only sees the hash, not the original data"
echo "- Response contains only true/false verification result"
