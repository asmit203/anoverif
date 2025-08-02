#!/usr/bin/env python3

import csv
import json
import math
from pathlib import Path
import argparse
import sys
from collections import defaultdict


def load_timing_data(csv_file):
    """Load timing data from CSV file"""
    data = []
    try:
        with open(csv_file, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                data.append(
                    {
                        "request_id": int(row["request_id"]),
                        "response_time_ms": float(row["response_time_ms"]),
                        "success": row["success"].lower() == "true",
                        "error": row.get("error", ""),
                    }
                )
        return data
    except Exception as e:
        print(f"Error loading {csv_file}: {e}")
        return None


def calculate_statistics(response_times):
    """Calculate comprehensive statistics"""
    if not response_times:
        return {}

    n = len(response_times)
    sorted_times = sorted(response_times)

    # Basic statistics
    mean = sum(response_times) / n
    median = (
        sorted_times[n // 2]
        if n % 2 == 1
        else (sorted_times[n // 2 - 1] + sorted_times[n // 2]) / 2
    )
    minimum = min(response_times)
    maximum = max(response_times)
    range_val = maximum - minimum

    # Percentiles
    def percentile(data, p):
        index = (len(data) - 1) * p / 100
        lower = int(index)
        upper = lower + 1
        weight = index - lower
        if upper >= len(data):
            return data[-1]
        return data[lower] * (1 - weight) + data[upper] * weight

    p25 = percentile(sorted_times, 25)
    p75 = percentile(sorted_times, 75)
    p95 = percentile(sorted_times, 95)
    p99 = percentile(sorted_times, 99)

    # Standard deviation
    variance = sum((x - mean) ** 2 for x in response_times) / n
    std_dev = math.sqrt(variance)

    # Coefficient of variation
    cv = std_dev / mean if mean != 0 else 0

    # Sequential variations
    variations = []
    for i in range(1, len(response_times)):
        variations.append(abs(response_times[i] - response_times[i - 1]))

    avg_variation = sum(variations) / len(variations) if variations else 0
    max_variation = max(variations) if variations else 0

    return {
        "n": n,
        "mean": mean,
        "median": median,
        "std_dev": std_dev,
        "min": minimum,
        "max": maximum,
        "range": range_val,
        "p25": p25,
        "p75": p75,
        "p95": p95,
        "p99": p99,
        "cv": cv,
        "avg_variation": avg_variation,
        "max_variation": max_variation,
        "iqr": p75 - p25,
    }


def detect_outliers(response_times, method="zscore", threshold=2):
    """Detect outliers using z-score or IQR method"""
    if not response_times:
        return []

    if method == "zscore":
        mean = sum(response_times) / len(response_times)
        std_dev = math.sqrt(
            sum((x - mean) ** 2 for x in response_times) / len(response_times)
        )
        outliers = []
        for i, time in enumerate(response_times):
            z_score = abs(time - mean) / std_dev if std_dev != 0 else 0
            if z_score > threshold:
                outliers.append((i, time, z_score))
        return outliers

    elif method == "iqr":
        sorted_times = sorted(response_times)
        n = len(sorted_times)
        q1 = sorted_times[n // 4]
        q3 = sorted_times[3 * n // 4]
        iqr = q3 - q1
        lower_bound = q1 - 1.5 * iqr
        upper_bound = q3 + 1.5 * iqr

        outliers = []
        for i, time in enumerate(response_times):
            if time < lower_bound or time > upper_bound:
                outliers.append((i, time, 0))  # No z-score for IQR method
        return outliers


def analyze_patterns(data):
    """Analyze timing patterns and trends"""
    if len(data) < 3:
        return {}

    response_times = [d["response_time_ms"] for d in data if d["success"]]
    request_ids = [d["request_id"] for d in data if d["success"]]

    if len(response_times) < 3:
        return {}

    # Trend analysis (simple linear regression)
    n = len(response_times)
    sum_x = sum(request_ids)
    sum_y = sum(response_times)
    sum_xy = sum(x * y for x, y in zip(request_ids, response_times))
    sum_x2 = sum(x * x for x in request_ids)

    # Slope calculation
    denominator = n * sum_x2 - sum_x * sum_x
    if denominator != 0:
        slope = (n * sum_xy - sum_x * sum_y) / denominator
        intercept = (sum_y - slope * sum_x) / n
    else:
        slope = 0
        intercept = sum_y / n

    # Pattern classification
    avg_time = sum(response_times) / len(response_times)
    std_dev = math.sqrt(
        sum((x - avg_time) ** 2 for x in response_times) / len(response_times)
    )

    patterns = []
    if abs(slope) > 5:  # More than 5ms change per request
        if slope > 0:
            patterns.append("Increasing trend detected (performance degrading)")
        else:
            patterns.append("Decreasing trend detected (performance improving)")

    if std_dev / avg_time > 0.5:
        patterns.append("High variability in response times")
    elif std_dev / avg_time < 0.1:
        patterns.append("Very consistent response times")

    # Performance bands
    within_1std = sum(1 for t in response_times if abs(t - avg_time) <= std_dev)
    within_2std = sum(1 for t in response_times if abs(t - avg_time) <= 2 * std_dev)

    return {
        "slope": slope,
        "intercept": intercept,
        "trend_description": f"{slope:+.2f} ms per request",
        "patterns": patterns,
        "within_1std_pct": (within_1std / len(response_times)) * 100,
        "within_2std_pct": (within_2std / len(response_times)) * 100,
    }


def categorize_performance(cv, avg_time):
    """Categorize performance based on coefficient of variation and average time"""
    if cv < 0.1:
        consistency = "Excellent"
        consistency_desc = "Very low variation, highly predictable"
    elif cv < 0.25:
        consistency = "Good"
        consistency_desc = "Low variation, reasonably predictable"
    elif cv < 0.5:
        consistency = "Fair"
        consistency_desc = "Moderate variation, somewhat unpredictable"
    else:
        consistency = "Poor"
        consistency_desc = "High variation, unpredictable performance"

    if avg_time < 500:
        speed = "Fast"
    elif avg_time < 1000:
        speed = "Moderate"
    elif avg_time < 2000:
        speed = "Slow"
    else:
        speed = "Very Slow"

    return consistency, consistency_desc, speed


def generate_comprehensive_report(data, output_file=None):
    """Generate a comprehensive performance analysis report"""

    # Basic metrics
    total_requests = len(data)
    successful_requests = [d for d in data if d["success"]]
    failed_requests = [d for d in data if not d["success"]]

    success_rate = (
        len(successful_requests) / total_requests * 100 if total_requests > 0 else 0
    )

    # Response time analysis for successful requests
    response_times = [d["response_time_ms"] for d in successful_requests]

    if not response_times:
        print("❌ No successful requests to analyze")
        return

    stats = calculate_statistics(response_times)
    outliers_zscore = detect_outliers(response_times, "zscore", 2)
    outliers_iqr = detect_outliers(response_times, "iqr")
    patterns = analyze_patterns(successful_requests)

    consistency, consistency_desc, speed = categorize_performance(
        stats["cv"], stats["mean"]
    )

    # Error analysis
    error_counts = defaultdict(int)
    for req in failed_requests:
        error_counts[req["error"]] += 1

    # Generate report
    report_lines = []

    def add_line(line="", indent=0):
        report_lines.append("  " * indent + line)

    add_line("🎯 COMPREHENSIVE ANOVERIF PERFORMANCE ANALYSIS")
    add_line("=" * 70)
    add_line()

    # Executive Summary
    add_line("📈 EXECUTIVE SUMMARY")
    add_line("-" * 30)
    add_line(f"Overall Performance Rating: {consistency} ({speed})")
    add_line(
        f"Success Rate: {success_rate:.1f}% ({len(successful_requests)}/{total_requests} requests)"
    )
    add_line(f"Average Response Time: {stats['mean']:.1f} ms")
    add_line(f"Performance Consistency: {consistency_desc}")
    add_line()

    # Key Metrics
    add_line("📊 KEY PERFORMANCE METRICS")
    add_line("-" * 35)
    add_line(f"Total Requests Analyzed: {total_requests}")
    add_line(f"Successful Responses: {len(successful_requests)}")
    add_line(f"Failed Responses: {len(failed_requests)}")
    add_line(f"Success Rate: {success_rate:.2f}%")
    add_line()

    # Response Time Statistics
    add_line("⏱️  RESPONSE TIME ANALYSIS")
    add_line("-" * 35)
    add_line(f"Mean Response Time: {stats['mean']:.2f} ms")
    add_line(f"Median Response Time: {stats['median']:.2f} ms")
    add_line(f"Standard Deviation: {stats['std_dev']:.2f} ms")
    add_line(f"Minimum Time: {stats['min']:.2f} ms")
    add_line(f"Maximum Time: {stats['max']:.2f} ms")
    add_line(f"Range: {stats['range']:.2f} ms")
    add_line()
    add_line("Percentile Analysis:")
    add_line(f"25th Percentile (Q1): {stats['p25']:.2f} ms", 1)
    add_line(f"75th Percentile (Q3): {stats['p75']:.2f} ms", 1)
    add_line(f"95th Percentile: {stats['p95']:.2f} ms", 1)
    add_line(f"99th Percentile: {stats['p99']:.2f} ms", 1)
    add_line(f"Interquartile Range (IQR): {stats['iqr']:.2f} ms", 1)
    add_line()

    # Variability Analysis
    add_line("📈 VARIABILITY ANALYSIS")
    add_line("-" * 30)
    add_line(f"Coefficient of Variation: {stats['cv']:.3f}")
    add_line(f"Consistency Rating: {consistency}")
    add_line(f"Average Sequential Variation: {stats['avg_variation']:.2f} ms")
    add_line(f"Maximum Sequential Variation: {stats['max_variation']:.2f} ms")
    add_line()

    # Outlier Detection
    add_line("🔍 OUTLIER ANALYSIS")
    add_line("-" * 25)
    add_line(f"Z-Score Outliers (>2σ): {len(outliers_zscore)}")
    if outliers_zscore:
        add_line("Outlier Details:", 1)
        for idx, time, zscore in outliers_zscore[:5]:  # Show first 5
            add_line(f"Request {idx}: {time:.1f} ms (z-score: {zscore:.2f})", 2)
        if len(outliers_zscore) > 5:
            add_line(f"... and {len(outliers_zscore) - 5} more", 2)

    add_line(f"IQR-Based Outliers: {len(outliers_iqr)}")
    add_line()

    # Pattern Analysis
    if patterns:
        add_line("🎯 PERFORMANCE PATTERNS")
        add_line("-" * 30)
        add_line(f"Trend: {patterns['trend_description']}")
        add_line(f"Requests within 1σ: {patterns['within_1std_pct']:.1f}%")
        add_line(f"Requests within 2σ: {patterns['within_2std_pct']:.1f}%")
        add_line()
        if patterns["patterns"]:
            add_line("Detected Patterns:")
            for pattern in patterns["patterns"]:
                add_line(f"• {pattern}", 1)
        add_line()

    # Performance Recommendations
    add_line("💡 PERFORMANCE INSIGHTS & RECOMMENDATIONS")
    add_line("-" * 50)

    if stats["cv"] < 0.1:
        add_line("✅ Excellent consistency - server performance is very predictable")
    elif stats["cv"] < 0.25:
        add_line("✅ Good consistency - server performance is reasonably stable")
    elif stats["cv"] < 0.5:
        add_line("⚠️ Moderate variability - consider investigating load patterns")
    else:
        add_line(
            "❌ High variability - server may be under stress or experiencing issues"
        )

    if stats["mean"] > 2000:
        add_line("⚠️ High average response time - consider performance optimization")
    elif stats["mean"] < 500:
        add_line("✅ Fast response times - good server performance")

    if len(outliers_zscore) > len(response_times) * 0.05:  # More than 5% outliers
        add_line(
            "⚠️ Many outliers detected - investigate server load or resource constraints"
        )

    if patterns and abs(patterns["slope"]) > 10:
        if patterns["slope"] > 0:
            add_line(
                "❌ Performance degrading over time - investigate resource leaks or increasing load"
            )
        else:
            add_line(
                "✅ Performance improving over time - server warming up or optimization taking effect"
            )

    # Error Analysis
    if failed_requests:
        add_line()
        add_line("❌ ERROR ANALYSIS")
        add_line("-" * 20)
        add_line(f"Total Failed Requests: {len(failed_requests)}")
        add_line(f"Failure Rate: {len(failed_requests)/total_requests*100:.2f}%")
        add_line()
        add_line("Error Breakdown:")
        for error, count in sorted(
            error_counts.items(), key=lambda x: x[1], reverse=True
        ):
            add_line(f"• {error}: {count} occurrences", 1)
        add_line()

    # System Recommendations
    add_line("🔧 SYSTEM TUNING RECOMMENDATIONS")
    add_line("-" * 40)

    if stats["cv"] > 0.3:
        add_line("• Consider implementing request queuing or load balancing")
        add_line("• Monitor server resources (CPU, memory, disk I/O)")
        add_line("• Review async request processing configuration")

    if stats["mean"] > 1000:
        add_line("• Consider optimizing backend API calls")
        add_line("• Review database query performance")
        add_line("• Implement caching strategies")

    if len(failed_requests) > 0:
        add_line("• Investigate causes of request failures")
        add_line("• Implement retry mechanisms with exponential backoff")
        add_line("• Review server timeout configurations")

    add_line()
    add_line("📋 TESTING SUMMARY")
    add_line("-" * 25)
    add_line(f"Test completed with {total_requests} requests")
    add_line(f"Success rate: {success_rate:.1f}%")
    add_line(f"Average response time: {stats['mean']:.1f} ms")
    add_line(f"Performance rating: {consistency}")
    add_line()
    add_line("Report generated by Anoverif Advanced Benchmark Suite")
    add_line("=" * 70)

    # Print or save report
    report_text = "\\n".join(report_lines)

    if output_file:
        try:
            with open(output_file, "w") as f:
                f.write(report_text)
            print(f"📄 Comprehensive report saved to {output_file}")
        except Exception as e:
            print(f"Error saving report: {e}")

    print(report_text)

    # Generate JSON summary for programmatic access
    summary = {
        "total_requests": total_requests,
        "successful_requests": len(successful_requests),
        "failed_requests": len(failed_requests),
        "success_rate": success_rate,
        "statistics": stats,
        "performance_rating": {
            "consistency": consistency,
            "speed": speed,
            "description": consistency_desc,
        },
        "outliers": {
            "zscore_count": len(outliers_zscore),
            "iqr_count": len(outliers_iqr),
        },
        "errors": dict(error_counts),
        "patterns": patterns,
    }

    json_file = (
        output_file.replace(".txt", ".json") if output_file else "analysis_summary.json"
    )
    try:
        with open(json_file, "w") as f:
            json.dump(summary, f, indent=2)
        print(f"📊 JSON summary saved to {json_file}")
    except Exception as e:
        print(f"Warning: Could not save JSON summary: {e}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate comprehensive timing analysis report"
    )
    parser.add_argument("csv_file", help="CSV file with timing results")
    parser.add_argument("--output", "-o", help="Output file for the report")

    args = parser.parse_args()

    # Load data
    data = load_timing_data(args.csv_file)
    if data is None:
        sys.exit(1)

    print(f"📁 Loaded {len(data)} records from {args.csv_file}")
    print()

    # Generate report
    generate_comprehensive_report(data, args.output)


if __name__ == "__main__":
    main()
