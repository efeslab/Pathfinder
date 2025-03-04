#!/bin/bash
set -e
trap 'error ${LINENO}' ERR

rm -rf /home/jiexiao/pathfinder/pm-cc-bug-finder/pin_tool/tool_src/workload
mkdir /home/jiexiao/pathfinder/pm-cc-bug-finder/pin_tool/tool_src/workload

make PIN_ROOT=../pin-3.28 obj-intel64/posix_trace_read.so

/home/jiexiao/pathfinder/pm-cc-bug-finder/pin_tool/pin-3.28/pin -t /home/jiexiao/pathfinder/pm-cc-bug-finder/pin_tool/tool_src/obj-intel64/posix_trace_read.so -o /home/jiexiao/pathfinder/pm-cc-bug-finder/pin_tool/tracer.log -tf /home/jiexiao/pathfinder/alice/alice/bugs/bug3/rocksdb-pathfinder -- /home/jiexiao/pathfinder/alice/alice/bugs/bug3/rocksdb-bug-0/workload /home/jiexiao/pathfinder/pm-cc-bug-finder/pin_tool/tool_src/workload