#!/usr/bin/env python3
"""
Advanced Benchmark Suite for Anoverif
Analyzes timing variations and request performance patterns
"""

import asyncio
import aiohttp
import time
import json
import statistics
import argparse
import sys
import matplotlib.pyplot as plt
import numpy as np
from dataclasses import dataclass
from typing import List, Dict, Any
from concurrent.futures import ThreadPoolExecutor
import threading
import random


@dataclass
class RequestResult:
    """Individual request result with detailed timing"""

    request_id: int
    start_time: float
    end_time: float
    response_time: float
    status_code: int
    success: bool
    result_value: bool
    error: str = None
    queue_time: float = 0.0  # Time in queue before processing
    processing_time: float = 0.0  # Actual processing time


class BenchmarkSuite:
    def __init__(self, base_url: str = "http://localhost:8082/verify"):
        self.base_url = base_url
        self.results: List[RequestResult] = []
        self.session = None

    async def __aenter__(self):
        self.session = aiohttp.ClientSession()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            await self.session.close()

    async def send_request(self, request_id: int, idval: str) -> RequestResult:
        """Send individual request with detailed timing"""
        start_time = time.time()

        try:
            payload = {"idval": idval}
            async with self.session.post(
                self.base_url,
                json=payload,
                headers={"Content-Type": "application/json"},
                timeout=aiohttp.ClientTimeout(total=10),
            ) as response:
                end_time = time.time()
                response_time = (end_time - start_time) * 1000  # Convert to ms

                response_data = await response.json()

                return RequestResult(
                    request_id=request_id,
                    start_time=start_time,
                    end_time=end_time,
                    response_time=response_time,
                    status_code=response.status,
                    success=response_data.get("success", False),
                    result_value=response_data.get("result", False),
                    error=response_data.get("error"),
                )

        except Exception as e:
            end_time = time.time()
            response_time = (end_time - start_time) * 1000

            return RequestResult(
                request_id=request_id,
                start_time=start_time,
                end_time=end_time,
                response_time=response_time,
                status_code=0,
                success=False,
                result_value=False,
                error=str(e),
            )

    async def run_burst_test(
        self, burst_size: int, burst_interval: float
    ) -> List[RequestResult]:
        """Run burst test with specified burst size and interval"""
        print(f"🚀 Running burst test: {burst_size} requests every {burst_interval}s")

        results = []
        burst_count = 0

        for burst in range(5):  # 5 bursts
            burst_start = time.time()
            print(f"  Burst {burst + 1}/5 starting...")

            # Create tasks for this burst
            tasks = []
            for i in range(burst_size):
                request_id = burst * burst_size + i
                idval = f"burst_{burst}_req_{i}_{random.randint(1000, 9999)}"
                tasks.append(self.send_request(request_id, idval))

            # Execute burst
            burst_results = await asyncio.gather(*tasks)
            results.extend(burst_results)

            burst_end = time.time()
            burst_duration = burst_end - burst_start
            print(f"  Burst {burst + 1} completed in {burst_duration:.2f}s")

            # Wait for next burst
            if burst < 4:  # Don't wait after last burst
                await asyncio.sleep(burst_interval)

        return results

    async def run_ramp_test(
        self, start_rps: int, end_rps: int, duration: int
    ) -> List[RequestResult]:
        """Run ramp test with increasing request rate"""
        print(f"📈 Running ramp test: {start_rps} to {end_rps} RPS over {duration}s")

        results = []
        start_time = time.time()
        request_id = 0

        while time.time() - start_time < duration:
            elapsed = time.time() - start_time
            progress = elapsed / duration
            current_rps = start_rps + (end_rps - start_rps) * progress

            # Calculate delay for current RPS
            delay = 1.0 / current_rps if current_rps > 0 else 1.0

            # Send request
            idval = f"ramp_req_{request_id}_{random.randint(1000, 9999)}"
            result = await self.send_request(request_id, idval)
            results.append(result)

            request_id += 1

            # Progress indicator
            if request_id % 10 == 0:
                print(
                    f"  Progress: {progress*100:.1f}% - Current RPS: {current_rps:.1f}"
                )

            await asyncio.sleep(delay)

        return results

    async def run_sustained_test(self, rps: int, duration: int) -> List[RequestResult]:
        """Run sustained load test"""
        print(f"⚡ Running sustained test: {rps} RPS for {duration}s")

        results = []
        start_time = time.time()
        request_id = 0
        delay = 1.0 / rps

        while time.time() - start_time < duration:
            idval = f"sustained_req_{request_id}_{random.randint(1000, 9999)}"
            result = await self.send_request(request_id, idval)
            results.append(result)

            request_id += 1

            if request_id % 20 == 0:
                elapsed = time.time() - start_time
                print(f"  Progress: {elapsed:.1f}s - Requests sent: {request_id}")

            await asyncio.sleep(delay)

        return results

    async def run_concurrent_test(
        self, concurrency: int, total_requests: int
    ) -> List[RequestResult]:
        """Run concurrent test with specified concurrency level"""
        print(
            f"🔀 Running concurrent test: {concurrency} concurrent, {total_requests} total"
        )

        semaphore = asyncio.Semaphore(concurrency)

        async def limited_request(request_id: int, idval: str):
            async with semaphore:
                return await self.send_request(request_id, idval)

        tasks = []
        for i in range(total_requests):
            idval = f"concurrent_req_{i}_{random.randint(1000, 9999)}"
            tasks.append(limited_request(i, idval))

        return await asyncio.gather(*tasks)


def analyze_results(results: List[RequestResult]) -> Dict[str, Any]:
    """Analyze benchmark results and generate statistics"""
    if not results:
        return {}

    # Filter successful requests
    successful = [r for r in results if r.success]
    failed = [r for r in results if not r.success]

    # Response times
    response_times = [r.response_time for r in successful]

    if not response_times:
        return {"error": "No successful requests"}

    # Basic statistics
    stats = {
        "total_requests": len(results),
        "successful_requests": len(successful),
        "failed_requests": len(failed),
        "success_rate": len(successful) / len(results) * 100,
        "response_times": {
            "min": min(response_times),
            "max": max(response_times),
            "mean": statistics.mean(response_times),
            "median": statistics.median(response_times),
            "p95": np.percentile(response_times, 95),
            "p99": np.percentile(response_times, 99),
            "std_dev": (
                statistics.stdev(response_times) if len(response_times) > 1 else 0
            ),
        },
        "timing_analysis": analyze_timing_patterns(successful),
        "error_analysis": analyze_errors(failed),
    }

    return stats


def analyze_timing_patterns(results: List[RequestResult]) -> Dict[str, Any]:
    """Analyze timing patterns and variations"""
    if len(results) < 2:
        return {}

    times = [r.response_time for r in results]
    timestamps = [r.start_time for r in results]

    # Time series analysis
    time_series = list(zip(timestamps, times))
    time_series.sort(key=lambda x: x[0])

    # Calculate variations
    variations = []
    for i in range(1, len(time_series)):
        variation = abs(time_series[i][1] - time_series[i - 1][1])
        variations.append(variation)

    # Detect patterns
    patterns = {
        "max_variation": max(variations) if variations else 0,
        "avg_variation": statistics.mean(variations) if variations else 0,
        "timing_consistency": (
            1 - (statistics.stdev(times) / statistics.mean(times))
            if statistics.mean(times) > 0
            else 0
        ),
        "outliers": detect_outliers(times),
        "trending": analyze_trend(times),
    }

    return patterns


def detect_outliers(data: List[float], threshold: float = 2.0) -> Dict[str, Any]:
    """Detect outliers using z-score method"""
    if len(data) < 3:
        return {"count": 0, "percentage": 0}

    mean = statistics.mean(data)
    std_dev = statistics.stdev(data)

    outliers = []
    for i, value in enumerate(data):
        z_score = abs(value - mean) / std_dev if std_dev > 0 else 0
        if z_score > threshold:
            outliers.append({"index": i, "value": value, "z_score": z_score})

    return {
        "count": len(outliers),
        "percentage": len(outliers) / len(data) * 100,
        "outliers": outliers[:10],  # Top 10 outliers
    }


def analyze_trend(data: List[float]) -> str:
    """Analyze if there's a trending pattern in response times"""
    if len(data) < 10:
        return "insufficient_data"

    # Simple linear regression to detect trend
    x = list(range(len(data)))
    y = data

    n = len(data)
    sum_x = sum(x)
    sum_y = sum(y)
    sum_xy = sum(xi * yi for xi, yi in zip(x, y))
    sum_x2 = sum(xi * xi for xi in x)

    slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x)

    if slope > 0.1:
        return "increasing"
    elif slope < -0.1:
        return "decreasing"
    else:
        return "stable"


def analyze_errors(failed_results: List[RequestResult]) -> Dict[str, Any]:
    """Analyze error patterns"""
    if not failed_results:
        return {"total": 0, "types": {}}

    error_types = {}
    for result in failed_results:
        error = result.error or "unknown"
        error_types[error] = error_types.get(error, 0) + 1

    return {
        "total": len(failed_results),
        "types": error_types,
        "most_common": (
            max(error_types.items(), key=lambda x: x[1]) if error_types else None
        ),
    }


def generate_report(test_name: str, stats: Dict[str, Any]) -> str:
    """Generate detailed report"""
    if "error" in stats:
        return f"❌ {test_name}: {stats['error']}"

    rt = stats["response_times"]
    ta = stats["timing_analysis"]

    report = f"""
🎯 {test_name} Results:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

📊 Basic Metrics:
  • Total Requests: {stats['total_requests']}
  • Success Rate: {stats['success_rate']:.1f}%
  • Failed Requests: {stats['failed_requests']}

⏱️ Response Time Analysis:
  • Min: {rt['min']:.2f}ms
  • Max: {rt['max']:.2f}ms  
  • Mean: {rt['mean']:.2f}ms
  • Median: {rt['median']:.2f}ms
  • 95th Percentile: {rt['p95']:.2f}ms
  • 99th Percentile: {rt['p99']:.2f}ms
  • Std Deviation: {rt['std_dev']:.2f}ms

📈 Timing Patterns:
  • Max Variation: {ta.get('max_variation', 0):.2f}ms
  • Avg Variation: {ta.get('avg_variation', 0):.2f}ms
  • Consistency Score: {ta.get('timing_consistency', 0):.3f}
  • Trend: {ta.get('trending', 'unknown')}
  • Outliers: {ta.get('outliers', {}).get('count', 0)} ({ta.get('outliers', {}).get('percentage', 0):.1f}%)
"""

    if stats.get("error_analysis", {}).get("total", 0) > 0:
        ea = stats["error_analysis"]
        report += f"""
❌ Error Analysis:
  • Total Errors: {ea['total']}
  • Most Common: {ea.get('most_common', ['unknown', 0])[0]} ({ea.get('most_common', ['unknown', 0])[1]} times)
"""

    return report


async def main():
    parser = argparse.ArgumentParser(description="Advanced Anoverif Benchmark Suite")
    parser.add_argument(
        "--url", default="http://localhost:8082/verify", help="Server URL"
    )
    parser.add_argument("--all", action="store_true", help="Run all benchmark tests")
    parser.add_argument("--burst", action="store_true", help="Run burst test")
    parser.add_argument("--ramp", action="store_true", help="Run ramp test")
    parser.add_argument("--sustained", action="store_true", help="Run sustained test")
    parser.add_argument("--concurrent", action="store_true", help="Run concurrent test")
    parser.add_argument("--output", help="Output file for detailed results")

    args = parser.parse_args()

    if not any([args.all, args.burst, args.ramp, args.sustained, args.concurrent]):
        args.all = True

    print("🚀 Advanced Anoverif Benchmark Suite")
    print("=" * 50)

    async with BenchmarkSuite(args.url) as benchmark:
        all_results = {}

        if args.all or args.burst:
            print("\n" + "=" * 50)
            results = await benchmark.run_burst_test(burst_size=20, burst_interval=2.0)
            stats = analyze_results(results)
            all_results["burst_test"] = {"results": results, "stats": stats}
            print(generate_report("Burst Test", stats))

        if args.all or args.ramp:
            print("\n" + "=" * 50)
            results = await benchmark.run_ramp_test(
                start_rps=5, end_rps=25, duration=30
            )
            stats = analyze_results(results)
            all_results["ramp_test"] = {"results": results, "stats": stats}
            print(generate_report("Ramp Test", stats))

        if args.all or args.sustained:
            print("\n" + "=" * 50)
            results = await benchmark.run_sustained_test(rps=15, duration=20)
            stats = analyze_results(results)
            all_results["sustained_test"] = {"results": results, "stats": stats}
            print(generate_report("Sustained Test", stats))

        if args.all or args.concurrent:
            print("\n" + "=" * 50)
            results = await benchmark.run_concurrent_test(
                concurrency=30, total_requests=150
            )
            stats = analyze_results(results)
            all_results["concurrent_test"] = {"results": results, "stats": stats}
            print(generate_report("Concurrent Test", stats))

        # Save detailed results if requested
        if args.output:
            with open(args.output, "w") as f:
                json.dump(all_results, f, indent=2, default=str)
            print(f"\n💾 Detailed results saved to {args.output}")

        print("\n🎉 Benchmark suite completed!")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n⚠️ Benchmark interrupted by user")
    except Exception as e:
        print(f"\n❌ Benchmark failed: {e}")
        sys.exit(1)
