#!/bin/bash

# # LevelDB Bug 0
# # Directory containing the .gcno and .gcda files
# BUILD_DIR="/home/yilegu/squint/bug_study/leveldb-0/leveldb-before"

# # Ouptut Dir
# OUTPUT_DIR="/home/yilegu/squint/pm-cc-bug-finder/targets/leveldb-bug-0/code_coverage_1"

# LevelDB Bug 1
# Directory containing the .gcno and .gcda files
# BUILD_DIR="/home/yilegu/squint/bug_study/leveldb-1/leveldb"

# # Ouptut Dir
# OUTPUT_DIR="/home/yilegu/squint/pm-cc-bug-finder/targets/leveldb-bug-1/code_coverage"

# LevelDB Bug 2
# # Directory containing the .gcno and .gcda files
# BUILD_DIR="/home/yilegu/squint/bug_study/leveldb-2/leveldb"

# # Ouptut Dir
# OUTPUT_DIR="/home/yilegu/squint/pm-cc-bug-finder/targets/leveldb-bug-2/code_coverage"

# # RocksDB Bug 0
# # Directory containing the .gcno and .gcda files
# BUILD_DIR="/home/yilegu/squint/bug_study/rocksdb-0/rocksdb-squint"

# # Ouptut Dir
# OUTPUT_DIR="/home/yilegu/squint/pm-cc-bug-finder/targets/rocksdb-bug-0/code_coverage"

# WiredTiger Bug 0
# Directory containing the .gcno and .gcda files
BUILD_DIR="/home/yilegu/squint/bug_study/wiredtiger-1-recover-from-backup/wiredtiger-before"

# Ouptut Dir
OUTPUT_DIR="/home/yilegu/squint/pm-cc-bug-finder/targets/wiredtiger-bug-0/code_coverage_Feb_2"

# # WiredTiger Bug 1
# # Directory containing the .gcno and .gcda files
# BUILD_DIR="/home/yilegu/squint/bug_study/wiredtiger-1-recover-from-backup/wiredtiger"

# # Ouptut Dir
# OUTPUT_DIR="/home/yilegu/squint/pm-cc-bug-finder/targets/wiredtiger-bug-1/code_coverage_Feb_2"

# mkdir OUTPUT_DIR if not exist
if [ ! -d "$OUTPUT_DIR" ]; then
    mkdir -p "$OUTPUT_DIR"
fi

# Interval in seconds between each coverage check
INTERVAL=1

# Output files for periodic coverage data
COVERAGE_INFO=$OUTPUT_DIR"/coverage.info"
FILTERED_COVERAGE_INFO=$OUTPUT_DIR"/filtered_coverage.info"
COVERAGE_SUMMARY_TXT=$OUTPUT_DIR"/file_coverage_summary.txt"

# Program to run
WORKLOAD="./workload"

# Clean up existing .gcda files
echo "Cleaning up existing .gcda files in $BUILD_DIR"
find "$BUILD_DIR" -name "*.gcda" -delete

# Ensure the coverage summary log is clean
> "$COVERAGE_SUMMARY_TXT"

# Run the workload in the background
# echo "Starting workload..."
# $WORKLOAD &
# WORKLOAD_PID=$!

# Function to collect and format coverage data periodically
function collect_coverage() {
    while true; do
        echo "Collecting coverage data at $(date)" >> "$COVERAGE_SUMMARY_TXT"

        # Step 1: Capture coverage data
        lcov --capture --directory "$BUILD_DIR" --output-file "$COVERAGE_INFO"

        # Step 2: Remove irrelevant files
        lcov --remove "$COVERAGE_INFO" '/usr/*' --output-file "$FILTERED_COVERAGE_INFO"

        # Step 3: Append file-by-file coverage information
        lcov --list "$FILTERED_COVERAGE_INFO" >> "$COVERAGE_SUMMARY_TXT"

        echo "Coverage data collected." >> "$COVERAGE_SUMMARY_TXT"

        # Wait for the specified interval before the next check
        sleep $INTERVAL
    done

    # Final coverage collection after workload completion
    echo "Final coverage data at $(date):" >> "$COVERAGE_SUMMARY_TXT"
    lcov --capture --directory "$BUILD_DIR" --output-file "$COVERAGE_INFO"
    lcov --remove "$COVERAGE_INFO" '/usr/*' --output-file "$FILTERED_COVERAGE_INFO"
    lcov --list "$FILTERED_COVERAGE_INFO" >> "$COVERAGE_SUMMARY_TXT"
}

# Collect coverage periodically
collect_coverage

echo "Coverage collection complete. See $COVERAGE_SUMMARY_TXT for details."
