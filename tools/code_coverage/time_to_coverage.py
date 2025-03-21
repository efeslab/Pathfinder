import re
from datetime import datetime

# Path to the log file
log_file_path = "/home/yilegu/squint/pm-cc-bug-finder/targets/wiredtiger-bug-0/code_coverage_Feb_2/file_coverage_summary.txt"

# Function to parse the log and extract time-to-coverage data
def parse_coverage_log(file_path):
    # Regular expressions to match timestamps and coverage data
    timestamp_pattern = re.compile(r"Collecting coverage data at (.+)")
    coverage_pattern = re.compile(r"Total:\|([\d.]+)%\s+\d+\|([\d.]+)%\s+\d+")
    
    results = []
    start_time = None
    
    with open(file_path, "r") as file:
        for line in file:
            # Match timestamp
            timestamp_match = timestamp_pattern.match(line)
            if timestamp_match:
                timestamp_str = timestamp_match.group(1)
                timestamp = datetime.strptime(timestamp_str, "%a %b %d %H:%M:%S %p %Z %Y")
                if start_time is None:
                    start_time = timestamp
                time_offset = (timestamp - start_time).total_seconds()
                print(f"timestamp = {timestamp}, start_time = {start_time}, time_offset = {time_offset}")
            
            # Match coverage data
            coverage_match = coverage_pattern.search(line)
            if coverage_match:
                total_line_coverage = float(coverage_match.group(1))
                total_function_coverage = float(coverage_match.group(2))
                results.append((time_offset, total_line_coverage, total_function_coverage))
    
    return results

# Parse the log and print results
time_to_coverage = parse_coverage_log(log_file_path)
print("Time to Coverage Data (time_offset, total_line_coverage, total_function_coverage):")
for entry in time_to_coverage:
    print(entry)