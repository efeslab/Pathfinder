# Artifact Evaluation Instructions

## 1. Overview
We provide a ready-to-run environment for **Pathfinder** on a cloud machine (Intel® Xeon® Platinum 8380 @ 2.30 GHz). Included are:

- **Pathfinder**: our crash-consistency bug detection framework  
- **Alice**: the comparable prior work as baseline
- **Target applications** and **workload scripts**


## 2. Environment

| Component       | Version / Spec                               |
| --------------- | --------------------------------------------- |
| OS              | Ubuntu 22.04.5 LTS (x86_64)                   |
| CPU             | Intel® Xeon® Platinum 8380 @ 2.30 GHz         |
| Compiler        | Clang 14                                       |
| Debugger        | GDB 10.2                                      |
| Build system    | CMake 3.22, Make                             |
| Tracing tool    | Intel Pin 3.27                               |
| Python (Alice)  | Python 2.7 (via Conda)                       |


## 3. Artifact Directory Structure

- **artifact/**
  - **Pathfinder/**
    - `deps/`          — third-party libraries  
    - `core/`          — bug-finding engine  
    - `pin_tool/`      — Pin tracing scripts  
    - `targets/`       — workloads (e.g., leveldb, wiredtiger)  
  - **alice/**
    - `bin/`           — trace & check binaries  
    - `alice-strace/`  — tracing scripts  
    - `targets/`       — same workloads for comparison 
  - **Squint/** (prior stable version of Pathfinder that works for MMIO-based applications)


## 4. Instructions

1. Follow the `README.md` in `jiexiao/Pathfinder` to setup and run Pathfinder. Most targets should be directly runnable without changing the config files.
2. Follow the `README.md` in `jiexiao/alice` to setup and run ALICE baseline.
3. `jiexiao/Squint` contains a previous version of Pathfinder where we run MMIO-based applications evaluations on our own server. Due to the lack of persistent memory hardware support in the current server, compiling this version will encounter errors. However, we provide this stable version of the code for reference.
4. `/home/cc/jiexiao/Squint/tools/eval_utils/bug_locations.py` contains all bugs found in MMIO-based applications, including both production-ready systems and microbenchmarks (Section 6.1 and 6.2 of the paper). 


