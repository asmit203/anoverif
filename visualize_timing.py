#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns
from pathlib import Path
import argparse
import sys


def load_results(csv_file):
    """Load timing results from CSV file"""
    try:
        df = pd.read_csv(csv_file)
        return df
    except Exception as e:
        print(f"Error loading {csv_file}: {e}")
        return None


def create_timing_visualizations(df, output_prefix="timing_analysis"):
    """Create comprehensive timing visualizations"""

    # Filter successful requests
    successful_df = df[df["success"] == True].copy()

    if len(successful_df) == 0:
        print("No successful requests to visualize")
        return

    # Set up the plotting style
    plt.style.use("seaborn-v0_8")
    sns.set_palette("husl")

    # Create a figure with multiple subplots
    fig = plt.figure(figsize=(16, 12))

    # 1. Response Time Over Request Sequence
    plt.subplot(2, 3, 1)
    plt.plot(
        successful_df["request_id"],
        successful_df["response_time_ms"],
        marker="o",
        linewidth=2,
        markersize=4,
        alpha=0.7,
    )
    plt.title("Response Time by Request Sequence", fontweight="bold")
    plt.xlabel("Request ID")
    plt.ylabel("Response Time (ms)")
    plt.grid(True, alpha=0.3)

    # Add trend line
    z = np.polyfit(successful_df["request_id"], successful_df["response_time_ms"], 1)
    p = np.poly1d(z)
    plt.plot(
        successful_df["request_id"],
        p(successful_df["request_id"]),
        "r--",
        alpha=0.8,
        label=f"Trend: {z[0]:.2f}ms per request",
    )
    plt.legend()

    # 2. Response Time Distribution
    plt.subplot(2, 3, 2)
    plt.hist(
        successful_df["response_time_ms"],
        bins=15,
        alpha=0.7,
        color="skyblue",
        edgecolor="black",
        density=True,
    )
    plt.axvline(
        successful_df["response_time_ms"].mean(),
        color="red",
        linestyle="--",
        label=f'Mean: {successful_df["response_time_ms"].mean():.1f}ms',
    )
    plt.axvline(
        successful_df["response_time_ms"].median(),
        color="orange",
        linestyle="--",
        label=f'Median: {successful_df["response_time_ms"].median():.1f}ms',
    )
    plt.title("Response Time Distribution", fontweight="bold")
    plt.xlabel("Response Time (ms)")
    plt.ylabel("Density")
    plt.legend()
    plt.grid(True, alpha=0.3)

    # 3. Box Plot
    plt.subplot(2, 3, 3)
    box_plot = plt.boxplot(successful_df["response_time_ms"], patch_artist=True)
    box_plot["boxes"][0].set_facecolor("lightblue")
    plt.title("Response Time Box Plot", fontweight="bold")
    plt.ylabel("Response Time (ms)")
    plt.grid(True, alpha=0.3)

    # Add statistical annotations
    stats_text = f"""
    Min: {successful_df['response_time_ms'].min():.1f}ms
    Q1: {successful_df['response_time_ms'].quantile(0.25):.1f}ms
    Median: {successful_df['response_time_ms'].median():.1f}ms
    Q3: {successful_df['response_time_ms'].quantile(0.75):.1f}ms
    Max: {successful_df['response_time_ms'].max():.1f}ms
    """
    plt.text(
        1.1,
        successful_df["response_time_ms"].median(),
        stats_text,
        bbox=dict(boxstyle="round,pad=0.3", facecolor="lightyellow"),
        verticalalignment="center",
    )

    # 4. Sequential Variations
    plt.subplot(2, 3, 4)
    variations = successful_df["response_time_ms"].diff().abs()
    plt.plot(
        successful_df["request_id"][1:],
        variations[1:],
        marker="s",
        linewidth=1,
        markersize=3,
        color="purple",
        alpha=0.7,
    )
    plt.title("Sequential Response Time Variations", fontweight="bold")
    plt.xlabel("Request ID")
    plt.ylabel("Absolute Variation (ms)")
    plt.grid(True, alpha=0.3)

    # Add average variation line
    avg_variation = variations[1:].mean()
    plt.axhline(
        avg_variation,
        color="red",
        linestyle="--",
        label=f"Avg Variation: {avg_variation:.1f}ms",
    )
    plt.legend()

    # 5. Cumulative Response Time
    plt.subplot(2, 3, 5)
    cumulative_time = successful_df["response_time_ms"].cumsum()
    plt.plot(
        successful_df["request_id"],
        cumulative_time,
        marker="",
        linewidth=2,
        color="green",
    )
    plt.title("Cumulative Response Time", fontweight="bold")
    plt.xlabel("Request ID")
    plt.ylabel("Cumulative Time (ms)")
    plt.grid(True, alpha=0.3)

    # 6. Performance Metrics Summary
    plt.subplot(2, 3, 6)
    plt.axis("off")

    # Calculate detailed statistics
    response_times = successful_df["response_time_ms"]
    stats = {
        "Count": len(response_times),
        "Mean": response_times.mean(),
        "Median": response_times.median(),
        "Std Dev": response_times.std(),
        "Min": response_times.min(),
        "Max": response_times.max(),
        "Range": response_times.max() - response_times.min(),
        "P95": response_times.quantile(0.95),
        "P99": response_times.quantile(0.99),
        "CV": response_times.std() / response_times.mean(),
    }

    # Create a table of statistics
    stats_text = "Performance Summary\\n" + "=" * 25 + "\\n"
    for key, value in stats.items():
        if key == "Count":
            stats_text += f"{key}: {value}\\n"
        elif key == "CV":
            stats_text += f"Coeff of Variation: {value:.3f}\\n"
        else:
            stats_text += f"{key}: {value:.1f} ms\\n"

    plt.text(
        0.1,
        0.5,
        stats_text,
        fontsize=12,
        fontfamily="monospace",
        bbox=dict(boxstyle="round,pad=0.5", facecolor="lightcyan"),
        verticalalignment="center",
    )

    plt.tight_layout()
    plt.savefig(f"{output_prefix}_comprehensive.png", dpi=300, bbox_inches="tight")
    print(f"📊 Comprehensive visualization saved as {output_prefix}_comprehensive.png")

    # Create a separate detailed analysis plot
    fig2, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(14, 10))

    # Moving average analysis
    window_size = min(5, len(successful_df) // 2)
    if window_size >= 2:
        moving_avg = (
            successful_df["response_time_ms"].rolling(window=window_size).mean()
        )
        ax1.plot(
            successful_df["request_id"],
            successful_df["response_time_ms"],
            "o-",
            alpha=0.6,
            label="Raw Data",
        )
        ax1.plot(
            successful_df["request_id"],
            moving_avg,
            linewidth=3,
            label=f"{window_size}-point Moving Average",
        )
        ax1.set_title("Response Time with Moving Average")
        ax1.set_xlabel("Request ID")
        ax1.set_ylabel("Response Time (ms)")
        ax1.legend()
        ax1.grid(True, alpha=0.3)

    # Outlier detection (using Z-score)
    z_scores = np.abs((response_times - response_times.mean()) / response_times.std())
    outliers = successful_df[z_scores > 2]  # Z-score > 2 considered outlier

    ax2.scatter(
        successful_df["request_id"],
        successful_df["response_time_ms"],
        alpha=0.6,
        label="Normal",
    )
    if len(outliers) > 0:
        ax2.scatter(
            outliers["request_id"],
            outliers["response_time_ms"],
            color="red",
            s=100,
            label=f"Outliers ({len(outliers)})",
            alpha=0.8,
        )
    ax2.set_title("Outlier Detection (Z-score > 2)")
    ax2.set_xlabel("Request ID")
    ax2.set_ylabel("Response Time (ms)")
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    # Performance bands
    mean_time = response_times.mean()
    std_time = response_times.std()

    ax3.fill_between(
        successful_df["request_id"],
        mean_time - std_time,
        mean_time + std_time,
        alpha=0.3,
        color="green",
        label="±1 Std Dev",
    )
    ax3.fill_between(
        successful_df["request_id"],
        mean_time - 2 * std_time,
        mean_time + 2 * std_time,
        alpha=0.2,
        color="yellow",
        label="±2 Std Dev",
    )
    ax3.plot(
        successful_df["request_id"],
        successful_df["response_time_ms"],
        "bo-",
        alpha=0.7,
        markersize=4,
    )
    ax3.axhline(mean_time, color="red", linestyle="--", label="Mean")
    ax3.set_title("Performance Bands")
    ax3.set_xlabel("Request ID")
    ax3.set_ylabel("Response Time (ms)")
    ax3.legend()
    ax3.grid(True, alpha=0.3)

    # Consistency analysis
    consistency_score = 1 - (response_times.std() / response_times.mean())
    variation_coefficient = response_times.std() / response_times.mean()

    labels = [
        "Excellent\\n(CV < 0.1)",
        "Good\\n(0.1 ≤ CV < 0.25)",
        "Fair\\n(0.25 ≤ CV < 0.5)",
        "Poor\\n(CV ≥ 0.5)",
    ]
    thresholds = [0.1, 0.25, 0.5, 1.0]
    colors = ["green", "yellow", "orange", "red"]

    current_category = 0
    for i, threshold in enumerate(thresholds):
        if variation_coefficient < threshold:
            current_category = i
            break

    bars = ax4.bar(labels, [0.1, 0.25, 0.5, 1.0], color=colors, alpha=0.3)
    bars[current_category].set_alpha(0.8)
    ax4.axhline(
        variation_coefficient,
        color="black",
        linewidth=3,
        label=f"Current CV: {variation_coefficient:.3f}",
    )
    ax4.set_title("Performance Consistency Rating")
    ax4.set_ylabel("Coefficient of Variation")
    ax4.legend()
    ax4.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(f"{output_prefix}_detailed.png", dpi=300, bbox_inches="tight")
    print(f"📈 Detailed analysis saved as {output_prefix}_detailed.png")

    return stats


def print_analysis_summary(stats, df):
    """Print a comprehensive analysis summary"""
    print("\\n🎯 COMPREHENSIVE TIMING ANALYSIS SUMMARY")
    print("=" * 60)

    successful_count = (df["success"] == True).sum()
    failed_count = (df["success"] == False).sum()

    print(f"\\n📊 Request Overview:")
    print(f"  • Total Requests: {len(df)}")
    print(f"  • Successful: {successful_count}")
    print(f"  • Failed: {failed_count}")
    print(f"  • Success Rate: {successful_count/len(df)*100:.1f}%")

    if successful_count > 0:
        print(f"\\n⏱️ Response Time Analysis:")
        print(f"  • Mean: {stats['Mean']:.2f} ms")
        print(f"  • Median: {stats['Median']:.2f} ms")
        print(f"  • Standard Deviation: {stats['Std Dev']:.2f} ms")
        print(f"  • Min: {stats['Min']:.2f} ms")
        print(f"  • Max: {stats['Max']:.2f} ms")
        print(f"  • Range: {stats['Range']:.2f} ms")
        print(f"  • 95th Percentile: {stats['P95']:.2f} ms")
        print(f"  • 99th Percentile: {stats['P99']:.2f} ms")

        print(f"\\n📈 Variability Metrics:")
        print(f"  • Coefficient of Variation: {stats['CV']:.3f}")

        if stats["CV"] < 0.1:
            consistency = "Excellent (Very Low Variation)"
        elif stats["CV"] < 0.25:
            consistency = "Good (Low Variation)"
        elif stats["CV"] < 0.5:
            consistency = "Fair (Moderate Variation)"
        else:
            consistency = "Poor (High Variation)"

        print(f"  • Consistency Rating: {consistency}")

        print(f"\\n🔍 Performance Insights:")
        if stats["Max"] > stats["Mean"] * 2:
            print(
                f"  ⚠️ Some requests are significantly slower (max is {stats['Max']/stats['Mean']:.1f}x the mean)"
            )
        else:
            print(f"  ✓ Response times are relatively consistent")

        if stats["CV"] > 0.5:
            print(
                f"  ⚠️ High variability detected - server may be under stress or processing varying workloads"
            )
        elif stats["CV"] < 0.1:
            print(
                f"  ✓ Very consistent performance - server is handling requests predictably"
            )

        outlier_threshold = stats["Mean"] + 2 * stats["Std Dev"]
        response_times = df[df["success"] == True]["response_time_ms"]
        outliers = (response_times > outlier_threshold).sum()
        if outliers > 0:
            print(f"  ℹ️ {outliers} outlier(s) detected (>2 std dev from mean)")


def main():
    parser = argparse.ArgumentParser(
        description="Analyze and visualize timing benchmark results"
    )
    parser.add_argument("csv_file", help="CSV file with timing results")
    parser.add_argument(
        "--output-prefix",
        default="analysis",
        help="Prefix for output files (default: analysis)",
    )
    parser.add_argument(
        "--no-plot", action="store_true", help="Skip plotting (just show summary)"
    )

    args = parser.parse_args()

    # Load data
    df = load_results(args.csv_file)
    if df is None:
        sys.exit(1)

    print(f"📁 Loaded {len(df)} records from {args.csv_file}")

    # Create visualizations
    if not args.no_plot:
        try:
            stats = create_timing_visualizations(df, args.output_prefix)
            if stats:
                print_analysis_summary(stats, df)
        except Exception as e:
            print(f"Error creating visualizations: {e}")
            print("Showing text summary only...")

    # Always show basic summary
    successful_df = df[df["success"] == True]
    if len(successful_df) > 0:
        basic_stats = {
            "Count": len(successful_df),
            "Mean": successful_df["response_time_ms"].mean(),
            "Median": successful_df["response_time_ms"].median(),
            "Std Dev": successful_df["response_time_ms"].std(),
            "Min": successful_df["response_time_ms"].min(),
            "Max": successful_df["response_time_ms"].max(),
            "Range": successful_df["response_time_ms"].max()
            - successful_df["response_time_ms"].min(),
            "P95": successful_df["response_time_ms"].quantile(0.95),
            "P99": successful_df["response_time_ms"].quantile(0.99),
            "CV": successful_df["response_time_ms"].std()
            / successful_df["response_time_ms"].mean(),
        }
        if args.no_plot:
            print_analysis_summary(basic_stats, df)


if __name__ == "__main__":
    main()
