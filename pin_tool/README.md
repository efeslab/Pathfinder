# Pin Tool Setup and Run Tutorial
This is a tutorial on building and running Pin tools that are used by Pathfinder.

## File Structure
```
./pin_tool
│── tool_src
│   ├── posix_tracer.cpp
│   ├── posix_read_observer.cpp
│   ├── mmio_read_observer.cpp
│   ├── makefile.unix.config
│   ├── ...
├── setup.sh
```



## Pin Tools Description
There are 3 Pin tools: `posix_tracer`, `posix_read_observer` and `mmio_read_observer`.

`posix_tracer`: Built from `tool_src/posix_tracer.cpp`. This is the main tracer Pathfinder will use to trace syscalls for POSIX-based applications. Additionally, it also supports tracing MMIO-based applications that use syscalls during the execution (e.g., LevelDB and RocksDB with mmap writes enabled).

`posix_read_observer`: Built from `tool_src/posix_read_observer.cpp`. This is a DPOR optimization for POSIX-based applications that can be enabled in Pathfinder. It is inspired by the "recovery observer" in [Persevere](https://dl.acm.org/doi/abs/10.1145/3434324). Given a recovery oracle and a crash state, `posix_read_observer` outputs the read syscalls issued by the oracle. Pathfinder will use this information to refine the set of crash states it needs to test.

`mmio_read_observer`: Built from `tool_src/mmio_read_observer.cpp`. Similar to `posix_read_observer`, this is a DPOR optimization for MMIO-based applications. It is inspired by the "constraint-refinement" process in [Jaaru](https://dl.acm.org/doi/abs/10.1145/3445814.3446735).

## Compile Pin Tools

We have prepared a script `setup.sh` to compile the above Pin tools automatically.

Before running the script, first configure the `tool_src/makefile.unix.config` that will be used as config for make. Specifically, change `TOOL_INCLUDES` and `TOOL_LPATHS` to be the correct paths pointing to the include and library for libb64. If libb64 is not compiled, make sure you first compile `pathfinder-core`.

Then run the script under `pin_tool` folder:

```
cd Pathfinder/pin_tool
./setup.sh
```

If the compilation is successful, you should be able to see the compiled Pin tools as dynamic libraries under `pin_tool/tool_src/obj-intel64/`.

## Pin Tools Usage
### POSIX Tracer
TODO

### POSIX Read Observer
TODO

### MMIO Read Observer
TODO

## Debug Pin Tools
Make sure you compile the tool with debug flag on. *make PIN_ROOT=../pin-3.21/ DEBUG=1 obj-intel64/YourTool.so* 

### Window 1
../pin-3.28/pin -pause_tool 30 -t obj-intel64/posix_trace.so -- ./workload {workload_args}

### Window 2
sudo gdb ../pin-3.28/pin

In gdb, run

```
set sysroot /not/existing/dir
file
add-symbol-file ./pin_tool/tool_src/obj-intel64/posix_tracer.so 0x7f7f279453d0 -s .data 0x7f7f27ac49c0 -s .bss 0x7f7f27ac5380

set architecture i386:x86-64
attach pid
cont
```

## makefile.unix.config
The *makefile.unix.config* file under *memory_ref/* folder is an example config file for correct compilation of the Pin tool, it serves as a guide on how to add custom libraries (i.e. libb64) during compilation. Users should replace the library path and include path with their own when compiling, and replace *pin-3.28/source/tools/Config/makefile.unix.config* with this file.
