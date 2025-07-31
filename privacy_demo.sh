#!/bin/bash

# Privacy and Anti-Tracking Demonstration Script

echo "Anoverif Privacy Protection Demo"
echo "================================="
echo ""

echo "🔐 Key Privacy Features:"
echo "1. Original idval sent to backend (no hash tracking)"
echo "2. Async request mixing with random delays (100ms-2s)"
echo "3. Request order scrambling prevents correlation"
echo "4. Hash used only for internal request tracking"
echo ""

echo "Testing concurrent requests from 'different websites'..."
echo ""

# Simulate requests from different "websites" with different user IDs
declare -a WEBSITES=("website_A" "website_B" "website_C" "website_D" "website_E")
declare -a USERIDS=("user_12345" "premium_user_789" "guest_456" "admin_001" "verified_999")

echo "Sending requests in parallel (simulating different sources):"

for i in {0..4}; do
    website=${WEBSITES[$i]}
    userid=${USERIDS[$i]}
    echo "📱 ${website}: Verifying ${userid}"
    
    # Send request in background with timing
    (
        start_time=$(date +%s)
        response=$(curl -s -X POST http://localhost:8081/verify \
            -H "Content-Type: application/json" \
            -H "X-Source-Website: ${website}" \
            -d "{\"idval\": \"${userid}\"}")
        end_time=$(date +%s)
        duration=$((end_time - start_time))
        
        result=$(echo $response | jq -r '.result')
        timestamp=$(echo $response | jq -r '.timestamp')
        
        echo "✅ ${website}: ${userid} -> ${result} (${duration}s, ts:${timestamp})"
    ) &
done

# Wait for all requests to complete
wait

echo ""
echo "📊 Analysis:"
echo "- Notice the different response times due to mixing delays"
echo "- Backend received original idvals (not hashes)"
echo "- Response order may differ from request order"
echo "- Verifier cannot correlate website to specific requests"

echo ""
echo "🔍 Server Statistics:"
curl -s http://localhost:9090/stats | jq .
