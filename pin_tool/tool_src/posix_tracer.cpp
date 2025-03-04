/*BEGIN_LEGAL 
  Intel Open Source License 

  Copyright (c) 2002-2017 Intel Corporation. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <execinfo.h>
#include <filesystem>
#include <iostream>
#include <regex>
#include <vector>
#include <cxxabi.h>
#include <cstdlib>
#include <unordered_map>

// #include <ucontext.h>
// #include <libunwind.h>

// #include <openssl/bio.h>
// #include <openssl/evp.h>
// #include <openssl/buffer.h>
extern "C" {
	#include <b64/cencode.h>
	#include <b64/cdecode.h>
}

#include "pin.H"

#define TRUE		1
#define FALSE		0
#define PAGE_SHIFT	12
#define PAGE_MASK	(~(PAGE_SIZE - 1))
#define MAP_DEBUG	0x80000

#define FILENAME_SIZE 512
#define BACKTRACE_SIZE 100

#define MALLOC "malloc"
#define FREE "free"

#define PRINT_BACKTRACE 1
#define DEBUG 0

// for workload which use markers to trace operations
#define PATHFINDER_OP_BEGIN "PATHFINDER_OP_BEGIN"
#define PATHFINDER_OP_END "PATHFINDER_OP_END"

KNOB< std::string > tf_knob(KNOB_MODE_WRITEONCE, "pintool",
		"tf", "", "specify target file name and selectively trace updates to this file");

KNOB< std::string > of_knob(KNOB_MODE_WRITEONCE, "pintool",
		"o", "posix_trace.out", "specify output file name");

KNOB< bool > rv_knob(KNOB_MODE_WRITEONCE, "pintool",
		"record-value", "1", "record write values for each write syscall");

//==============================================================
//  Analysis Routines
//==============================================================
// Note:  threadid+1 is used as an argument to the PIN_GetLock()
//        routine as a debugging aid.  This is the value that
//        the lock is set to, so it must be non-zero.

// lock serializes access to the output file.
FILE * out;
PIN_LOCK pinLock;
PIN_RWMUTEX RWMutex;
std::string target_filename;
std::string at_fdcwd = "";
bool record_value = false;

// global counters
unsigned long timestamp = 0;
unsigned long store_id = 0;

// for recording op count if pathfinder markers are used
// thread id -> op count
std::unordered_map<int, int> op_count;

typedef enum callType {
	SYSTEMCALL = 100,
	LIBCALL,
} CallType;

typedef enum syscallType {
	S_NONE = 0,
	S_MMAP,
	S_MUNMAP,
	S_MSYNC,
	S_MADVISE,
	S_FTRUNCATE,
	S_PWRITE64,
	S_WRITE,
	S_WRITEV,
	S_LSEEK,
	S_RENAME,
	S_UNLINK,
	S_FSYNC,
	S_FDATASYNC,
	S_FALLOCATE,
	S_OPEN,
	S_CREAT,
	S_OPENAT,
	S_CLOSE,
	S_MKDIR,
	S_MKDIRAT,
	S_RMDIR,
	S_SYNC,
	S_SYNCFS,
	S_SYNC_FILE_RANGE,
	S_CHDIR,
	S_FCHDIR,
	S_SKIP // a special type for skip tracing this system call
} SyscallType;

typedef enum libcallType {
	L_NONE = 10,
	L_MEMCPY,
	L_MEMSET,
	L_SKIP // a special type for skip tracing this library call
} LibcallType;

typedef struct threadNode {
	THREADID tid;
	SyscallType sType;
	LibcallType lType;
	void *syscallArgs;
	void *libcallArgs;
	struct threadNode *next;
	void *mem_addr;
	uint32_t mem_size;
} ThreadNode;

//ThreadNode *threadNodeHead = NULL;
//ThreadNode *threadNodeTail = NULL;
ThreadNode threadArray[100];

// mmio library call arguments and data structures
typedef struct memNode {
	char filename[FILENAME_SIZE];
	unsigned long start;
	unsigned long end;
	unsigned long length;
	unsigned long nrpages;
	struct memNode *next;
} MemNode;

MemNode *memNodeHead = NULL;
MemNode *memNodeTail = NULL;

typedef struct mmapArgs {
	char filename[FILENAME_SIZE];
	unsigned long addr;
	size_t length;
	int prot;
	int flags;
	int fd;
	off_t offset;
} MmapArgs;

typedef struct munmapArgs {
	char filename[FILENAME_SIZE];
	unsigned long addr;
	size_t length;
} MunmapArgs;

typedef struct msyncArgs {
	char filename[FILENAME_SIZE];
	unsigned long addr;
	size_t length;
	int flags;
} MsyncArgs;

// posix syscall call arguments
typedef struct ftruncateArgs {
	char filename[FILENAME_SIZE];
	off_t length;
	int fd;
} FtruncateArgs;

typedef struct pwrite64Args {
	char filename[FILENAME_SIZE];
	int fd;
	const void *buf;
	size_t count;
	off_t offset;
} Pwrite64Args;

typedef struct writeArgs {
	char filename[FILENAME_SIZE];
	int fd;
	const void *buf;
	size_t count;
} WriteArgs;

typedef struct writevArgs {
	char filename[FILENAME_SIZE];
	int fd;
	const struct iovec *iov;
	int iovcnt;
} WritevArgs;

typedef struct lseekArgs {
	char filename[FILENAME_SIZE];
	int fd;
	off_t offset;
	int whence;
} LseekArgs;

typedef struct renameArgs {
	char oldpath[FILENAME_SIZE];
	char newpath[FILENAME_SIZE];
} RenameArgs;

typedef struct unlinkArgs {
	char filename[FILENAME_SIZE];
} UnlinkArgs;

typedef struct fsyncArgs {
	char filename[FILENAME_SIZE];
	int fd;
} FsyncArgs;

typedef struct fdatasyncArgs {
	char filename[FILENAME_SIZE];
	int fd;
} FdatasyncArgs;

typedef struct fallocateArgs {
	char filename[FILENAME_SIZE];
	int fd;
	int mode;
	off_t offset;
	off_t len;
} FallocateArgs;

typedef struct openArgs {
	char filename[FILENAME_SIZE];
	int flags;
	mode_t mode;
} OpenArgs;

typedef struct creatArgs {
	char filename[FILENAME_SIZE];
	mode_t mode;
} CreatArgs;

typedef struct openatArgs {
	char filename[FILENAME_SIZE];
	int dirfd;
	int flags;
	mode_t mode;
} OpenatArgs;

// TODO: support openat2?

typedef struct closeArgs {
	char filename[FILENAME_SIZE];
	int fd;
} CloseArgs;

typedef struct mkdirArgs {
	char filename[FILENAME_SIZE];
	mode_t mode;
} MkdirArgs;

typedef struct mkdiratArgs {
	char filename[FILENAME_SIZE];
	int dirfd;
	mode_t mode;
} MkdiratArgs;

typedef struct rmdirArgs {
	char filename[FILENAME_SIZE];
} RmdirArgs;

typedef struct syncArgs {
	char filename[FILENAME_SIZE];
} SyncArgs;

typedef struct syncfsArgs {
	char filename[FILENAME_SIZE];
	int fd;
} SyncfsArgs;

typedef struct sync_file_rangeArgs {
	char filename[FILENAME_SIZE];
	int fd;
	off64_t offset;
	off64_t nbytes;
	unsigned int flags;
} SyncFileRangeArgs;

typedef struct chdirArgs {
	char filename[FILENAME_SIZE];
} ChdirArgs;

typedef struct fchdirArgs {
	char filename[FILENAME_SIZE];
	int fd;
} FchdirArgs;

// memory library call arguments

typedef struct memcpyArgs {
	char filename[FILENAME_SIZE];
	unsigned long dst;
	unsigned long src;
	size_t length;
} MemcpyArgs;

typedef struct memsetArgs {
	char filename[FILENAME_SIZE];
	unsigned long addr;
	int c;
	size_t length;
} MemsetArgs;

/* arbitrary buffer size */
#define BUFFER_SIZE 1000000

char* remove_newlines(char *str) {
    char *dst = str;
    for (const char *src = str; *src != '\0'; src++) {
        *dst = *src;
        if (*dst != '\n') dst++; // Skip the newline character.
    }
    *dst = '\0'; // Null-terminate the string.
	return str;
}

std::string sanitize_filename(std::string filename) {
	// if last two characters are /., then remove them
	if (filename.size() >= 2 && filename[filename.size()-1] == '.' && filename[filename.size()-2] == '/') {
		filename = filename.substr(0, filename.size()-2);
	}
	// if last character is /, then remove it
	if (filename.size() >= 1 && filename[filename.size()-1] == '/') {
		filename = filename.substr(0, filename.size()-1);
	}
	return filename;
}

std::string base64_encode(const char* input, uint32_t size)
{
	/* set up a destination buffer large enough to hold the encoded data */
	char* output = (char*)malloc(BUFFER_SIZE);
	/* keep track of our encoded position */
	char* c = output;
	/* store the number of bytes encoded by a single call */
	int cnt = 0;
	/* we need an encoder state */
	base64_encodestate s;
	
	/*---------- START ENCODING ----------*/
	/* initialise the encoder state */
	base64_init_encodestate(&s);
	/* gather data from the input and send it to the output */
	cnt = base64_encode_block(input, size, c, &s);
	c += cnt;
	/* since we have encoded the entire input string, we know that 
	   there is no more input data; finalise the encoding */
	cnt = base64_encode_blockend(c, &s);
	c += cnt;
	/*---------- STOP ENCODING  ----------*/
	
	/* we want to print the encoded data, so null-terminate it: */
	*c = 0;

	// we can remove newlines because base64 decoer ignores newlines
	output = remove_newlines(output);
	std::string output_str(output);
	free(output);
	return output_str;
}

char* base64_decode(const char* input, uint32_t size)
{
	/* set up a destination buffer large enough to hold the encoded data */
	char* output = (char*)malloc(BUFFER_SIZE);
	/* keep track of our decoded position */
	char* c = output;
	/* store the number of bytes decoded by a single call */
	int cnt = 0;
	/* we need a decoder state */
	base64_decodestate s;
	
	/*---------- START DECODING ----------*/
	/* initialise the decoder state */
	base64_init_decodestate(&s);
	/* decode the input data */
	cnt = base64_decode_block(input, size, c, &s);
	c += cnt;
	/* note: there is no base64_decode_blockend! */
	/*---------- STOP DECODING  ----------*/
	
	/* we want to print the decoded data, so null-terminate it: */
	*c = 0;
	
	return output;
}

void fd_to_filename(THREADID tid, int fd, char* filename) {
	int pid;
	char *path;
	ssize_t result;

	/* get filename */
	pid = PIN_GetPid();
	path = (char *)malloc(FILENAME_SIZE);
	strcpy(path, "/proc/");
	sprintf(&path[strlen(path)], "%d", pid);
	strcat(path, "/fd/");
	sprintf(&path[strlen(path)], "%d", fd);

	result = readlink(path, filename, FILENAME_SIZE);

	if (result < 0) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "readlink() failed. path=%s, filename=%s, fd=%d (ERROR)\n",
				path, filename, fd);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(path);
		exit(EXIT_FAILURE);
	}
	else if (result >= FILENAME_SIZE) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "readlink() result is longer than the buffer length, consider allocating a larger buffer!. path=%s, filename=%s, fd=%d (ERROR)\n",
				path, filename, fd);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(path);
		exit(EXIT_FAILURE);
	
	}
	else {
		filename[result] = '\0';
	
	}
	free(path);
}

std::tuple<std::string, std::string, std::string, std::string>
parseFunctionInfo(const std::string& str) {
    std::string function, file, line, address;

    // Extract function name
    size_t posFuncEnd = str.find("+");
    if (posFuncEnd != std::string::npos) {
        function = str.substr(0, posFuncEnd);
    }

    // Extract binary address
    size_t posAddressStart = str.rfind("+") + 1;
    size_t posAddressEnd = str.find(" ", posAddressStart);
    if (posAddressStart != std::string::npos) {
        address = str.substr(posAddressStart, posAddressEnd - posAddressStart);
    }

    // Extract file and line, if multiple () pairs are found, use the last one
	size_t posParenthesisStart = str.rfind("(");
	size_t posParenthesisEnd = str.rfind(")");
    if (posParenthesisStart != std::string::npos && posParenthesisEnd != std::string::npos) {
        size_t posColon = str.find(":", posParenthesisStart);
        file = str.substr(posParenthesisStart + 1, posColon - posParenthesisStart - 1);
        line = str.substr(posColon + 1, posParenthesisEnd - posColon - 1);
    }

    return {function, file, line, address};
}


// Function to print the backtrace
void printBacktraceUnwind(const CONTEXT * 	ctxt) {
    // unw_cursor_t cursor;
    // unw_context_t context;

    // // Initialize context to get the machine state
    // unw_getcontext(&context);
    // unw_init_local(&cursor, &context);

    // // Walk through the stack frame and print it
    // while (unw_step(&cursor) > 0) {
    //     unw_word_t offset, pc;
    //     char fname[64];

    //     unw_get_reg(&cursor, UNW_REG_IP, &pc);
    //     if (pc == 0) {
    //         break;
    //     }
    //     std::cout << "0x" << std::hex << pc << ":";

    //     if (unw_get_proc_name(&cursor, fname, sizeof(fname), &offset) == 0) {
    //         std::cout << " (" << fname << "+0x" << std::hex << offset << ")";
    //     }
    //     std::cout << std::endl;
    // }
}

int printBacktrace(const CONTEXT * 	ctxt) {
	// we are not interested in stack frames from standard libraries, so might as well filter out
	std::vector<std::string> filters = {"/usr/include"};
	void* buf[BACKTRACE_SIZE];
	PIN_LockClient();
	int nptrs = PIN_Backtrace(ctxt, buf, sizeof(buf)/sizeof(buf[0]));
    char** bt = backtrace_symbols(buf, nptrs);
	PIN_UnlockClient();
	// for (int i = 0; i < nptrs && i <= 2; i++) {
	// 	fprintf(out, "%s,", bt[i]);
	// }
	// fprintf(out, "\n");
	// for now just print the first trace
	// if (nptrs > 0) {
	// fprintf(out, "%s\n", bt[0]);
	// }

	// use parseFunctionInfo to parse each line of bt
	for (int i = 0; i < nptrs; i++) {
		std::string str;
		int status;
		char* demangled_name = abi::__cxa_demangle(bt[i], NULL, NULL, &status);
		if (status == 0 && demangled_name != NULL) {
			// Use demangled_name
			str = demangled_name;
		} else {
			// Use original mangled name (bt[i])
			str = bt[i];
		}
		// Free the memory allocated by __cxa_demangle
		free(demangled_name);
		auto [func, file, line, offset] = parseFunctionInfo(str);
		bool skip = false;
		for (auto filter : filters) {
			if (file.find(filter) != std::string::npos) {
				skip = true;
				break;
			}
		}
		if (!skip) {
			fprintf(out, "%s,%s,%s,%s;", func.c_str(), file.c_str(), line.c_str(), offset.c_str());
		}
	}
	fprintf(out, "\n");
	fflush(out);

	free(bt);
	return nptrs;
}

VOID BeforeMalloc(CONTEXT* ctxt, CHAR* name, ADDRINT size) { 
	fprintf(out, "%s begins (%lu)\n", name, size);
	printBacktrace(ctxt);
 }
 
VOID AfterMalloc(CONTEXT* ctxt, ADDRINT ret) { 
	// print ret in pointer form
	fprintf(out, "malloc() ends = %p\n", (void *)ret);
	printBacktrace(ctxt);
 }

VOID BeforeFree(CONTEXT* ctxt, CHAR* name, ADDRINT addr) { 
	fprintf(out, "%s begins (%p)\n", name, (void *)addr);
	// printBacktrace(ctxt);
 }


char *findMemNode(unsigned long addr, unsigned long *pgoff, THREADID tid) {
	MemNode *curr = NULL;

	curr = memNodeHead;
	while (curr) {
		if ((curr->start <= addr) && (addr < curr->end)) {
			*pgoff = ((addr & PAGE_MASK) - curr->start) >> PAGE_SHIFT;
			return curr->filename;
		}
		curr = curr->next;
	}
	return NULL;
}

int insertMemNode(char *filename, unsigned long start, unsigned long end, unsigned long length,
		unsigned long nrpages, THREADID tid) {
	MemNode *newNode = (MemNode *)malloc(sizeof(MemNode));

	if (newNode == NULL) {
		fprintf(out, "[%d] %s: malloc() failed. (ERROR)\n", tid, __func__);
		fflush(out);
		return FALSE;
	}
	strcpy(newNode->filename, filename);
	newNode->start = start;
	newNode->end = end;
	newNode->length = length;
	newNode->nrpages = nrpages;
	newNode->next = NULL;

	if (memNodeHead == NULL)
		memNodeHead = newNode;
	if (memNodeTail != NULL)
		memNodeTail->next = newNode;
	memNodeTail = newNode;
	//fprintf(out, "[%d] %s: A memNode was inserted into the memNode list.\n", tid, __func__);
	//fflush(out);
	return TRUE;
}

#if 0
int insertThreadNode(THREADID tid) {
	ThreadNode *newNode = (ThreadNode *)malloc(sizeof(ThreadNode));

	if (newNode == NULL) {
		fprintf(out, "[%d] %s: malloc() failed. (ERROR)\n", tid, __func__);
		fflush(out);
		return FALSE;
	}
	newNode->tid = tid;
	newNode->sType = S_NONE;
	newNode->lType = L_NONE;
	newNode->syscallArgs = NULL;
	newNode->libcallArgs = NULL;
	newNode->next = NULL;

	if (threadNodeHead == NULL)
		threadNodeHead = newNode;
	if (threadNodeTail != NULL)
		threadNodeTail->next = newNode;
	threadNodeTail = newNode;
	//fprintf(out, "[%d] %s: A threadNode was inserted into the threadNode list.\n", tid, __func__);
	//fflush(out);
	return TRUE;
}
#endif

int deleteMemNode(unsigned long start, unsigned long length, THREADID tid) {
	MemNode *prev = NULL;
	MemNode *curr = NULL;

	curr = memNodeHead;
	while (curr) {
		if ((curr->start == start) && (curr->length == length)) {
			if (prev != NULL)
				prev->next = curr->next;
			else
				memNodeHead = curr->next;
			if (curr->next == NULL)
				memNodeTail = prev;
			free(curr);
			//fprintf(out, "[%d] %s: A memNode was deleted.\n", tid, __func__);
			//fflush(out);
			return TRUE;
		}
		prev = curr;
		curr = curr->next;
	}
	fprintf(out, "[%d] %s: This memNode can not be deleted. (ERROR)\n", tid, __func__);
	fflush(out);
	return FALSE;
}

#if 0
int deleteThreadNode(THREADID tid) {
	ThreadNode *prev = NULL;
	ThreadNode *curr = NULL;

	curr = threadNodeHead;
	while (curr) {
		if (curr->tid == tid) {
			if (prev != NULL)
				prev->next = curr->next;
			else
				threadNodeHead = curr->next;
			if (curr->next == NULL)
				threadNodeTail = prev;
			free(curr);
			//fprintf(out, "[%d] %s: A threadNode was deleted.\n", tid, __func__);
			//fflush(out);
			return TRUE;
		}
		prev = curr;
		curr = curr->next;
	}
	fprintf(out, "[%d] %s: This threadNode can not be deleted. (ERROR)\n", tid, __func__);
	fflush(out);
	return FALSE;
}
#endif

#if 0
int putSyscallArgs(THREADID tid, void *args, SyscallType type) {
	ThreadNode *curr = threadNodeHead;

	while (curr) {
		if (curr->tid == tid) {
			if (curr->syscallArgs != NULL) {
				fprintf(out, "%d, %s: syscallArgs already exists. (ERROR)\n",
						tid, __func__);
				fflush(out);
				exit(EXIT_FAILURE);
			}
			curr->syscallArgs = args;
			curr->sType = type;
			return TRUE;
		}
		curr = curr->next;
	}
	fprintf(out, "%d, %s: THREADID is not exist. (ERROR)\n", tid, __func__);
	fflush(out);
	return FALSE;
}

int putLibcallArgs(THREADID tid, void *args, LibcallType type) {
	ThreadNode *curr = threadNodeHead;

	while (curr) {
		if (curr->tid == tid) {
			if (curr->libcallArgs != NULL) {
				fprintf(out, "%d, %s: libcallArgs already exists. (ERROR)\n",
						tid, __func__);
				fflush(out);
				exit(EXIT_FAILURE);
			}
			curr->libcallArgs = args;
			curr->lType = type;
			return TRUE;
		}
		curr = curr->next;
	}
	fprintf(out, "%d, %s: THREADID is not exist. (ERROR)\n", tid, __func__);
	fflush(out);
	return FALSE;
}

void *getSyscallArgs(THREADID tid, SyscallType *type) {
	ThreadNode *curr = threadNodeHead;
	void *args = NULL;

	while (curr) {
		if (curr->tid == tid) {
			/*
			if (curr->syscallArgs == NULL) {
				fprintf(out, "%d, %s: There is no syscallArgs. (ERROR)\n",
						tid, __func__);
				fflush(out);
				exit(EXIT_FAILURE);
			}
			*/
			args = curr->syscallArgs;
			*type = curr->sType;
			curr->syscallArgs = NULL;
			return args;
		}
		curr = curr->next;
	}
	fprintf(out, "%d, %s: THREADID is not exist. (ERROR)\n", tid, __func__);
	fflush(out);
	return NULL;
}

void *getLibcallArgs(THREADID tid, LibcallType *type) {
	ThreadNode *curr = threadNodeHead;
	void *args = NULL;

	while (curr) {
		if (curr->tid == tid) {
			/*
			if (curr->libcallArgs == NULL) {
				fprintf(out, "%d, %s: There is no libcallArgs. (ERROR)\n",
						tid, __func__);
				fflush(out);
				exit(EXIT_FAILURE);
			}
			*/
			args = curr->libcallArgs;
			*type = curr->lType;
			curr->libcallArgs = NULL;
			return args;
		}
		curr = curr->next;
	}
	fprintf(out, "%d, %s: THREADID is not exist. (ERROR)\n", tid, __func__);
	fflush(out);
	return NULL;
}
#endif

// This routine is executed every time a thread is created.
VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v) {
	// PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%d, thread-begin\n",tid);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);

	if (tid > 1000) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: There are too many threads. (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}
	//insertThreadNode(tid);
	threadArray[tid].sType = S_NONE;
	threadArray[tid].syscallArgs = NULL;
	threadArray[tid].lType = L_NONE;
	threadArray[tid].libcallArgs = NULL;
}

// This routine is executed every time a thread is destroyed.
VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v) {
	// PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%d, thread-end\n",tid);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
	//deleteThreadNode(tid);
}

// This routine is executed each time mmap() is called.
VOID BeforeMmap(THREADID tid,
		unsigned long addr, size_t length, int prot, int flags, int fd, off_t offset) {
	MmapArgs *args;

	/* only memory mapped files are traced, and skip when mmap is not writeable. */
	if (fd <= 0 || !(prot & PROT_WRITE)) {
		// fprintf(out, "%d, not memory mapped file, SKIP\n", tid);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	/* trace only when mmap is called with the MAP_DEBUG flag. */
	// if (!(flags & MAP_DEBUG))
	// 	return;

	args = (MmapArgs *)malloc(sizeof(MmapArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->addr = addr;
	args->length = length;
	args->prot = prot;
	args->flags = flags;
	args->fd = fd;
	args->offset = offset;

	/*
	PIN_GetLock(&pinLock, tid+1);
	putSyscallArgs(tid, (void *)args, S_MMAP);
	PIN_ReleaseLock(&pinLock);
	*/
	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_MMAP;

	// PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%d, mmap-call, 0x%lx, %lu, 0x%x, 0x%x, %s, %ld\n",
	// 		tid, addr, length, prot, flags, filename, offset);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
}

VOID AfterMmap(THREADID tid, MmapArgs *args, void *ret, CONTEXT *ctxt) {
	unsigned long start, end, nrpages;
	int result;

	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == MAP_FAILED) {
		free(args);
		return;
	}

	start = (unsigned long)ret;
	end = start + args->length;
	nrpages = args->length >> PAGE_SHIFT;
	args->addr = start;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	result = insertMemNode(args->filename, start, end, args->length, nrpages, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (result) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%lu,%d,REGISTER_FILE,%s,0x%lx,%lu,%ld,%d,%d;", timestamp, tid, args->filename, args->addr, args->length, args->offset, args->prot, args->flags);
		#if PRINT_BACKTRACE
		printBacktrace(ctxt);
		#else
		fprintf(out, "\n");
		#endif
		fflush(out);
		#if DEBUG
		printf("%lu,%d,REGISTER_FILE,%s,0x%lx,%lu,%ld,%d,%d;", timestamp, tid, args->filename, args->addr, args->length, args->offset, args->prot, args->flags);
		#endif
		PIN_RWMutexWriteLock(&RWMutex);
		timestamp++;
		PIN_RWMutexUnlock(&RWMutex);
		PIN_ReleaseLock(&pinLock);
	}
	else {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: insertMemNode failed. (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	free(args);
}

// This routine is executed each time munmap() is called.
VOID BeforeMunmap(THREADID tid, unsigned long addr, size_t length) {
	MunmapArgs *args;
	char *filename;
	unsigned long pgoff;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	filename = findMemNode(addr, &pgoff, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (filename == NULL || (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL)) {
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (MunmapArgs *)malloc(sizeof(MunmapArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);
	args->addr = addr;
	args->length = length;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_MUNMAP;

	// PIN_GetLock(&pinLock, tid+1);
	// //putSyscallArgs(tid, (void *)args, S_MUNMAP);
	// fprintf(out, "%d, munmap-call, 0x%lx, %lu\n", tid, addr, length);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
}

VOID AfterMunmap(THREADID tid, MunmapArgs *args, int ret, CONTEXT *ctxt) {
	int result;

	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	result = deleteMemNode(args->addr, args->length, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (result) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%lu,%d,UNREGISTER_FILE,%s,0x%lx,%lu;", timestamp, tid, args->filename, args->addr, args->length);	
		#if PRINT_BACKTRACE
		printBacktrace(ctxt);
		#else
		fprintf(out, "\n");
		#endif
		fflush(out);
		#if DEBUG
		printf("%lu,%d,UNREGISTER_FILE,%s,0x%lx,%lu;", timestamp, tid, args->filename, args->addr, args->length);
		#endif
		PIN_RWMutexWriteLock(&RWMutex);
		timestamp++;
		PIN_RWMutexUnlock(&RWMutex);
		PIN_ReleaseLock(&pinLock);
	}
	else {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: deleteMemNode failed. (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}
	free(args);
}

// This routine is executed each time msync() is called.
VOID BeforeMsync(THREADID tid, unsigned long addr, size_t length, int flags) {
	MsyncArgs *args;
	char *filename;
	unsigned long pgoff;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	filename = findMemNode(addr, &pgoff, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (filename == NULL || (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL)) {
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (MsyncArgs *)malloc(sizeof(MsyncArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);
	args->addr = addr;
	args->length = length;
	args->flags = flags;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_MSYNC;

	// PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%d, msync-call, 0x%lx, %lu, %s\n",
	// 		tid, addr, length, filename);
	// fflush(out);
	// //putSyscallArgs(tid, (void *)args, S_MSYNC);
	// PIN_ReleaseLock(&pinLock);
}

VOID AfterMsync(THREADID tid, MsyncArgs *args, int ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,MSYNC,%s,0x%lx,%lu,%d;", timestamp, tid, args->filename, args->addr, args->length, args->flags);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,MSYNC,%s,0x%lx,%lu,%d;", timestamp, tid, args->filename, args->addr, args->length, args->flags);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock); 
	free(args);
}

VOID BeforeFtruncate(THREADID tid, int fd, off_t length) {
	FtruncateArgs *args;

	args = (FtruncateArgs *)malloc(sizeof(FtruncateArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->length = length;
	args->fd = fd;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_FTRUNCATE;

}

VOID AfterFtruncate(THREADID tid, FtruncateArgs *args, int ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,FTRUNCATE,%d,%s,%ld;", timestamp, tid, args->fd, args->filename, args->length);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,FTRUNCATE,%d,%s,%ld;", timestamp, tid, args->fd, args->filename, args->length);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforePwrite64(THREADID tid, int fd, const void *buf, size_t count, off_t offset) {
	Pwrite64Args *args;

	args = (Pwrite64Args *)malloc(sizeof(Pwrite64Args));	
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;
	args->buf = buf;
	args->count = count;
	args->offset = offset;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_PWRITE64;

}

VOID AfterPwrite64(THREADID tid, Pwrite64Args *args, ssize_t ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	std::string encoded_result;
	if (record_value) {
		encoded_result = base64_encode((char *)args->buf, args->count);
		// debug print char array in out
		// fprintf(out, "hexdump of args->buf before encode \n");
		// for (size_t i = 0; i < args->count; i++) {
		// 	fprintf(out, "%02x ", ((char *)args->buf)[i]);
		// }
		// fprintf(out, "\n");

		// char* decode_result = base64_decode(encoded_result.c_str(), encoded_result.size());
		// fprintf(out, "hexdump of args->buf after decode \n");
		// for (size_t i = 0; i < args->count; i++) {
		// 	fprintf(out, "%02x ", decode_result[i]);
		// }
		// fprintf(out, "\n");
		if (encoded_result[encoded_result.size()-1] == '\n') {
			fprintf(out, "%lu,%d,PWRITE64,%d,%s,%ld,%lu,%s;", timestamp, tid, args->fd, args->filename, args->offset, args->count, encoded_result.substr(0, encoded_result.size()-1).c_str());
		}
		else {
			fprintf(out, "%lu,%d,PWRITE64,%d,%s,%ld,%lu,%s;", timestamp, tid, args->fd, args->filename, args->offset, args->count, encoded_result.c_str());
		}
	}
	else {
		fprintf(out, "%lu,%d,PWRITE64,%d,%s,%ld,%lu,;", timestamp, tid, args->fd, args->filename, args->offset, args->count);
	}
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	if (record_value) {
		printf("%lu,%d,PWRITE64,%d,%s,%ld,%lu,%s;", timestamp, tid, args->fd, args->filename, args->offset, args->count, encoded_result.substr(0, encoded_result.size()-1).c_str());
	}
	else {
		printf("%lu,%d,PWRITE64,%d,%s,%ld,%lu,;", timestamp, tid, args->fd, args->filename, args->offset, args->count);
	}
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeWrite(THREADID tid, int fd, const void *buf, size_t count) {
	WriteArgs *args;

	args = (WriteArgs *)malloc(sizeof(WriteArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;
	args->buf = buf;
	args->count = count;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_WRITE;

}

VOID AfterWrite(THREADID tid, WriteArgs *args, ssize_t ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	std::string encoded_result;
	if (record_value) {
		encoded_result = base64_encode((char *)args->buf, args->count);
		if (encoded_result[encoded_result.size()-1] == '\n') {
			fprintf(out, "%lu,%d,WRITE,%d,%s,%lu,%s;", timestamp, tid, args->fd, args->filename, args->count, encoded_result.substr(0, encoded_result.size()-1).c_str());
		}
		else {
			fprintf(out, "%lu,%d,WRITE,%d,%s,%lu,%s;", timestamp, tid, args->fd, args->filename, args->count, encoded_result.c_str());
		}
	}
	else {
		fprintf(out, "%lu,%d,WRITE,%d,%s,%lu,;", timestamp, tid, args->fd, args->filename, args->count);
	
	}
	// fprintf(out, "%lu,%d,WRITE,%s,%lu,%s;\n", timestamp, tid, args->filename, args->count, encoded_result.substr(0, encoded_result.size()-1).c_str());
	// fprintf(out, "encoded_result: %s|||\n", encoded_result.c_str());
	// // hexdump of args->buf
	// fprintf(out, "hexdump of args->buf before encode \n");
	// for (size_t i = 0; i < args->count; i++) {
	// 	fprintf(out, "%02x", ((char *)args->buf)[i]);
	// }
	// char * decode_result = base64_decode((encoded_result.substr(0, encoded_result.size()-1)+"\n").c_str(), encoded_result.size());
	// fprintf(out, "\nhexdump of args->buf after decode \n");
	// for (size_t i = 0; i < args->count; i++) {
	// 	fprintf(out, "%02x", decode_result[i]);
	// }
	// fprintf(out, "\n");
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	if (record_value) {
		if (encoded_result[encoded_result.size()-1] == '\n') {
			printf("%lu,%d,WRITE,%d,%s,%lu,%s;", timestamp, tid, args->fd, args->filename, args->count, encoded_result.substr(0, encoded_result.size()-1).c_str());
		}
		else {
			printf("%lu,%d,WRITE,%d,%s,%lu,%s;", timestamp, tid, args->fd, args->filename, args->count, encoded_result.c_str());
		}
	}
	else {
		printf("%lu,%d,WRITE,%d,%s,%lu,;", timestamp, tid, args->fd, args->filename, args->count);
	
	}
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeWritev(THREADID tid, int fd, const struct iovec *iov, int iovcnt) 
{
	WritevArgs *args;

	args = (WritevArgs *)malloc(sizeof(WritevArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;
	args->iov = iov;
	args->iovcnt = iovcnt;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_WRITEV;

}

VOID AfterWritev(THREADID tid, WritevArgs *args, ssize_t ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	if (record_value) {
		fprintf(out, "%lu,%d,WRITEV,%d,%s,%d", timestamp, tid, args->fd, args->filename, args->iovcnt);
		for (int i = 0; i < args->iovcnt; i++) {
			std::string encoded_result = base64_encode((char *)args->iov[i].iov_base, args->iov[i].iov_len);
			if (encoded_result[encoded_result.size()-1] == '\n') {
				fprintf(out, ",%lu,%s", args->iov[i].iov_len, encoded_result.substr(0, encoded_result.size()-1).c_str());
			}
			else {
				fprintf(out, ",%lu,%s", args->iov[i].iov_len, encoded_result.c_str());
			}
		}
		fprintf(out, ";");
	}
	else {
		fprintf(out, "%lu,%d,WRITEV,%d,%s,%d", timestamp, tid, args->fd, args->filename, args->iovcnt);
		for (int i = 0; i < args->iovcnt; i++) {
			fprintf(out, ",%lu,", args->iov[i].iov_len);
		}
		fprintf(out, ";");
	}
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	#if DEBUG
	printf("%lu,%d,WRITEV,%d,%s,%d \n", timestamp, tid, args->fd, args->filename, args->iovcnt);
	#endif
	fflush(out);
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeLseek(THREADID tid, int fd, off_t offset, int whence) {
	LseekArgs *args;

	args = (LseekArgs *)malloc(sizeof(LseekArgs));	
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;
	args->offset = offset;
	args->whence = whence;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_LSEEK;

}

VOID AfterLseek(THREADID tid, LseekArgs *args, off_t ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,LSEEK,%d,%s,%ld,%d;", timestamp, tid, args->fd, args->filename, args->offset, args->whence);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,LSEEK,%d,%s,%ld,%d;", timestamp, tid, args->fd, args->filename, args->offset, args->whence);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeRename(THREADID tid, const char *oldpath, const char *newpath) {
	RenameArgs *args;

	std::string sanitized_oldpath = sanitize_filename(std::string(oldpath));
	std::string sanitized_newpath = sanitize_filename(std::string(newpath));
	if (sanitized_oldpath != oldpath) {
		oldpath = sanitized_oldpath.c_str();
	}
	if (sanitized_newpath != newpath) {
		newpath = sanitized_newpath.c_str();
	}
	std::string old_path_updated, new_path_updated;
	if (oldpath[0] != '/') {
		old_path_updated = (std::filesystem::path(at_fdcwd) / std::filesystem::path(oldpath)).string();
		oldpath = old_path_updated.c_str();
	}
	if (newpath[0] != '/') {
		new_path_updated = (std::filesystem::path(at_fdcwd) / std::filesystem::path(newpath)).string();
		newpath = new_path_updated.c_str();
	}
	
	if (target_filename != "" && strstr(oldpath, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (RenameArgs *)malloc(sizeof(RenameArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->oldpath, oldpath);
	strcpy(args->newpath, newpath);

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_RENAME;
}

VOID AfterRename(THREADID tid, RenameArgs *args, int ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,RENAME,%s,%s;", timestamp, tid, args->oldpath, args->newpath);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,RENAME,%s,%s;", timestamp, tid, args->oldpath, args->newpath);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeUnlink(THREADID tid, const char *filename) {
	UnlinkArgs *args;
	std::string filename_updated;
	if (filename[0] != '/') {
		filename_updated = (std::filesystem::path(at_fdcwd) / std::filesystem::path(filename)).string();
		filename = filename_updated.c_str();
	}

	if (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (UnlinkArgs *)malloc(sizeof(UnlinkArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_UNLINK;
}

VOID AfterUnlink(THREADID tid, UnlinkArgs *args, int ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,UNLINK,%s;", timestamp, tid, args->filename);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,UNLINK,%s;", timestamp, tid, args->filename);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeFsync(THREADID tid, int fd) {
	FsyncArgs *args;

	args = (FsyncArgs *)malloc(sizeof(FsyncArgs));	
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_FSYNC;

}

VOID AfterFsync(THREADID tid, FsyncArgs *args, int ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,FSYNC,%d,%s;", timestamp, tid, args->fd, args->filename);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,FSYNC,%d,%s;", timestamp, tid, args->fd, args->filename);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeFdatasync(THREADID tid, int fd) {
	FdatasyncArgs *args;

	args = (FdatasyncArgs *)malloc(sizeof(FdatasyncArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_FDATASYNC;

}

VOID AfterFdatasync(THREADID tid, FdatasyncArgs *args, int ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,FDATASYNC,%d,%s;", timestamp, tid, args->fd, args->filename);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,FDATASYNC,%d,%s;", timestamp, tid, args->fd, args->filename);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeFallocate(THREADID tid, int fd, int mode, off_t offset, off_t len) {
	FallocateArgs *args;

	args = (FallocateArgs *)malloc(sizeof(FallocateArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;
	args->mode = mode;
	args->offset = offset;
	args->len = len;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_FALLOCATE;

}

VOID AfterFallocate(THREADID tid, FallocateArgs *args, int ret, CONTEXT *ctxt) {
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret == -1) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,FALLOCATE,%d,%s,%d,%ld,%ld;", timestamp, tid, args->fd, args->filename, args->mode, args->offset, args->len);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,FALLOCATE,%d,%s,%d,%ld,%ld;", timestamp, tid, args->fd, args->filename, args->mode, args->offset, args->len);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeOpen(THREADID tid, const char *filename, int flags, mode_t mode) {
	OpenArgs *args;

	std::string sanitized_filename = sanitize_filename(std::string(filename));
	if (sanitized_filename != filename) {
		filename = sanitized_filename.c_str();
	}

	// if flag is O_RDONLY and not directory, then skip
	if (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (OpenArgs *)malloc(sizeof(OpenArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);
	args->flags = flags;
	args->mode = mode;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_OPEN;
}

VOID AfterOpen(THREADID tid, OpenArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	// if open with O_APPEND, get file size
	if (args->flags & O_APPEND) {
		struct stat st;
		if (fstat(ret, &st) == -1) {
			fprintf(out, "[%d] fstat() failed (ERROR)\n", tid);
			fflush(out);
			free(args);
			exit(EXIT_FAILURE);
		}
		fprintf(out, "%lu,%d,OPEN,%s,%d,%d,%d,%ld;", timestamp, tid, args->filename, args->flags, args->mode, ret, st.st_size);
	}
	else {
		fprintf(out, "%lu,%d,OPEN,%s,%d,%d,%d,%ld;", timestamp, tid, args->filename, args->flags, args->mode, ret, (long int)-1);
	}
	// fprintf(out, "%lu,%d,OPEN,%s,%d,%d,%d;", timestamp, tid, args->filename, args->flags, args->mode, ret);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,OPEN,%s,%d,%d;", timestamp, tid, args->filename, args->flags, args->mode);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeCreat(THREADID tid, const char *filename, mode_t mode) {
	CreatArgs *args;

	if (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (CreatArgs *)malloc(sizeof(CreatArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);
	args->mode = mode;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_CREAT;
}

VOID AfterCreat(THREADID tid, CreatArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,CREAT,%s,%d,%d;", timestamp, tid, args->filename, args->mode, ret);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,CREAT,%s,%d,%d;", timestamp, tid, args->filename, args->mode, ret);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeOpenat(THREADID tid, int dirfd, const char *filename, int flags, mode_t mode) {
	// TODO: support AT_FDCWD
	// If filename is relative and dirfd is the special value AT_FDCWD, then filename is interpreted relative to the current working directory of the calling process (like open(2)).
	OpenatArgs *args;

	args = (OpenatArgs *)malloc(sizeof(OpenatArgs));

	std::string sanitized_filename = sanitize_filename(std::string(filename));
	if (sanitized_filename != filename) {
		filename = sanitized_filename.c_str();
	}

	// check if filename is absolute or relative, if absolute, we do not interpret dirfd
	if (filename[0] == '/') {
		strcpy(args->filename, filename);
	}
	else if (dirfd == AT_FDCWD) {
		// get current working dir
		char cwd[FILENAME_SIZE];
		if (getcwd(cwd, sizeof(cwd)) == NULL) {
			PIN_GetLock(&pinLock, tid+1);
			fprintf(out, "[%d]: getcwd() failed (ERROR)\n", tid);
			fflush(out);
			PIN_ReleaseLock(&pinLock);
			free(args);
			exit(EXIT_FAILURE);
		}
		if (filename[0] == '.') {
			// if filename is ., then just copy cwd
			strcpy(args->filename, cwd);
		}
		else {
			// concatenate cwd and filename
			strcat(cwd, "/");
			strcat(cwd, filename);
			strcpy(args->filename, cwd);
		}
	}
	else {
		fd_to_filename(tid, dirfd, args->filename);
		// concatenate dirfd and filename
		strcat(args->filename, "/");
		strcat(args->filename, filename);
	}

	// if flag is O_RDONLY and not directory, then skip (flags&O_ACCMODE) == O_RDONLY
	// update, I find it is actually hard to keep track files that are read-only and also remove "close" correspondingly, so just keep it for now, we can optimize in pathfinder by removing open/close pairs
	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		free(args);
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->dirfd = dirfd;
	args->flags = flags;
	args->mode = mode;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_OPENAT;
}

VOID AfterOpenat(THREADID tid, OpenatArgs *args, int ret, CONTEXT *ctxt) {

	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}

	PIN_GetLock(&pinLock, tid+1);
	// if open with O_APPEND, get file size
	if (args->flags & O_APPEND) {
		struct stat st;
		if (fstat(ret, &st) == -1) {
			fprintf(out, "[%d] fstat() failed (ERROR)\n", tid);
			fflush(out);
			free(args);
			exit(EXIT_FAILURE);
		}
		fprintf(out, "%lu,%d,OPEN,%s,%d,%d,%d,%ld;", timestamp, tid, args->filename, args->flags, args->mode, ret, st.st_size);
	}
	else {
		fprintf(out, "%lu,%d,OPEN,%s,%d,%d,%d,%ld;", timestamp, tid, args->filename, args->flags, args->mode, ret, (long int)-1);
	}
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,OPEN,%s,%d,%d,%d;", timestamp, tid, args->filename, args->flags, args->mode, ret);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeClose(THREADID tid, int fd) {
	CloseArgs *args;

	args = (CloseArgs *)malloc(sizeof(CloseArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_CLOSE;

}

VOID AfterClose(THREADID tid, CloseArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}

	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,CLOSE,%d,%s;", timestamp, tid, args->fd, args->filename);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,CLOSE,%d,%s;", timestamp, tid, args->fd, args->filename);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeMkdir(THREADID tid, const char *filename, mode_t mode) {
	MkdirArgs *args;

	if (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (MkdirArgs *)malloc(sizeof(MkdirArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);
	args->mode = mode;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_MKDIR;
}

VOID AfterMkdir(THREADID tid, MkdirArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,MKDIR,%s,%d;", timestamp, tid, args->filename, args->mode);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,MKDIR,%s,%d;", timestamp, tid, args->filename, args->mode);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

// TODO: implement full mkdirat syscall
VOID BeforeMkdirat(THREADID tid, int dirfd, const char *filename, mode_t mode) {
	MkdiratArgs *args;

	if (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (MkdiratArgs *)malloc(sizeof(MkdiratArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	args->dirfd = dirfd;
	strcpy(args->filename, filename);
	args->mode = mode;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_MKDIRAT;
}

VOID AfterMkdirat(THREADID tid, MkdiratArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,MKDIRAT,%d,%s,%d;", timestamp, tid, args->dirfd, args->filename, args->mode);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,MKDIRAT,%d,%s,%d;", timestamp, tid, args->dirfd, args->filename, args->mode);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeRmdir(THREADID tid, const char *filename) {
	RmdirArgs *args;

	if (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (RmdirArgs *)malloc(sizeof(RmdirArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_RMDIR;
}

VOID AfterRmdir(THREADID tid, RmdirArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,RMDIR,%s;", timestamp, tid, args->filename);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,RMDIR,%s;", timestamp, tid, args->filename);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeSync(THREADID tid) {
	SyncArgs *args;

	args = (SyncArgs *)malloc(sizeof(SyncArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_SYNC;
}

VOID AfterSync(THREADID tid, SyncArgs *args, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,SYNC;", timestamp, tid);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,SYNC;", timestamp, tid);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeSyncfs(THREADID tid, int fd) {
	SyncfsArgs *args;

	args = (SyncfsArgs *)malloc(sizeof(SyncfsArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_SYNCFS;

}

VOID AfterSyncfs(THREADID tid, SyncfsArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,SYNCFS,%d,%s;", timestamp, tid, args->fd, args->filename);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,SYNCFS,%d,%s;", timestamp, tid, args->fd, args->filename);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeSyncFileRange(THREADID tid, int fd, off64_t offset, off64_t nbytes, unsigned int flags) {
	SyncFileRangeArgs *args;

	args = (SyncFileRangeArgs *)malloc(sizeof(SyncFileRangeArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;
	args->offset = offset;
	args->nbytes = nbytes;
	args->flags = flags;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_SYNC_FILE_RANGE;

}

VOID AfterSyncFileRange(THREADID tid, SyncFileRangeArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	PIN_GetLock(&pinLock, tid+1);
	fprintf(out, "%lu,%d,SYNC_FILE_RANGE,%d,%s,%ld,%ld,%d;", timestamp, tid, args->fd, args->filename, args->offset, args->nbytes, args->flags);
	#if PRINT_BACKTRACE
	printBacktrace(ctxt);
	#else
	fprintf(out, "\n");
	#endif
	fflush(out);
	#if DEBUG
	printf("%lu,%d,SYNC_FILE_RANGE,%d,%s,%ld,%ld,%d;", timestamp, tid, args->fd, args->filename, args->offset, args->nbytes, args->flags);
	#endif
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeChdir(THREADID tid, const char *path) {
	ChdirArgs *args;

	if (target_filename != "" && strstr(path, target_filename.c_str()) == NULL) {
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	args = (ChdirArgs *)malloc(sizeof(ChdirArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, path);

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_CHDIR;
}

VOID AfterChdir(THREADID tid, ChdirArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	// PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%lu,%d,CHDIR,%s;", timestamp, tid, args->path);
	// #if PRINT_BACKTRACE
	// printBacktrace(ctxt);
	// #else
	// fprintf(out, "\n");
	// #endif
	// fflush(out);
	// #if DEBUG
	// printf("%lu,%d,CHDIR,%s;", timestamp, tid, args->path);
	// #endif
	PIN_RWMutexWriteLock(&RWMutex);
	at_fdcwd = args->filename;
	PIN_RWMutexUnlock(&RWMutex);
	// PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID BeforeFchdir(THREADID tid, int fd) {
	FchdirArgs *args;

	args = (FchdirArgs *)malloc(sizeof(FchdirArgs));
	fd_to_filename(tid, fd, args->filename);

	if (target_filename != "" && strstr(args->filename, target_filename.c_str()) == NULL) {
		free(args);
		threadArray[tid].syscallArgs = NULL;
		threadArray[tid].sType = S_SKIP;
		return;
	}

	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "[%d] %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		free(args);
		exit(EXIT_FAILURE);
	}

	args->fd = fd;

	threadArray[tid].syscallArgs = (void *)args;
	threadArray[tid].sType = S_FCHDIR;
}

VOID AfterFchdir(THREADID tid, FchdirArgs *args, int ret, CONTEXT *ctxt) {
	// if no error, then continue
	if (threadArray[tid].sType == S_SKIP) {
		return;
	}
	if (ret < 0) {
		free(args);
		return;
	}
	// PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%lu,%d,FCHDIR,%d,%s;", timestamp, tid, args->fd, args->filename);
	// #if PRINT_BACKTRACE
	// printBacktrace(ctxt);
	// #else
	// fprintf(out, "\n");
	// #endif
	// fflush(out);
	// #if DEBUG
	// printf("%lu,%d,FCHDIR,%d,%s;", timestamp, tid, args->fd, args->filename);
	// #endif
	PIN_RWMutexWriteLock(&RWMutex);
	at_fdcwd = args->filename;
	PIN_RWMutexUnlock(&RWMutex);
	// PIN_ReleaseLock(&pinLock);
	free(args);
}

VOID SyscallEntry(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v)
{
	int number = (int)PIN_GetSyscallNumber(ctxt, std);
	// std::cout << "Syscall starts, syscall number: " << number << std::endl;
	if (number == __NR_mmap) {
		// std::cout << "mmap begins" << std::endl;
		BeforeMmap(tid, (unsigned long)PIN_GetSyscallArgument(ctxt, std, 0),
				(size_t)PIN_GetSyscallArgument(ctxt, std, 1),
				(int)PIN_GetSyscallArgument(ctxt, std, 2),
				(int)PIN_GetSyscallArgument(ctxt, std, 3),
				(int)PIN_GetSyscallArgument(ctxt, std, 4),
				(off_t)PIN_GetSyscallArgument(ctxt, std, 5));
	}
	else if (number == __NR_munmap) {
		BeforeMunmap(tid, (unsigned long)PIN_GetSyscallArgument(ctxt, std, 0),
				(size_t)PIN_GetSyscallArgument(ctxt, std, 1));
	}
	else if (number == __NR_msync) {
		BeforeMsync(tid, (unsigned long)PIN_GetSyscallArgument(ctxt, std, 0),
				(size_t)PIN_GetSyscallArgument(ctxt, std, 1),
				(int)PIN_GetSyscallArgument(ctxt, std, 2));
	}
	else if (number == __NR_ftruncate) {
		// std::cout << "ftruncate begins" << std::endl;
		BeforeFtruncate(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(off_t)PIN_GetSyscallArgument(ctxt, std, 1));
	}
	else if (number == __NR_pwrite64) {
		// std::cout << "pwrite64 begins" << std::endl;
		BeforePwrite64(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(const void *)PIN_GetSyscallArgument(ctxt, std, 1),
				(size_t)PIN_GetSyscallArgument(ctxt, std, 2),
				(off_t)PIN_GetSyscallArgument(ctxt, std, 3));
	}
	else if (number == __NR_write) {
		// std::cout << "write begins" << std::endl;
		BeforeWrite(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(const void *)PIN_GetSyscallArgument(ctxt, std, 1),
				(size_t)PIN_GetSyscallArgument(ctxt, std, 2));
	}
	else if (number == __NR_writev) {
		// std::cout << "writev begins" << std::endl;
		BeforeWritev(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(const struct iovec *)PIN_GetSyscallArgument(ctxt, std, 1),
				(int)PIN_GetSyscallArgument(ctxt, std, 2));
	}
	else if (number == __NR_pwritev) {
		std::cerr << "pwritev is not supported. Fix this!" << std::endl;
		exit(EXIT_FAILURE);
	}
	// else if (number == __NR_pwritev2) {
	// 	std::cerr << "pwritev2 is not supported. Fix this!" << std::endl;
	// 	exit(EXIT_FAILURE);
	// }
	else if (number == __NR_lseek) {
		// std::cout << "lseek begins" << std::endl;
		BeforeLseek(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(off_t)PIN_GetSyscallArgument(ctxt, std, 1),
				(int)PIN_GetSyscallArgument(ctxt, std, 2));
	}
	else if (number == __NR_rename) {
		BeforeRename(tid, (const char *)PIN_GetSyscallArgument(ctxt, std, 0),
				(const char *)PIN_GetSyscallArgument(ctxt, std, 1));
	}
	else if (number == __NR_unlink) {
		BeforeUnlink(tid, (const char *)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	else if (number == __NR_fsync) {
		BeforeFsync(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	else if (number == __NR_fdatasync) {
		BeforeFdatasync(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	else if (number == __NR_fallocate) {
		BeforeFallocate(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(int)PIN_GetSyscallArgument(ctxt, std, 1),
				(off_t)PIN_GetSyscallArgument(ctxt, std, 2),
				(off_t)PIN_GetSyscallArgument(ctxt, std, 3));
	}
	else if (number == __NR_open) {
		BeforeOpen(tid, (const char *)PIN_GetSyscallArgument(ctxt, std, 0),
				(int)PIN_GetSyscallArgument(ctxt, std, 1), (int)PIN_GetSyscallArgument(ctxt, std, 2));
	}
	else if (number == __NR_creat) {
		BeforeCreat(tid, (const char *)PIN_GetSyscallArgument(ctxt, std, 0),
				(mode_t)PIN_GetSyscallArgument(ctxt, std, 1));
	}
	else if (number == __NR_openat) {
		BeforeOpenat(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(const char *)PIN_GetSyscallArgument(ctxt, std, 1),
				(int)PIN_GetSyscallArgument(ctxt, std, 2),
				(int)PIN_GetSyscallArgument(ctxt, std, 3));
	}
	else if (number == __NR_close) {
		BeforeClose(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	else if (number == __NR_mkdir) {
		BeforeMkdir(tid, (const char *)PIN_GetSyscallArgument(ctxt, std, 0),
				(mode_t)PIN_GetSyscallArgument(ctxt, std, 1));
	}
	// else if (number == __NR_mkdirat) {
	// 	BeforeMkdirat(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
	// 			(const char *)PIN_GetSyscallArgument(ctxt, std, 1),
	// 			(mode_t)PIN_GetSyscallArgument(ctxt, std, 2));
	// }	
	else if (number == __NR_rmdir) {
		BeforeRmdir(tid, (const char *)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	else if (number == __NR_sync) {
		BeforeSync(tid);
	}
	else if (number == __NR_syncfs) {
		BeforeSyncfs(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	else if (number == __NR_sync_file_range) {
		BeforeSyncFileRange(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0),
				(off64_t)PIN_GetSyscallArgument(ctxt, std, 1),
				(off64_t)PIN_GetSyscallArgument(ctxt, std, 2),
				(unsigned int)PIN_GetSyscallArgument(ctxt, std, 3));
	}
	else if (number == __NR_chdir) {
		BeforeChdir(tid, (const char *)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	else if (number == __NR_fchdir) {
		BeforeFchdir(tid, (int)PIN_GetSyscallArgument(ctxt, std, 0));
	}
	
}

VOID SyscallExit(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v)
{
	void *args;
	SyscallType type;

	// int number = (int)PIN_GetSyscallNumber(ctxt, std);
	// std::cout << "Syscall exits, syscall number: " << number << std::endl;

	/*
	PIN_GetLock(&pinLock, tid+1);
	args = getSyscallArgs(tid, &type);
	PIN_ReleaseLock(&pinLock);
	*/
	args = (void *)(threadArray[tid].syscallArgs);
	type = threadArray[tid].sType;

	threadArray[tid].syscallArgs = NULL;
	threadArray[tid].sType = S_NONE;

	if (args == NULL) {
		return;
	}

	switch (type) {
		case S_MMAP:
			// std::cout << "mmap ends" << std::endl;
			AfterMmap(tid, (MmapArgs *)args, (void*)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_MUNMAP:
			AfterMunmap(tid, (MunmapArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_MSYNC:
			AfterMsync(tid, (MsyncArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_FTRUNCATE:
			AfterFtruncate(tid, (FtruncateArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_PWRITE64:
			AfterPwrite64(tid, (Pwrite64Args *)args, (ssize_t)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_WRITE:
			AfterWrite(tid, (WriteArgs *)args, (ssize_t)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_WRITEV:
			AfterWritev(tid, (WritevArgs *)args, (ssize_t)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_LSEEK:
			AfterLseek(tid, (LseekArgs *)args, (off_t)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_RENAME:
			AfterRename(tid, (RenameArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_UNLINK:
			AfterUnlink(tid, (UnlinkArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_FSYNC:
			AfterFsync(tid, (FsyncArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_FDATASYNC:
			AfterFdatasync(tid, (FdatasyncArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_FALLOCATE:
			AfterFallocate(tid, (FallocateArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_CREAT:
			AfterCreat(tid, (CreatArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_OPEN:
			AfterOpen(tid, (OpenArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_OPENAT:
			AfterOpenat(tid, (OpenatArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_CLOSE:
			AfterClose(tid, (CloseArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_MKDIR:
			AfterMkdir(tid, (MkdirArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		// case S_MKDIRAT:
		// 	AfterMkdirat(tid, (MkdiratArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
		// 	break;
		case S_RMDIR:
			AfterRmdir(tid, (RmdirArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_SYNC:
			AfterSync(tid, (SyncArgs *)args, ctxt);
			break;
		case S_SYNCFS:
			AfterSyncfs(tid, (SyncfsArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_SYNC_FILE_RANGE:
			AfterSyncFileRange(tid, (SyncFileRangeArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_CHDIR:
			AfterChdir(tid, (ChdirArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		case S_FCHDIR:
			AfterFchdir(tid, (FchdirArgs *)args, (int)PIN_GetSyscallReturn(ctxt, std), ctxt);
			break;
		default:
			// PIN_GetLock(&pinLock, tid+1);
			// fprintf(out, "%d, %s: The args type is incorrect. (ERROR)\n", tid, __func__);
			// fflush(out);
			// PIN_ReleaseLock(&pinLock);
			// exit(EXIT_FAILURE);
            return;
	}
}

VOID BeforeMemcpy(ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, THREADID tid)
{
	MemcpyArgs *args;
	unsigned long pgoff;
	char *filename;

	unsigned long dst = (unsigned long)arg0;
	unsigned long src = (unsigned long)arg1;
	size_t length = (size_t)arg2;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	filename = findMemNode(dst, &pgoff, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (filename == NULL || (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL)) {
		threadArray[tid].libcallArgs = NULL;
		threadArray[tid].lType = L_SKIP;
		return;
	}

	args = (MemcpyArgs *)malloc(sizeof(MemcpyArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	strcpy(args->filename, filename);
	args->dst = dst;
	args->src = src;
	args->length = length;

	threadArray[tid].libcallArgs = (void *)args;
	threadArray[tid].lType = L_MEMCPY;

	// PIN_GetLock(&pinLock, tid+1);
	// //putLibcallArgs(tid, (void *)args, L_MEMCPY);
	// fprintf(out, "%d, memcpy-call, 0x%lx, 0x%lx, %lu, %s\n", tid, dst, src, length, filename);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
}

VOID AfterMemcpy(ADDRINT ret, THREADID tid) {
	MemcpyArgs *args;
	LibcallType type;

	/*
	PIN_GetLock(&pinLock, tid+1);
	args = (MemcpyArgs *)getLibcallArgs(tid, &type);
	PIN_ReleaseLock(&pinLock);
	*/
	args = (MemcpyArgs *)(threadArray[tid].libcallArgs);
	type = threadArray[tid].lType;
	threadArray[tid].libcallArgs = NULL;
	threadArray[tid].lType = L_NONE;

	if (args == NULL)
		return;

	if (type == L_MEMCPY) {
		// PIN_GetLock(&pinLock, tid+1);
		// fprintf(out, "%d, memcpy-return, 0x%lx\n", tid, (unsigned long)ret);
		// fflush(out);
		// PIN_ReleaseLock(&pinLock);
	}
	else {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: LibcallType is wrong.\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}
}

VOID BeforeMemset(ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, THREADID tid)
{
	MemsetArgs *args;
	unsigned long pgoff;
	char *filename;

	unsigned long addr = (unsigned long)arg0;
	int c = (int)arg1;
	size_t length = (size_t)arg2;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	filename = findMemNode(addr, &pgoff, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (filename == NULL || (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL)) {
		threadArray[tid].libcallArgs = NULL;
		threadArray[tid].lType = L_SKIP;
		return;
	}

	args = (MemsetArgs *)malloc(sizeof(MemsetArgs));
	if (args == NULL) {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: malloc() failed (ERROR)\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}

	if (filename != NULL) strcpy(args->filename, filename);
	args->addr = addr;
	args->c = c;
	args->length = length;

	threadArray[tid].libcallArgs = (void *)args;
	threadArray[tid].lType = L_MEMSET;

	// PIN_GetLock(&pinLock, tid+1);
	// //putLibcallArgs(tid, (void *)args, L_MEMSET);
	// if (filename != NULL) fprintf(out, "%d, memset-call, 0x%lx, %d, %lu, %s\n", tid, addr, c, length, filename);
	// else fprintf(out, "%d, memset-call, 0x%lx, %d, %lu, \n", tid, addr, c, length);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
}

VOID AfterMemset(ADDRINT ret, THREADID tid) {
	MemsetArgs *args;
	LibcallType type;

	/*
	PIN_GetLock(&pinLock, tid+1);
	args = (MemsetArgs *)getLibcallArgs(tid, &type);
	PIN_ReleaseLock(&pinLock);
	*/
	args = (MemsetArgs *)(threadArray[tid].libcallArgs);
	type = threadArray[tid].lType;
	threadArray[tid].libcallArgs = NULL;
	threadArray[tid].lType = L_NONE;

	if (args == NULL)
		return;

	if (type == L_MEMSET) {
		// PIN_GetLock(&pinLock, tid+1);
		// fprintf(out, "%d, memset-return, 0x%lx\n", tid, (unsigned long)ret);
		// fflush(out);
		// PIN_ReleaseLock(&pinLock);
	}
	else {
		PIN_GetLock(&pinLock, tid+1);
		fprintf(out, "%d, %s: LibcallType is wrong.\n", tid, __func__);
		fflush(out);
		PIN_ReleaseLock(&pinLock);
		exit(EXIT_FAILURE);
	}
}

VOID BeforeMain(ADDRINT arg0, ADDRINT arg1, THREADID tid) {
	// MainArgs *args;

	// args = (MainArgs *)malloc(sizeof(MainArgs));
	// if (args == NULL) {
	// 	PIN_GetLock(&pinLock, tid+1);
	// 	fprintf(out, "%d, %s: malloc() failed (ERROR)\n", tid, __func__);
	// 	fflush(out);
	// 	PIN_ReleaseLock(&pinLock);
	// 	exit(EXIT_FAILURE);
	// }

	// args->argc = (int)arg0;
	// args->argv = (char **)arg1;

	// threadArray[tid].libcallArgs = (void *)args;
	// threadArray[tid].lType = L_MAIN;

	// PIN_GetLock(&pinLock, tid+1);
	// //putLibcallArgs(tid, (void *)args, L_MAIN);
	// fprintf(out, "%d, main-call \n", tid);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
}

VOID AfterMain(ADDRINT ret, THREADID tid) {
	// MainArgs *args;
	// LibcallType type;

	/*
	PIN_GetLock(&pinLock, tid+1);
	args = (MainArgs *)getLibcallArgs(tid, &type);
	PIN_ReleaseLock(&pinLock);
	*/
	// args = (MainArgs *)(threadArray[tid].libcallArgs);
	// type = threadArray[tid].lType;
	// threadArray[tid].libcallArgs = NULL;
	// threadArray[tid].lType = L_NONE;

	// if (args == NULL)
	// 	return;

	// if (type == L_MAIN) {
	// PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%d, main-return, %d\n", tid, (int)ret);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
	// }
	// else {
	// 	PIN_GetLock(&pinLock, tid+1);
	// 	fprintf(out, "%d, %s: LibcallType is wrong.\n", tid, __func__);
	// 	fflush(out);
	// 	PIN_ReleaseLock(&pinLock);
	// 	exit(EXIT_FAILURE);
	// }
}

// Print a memory read record
VOID RecordMemRead(CONTEXT * ctx, VOID * ip, VOID * addr, THREADID tid)
{
	unsigned long pgoff;
	char *filename;
	unsigned long ip_addr = (unsigned long)ip;
	unsigned long mem_addr = (unsigned long)addr;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	filename = findMemNode(mem_addr, &pgoff, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (filename == NULL || (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL))
		return;

	PIN_GetLock(&pinLock, tid+1);
	// fprintf(out, "%d, READ, %s, 0x%lx, 0x%lx, %lu, \n", tid, filename, ip_addr, mem_addr, pgoff);

	// get value from mem_addr and print it
	char value = *(char *)addr;
	fprintf(out, "%d, READ, %s, 0x%lx, 0x%lx, %lu, %c\n", tid, filename, ip_addr, mem_addr, pgoff, value);

	// printBacktrace(ctx);
	fflush(out);
	PIN_ReleaseLock(&pinLock);
}

// Print a memory write record
VOID RecordMemWrite(CONTEXT * ctx, VOID * ip, VOID * addr, uint32_t size, THREADID tid)
{
	unsigned long pgoff;
	char *filename;
	// unsigned long ip_addr = (unsigned long)ip;
	unsigned long mem_addr = (unsigned long)addr;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	filename = findMemNode(mem_addr, &pgoff, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (filename == NULL || (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL))
		return;

	threadArray[tid].mem_addr = (void *)mem_addr;
	threadArray[tid].mem_size = size;
	// char value = *(char *)actual_addr;
    // INS ins;
    // ins.q_set(insarg);
    // if (INS_MemoryOperandCount(ins) == 0 || !INS_OperandIsMemory(ins, 0))
    //   return;
    // // clog << "Disassembly: " << INS_Disassemble(ins) << " W " << size << endl;
	// PIN_GetLock(&pinLock, tid+1);
    // PIN_REGISTER reg_val;
    // PIN_GetContextRegval(ctx, INS_OperandReg(ins, 1), reinterpret_cast<UINT8 *>(&reg_val));

    // if (size == 1)
    // {
    // //   char value = *(reg_val.byte);
	//   fprintf(out, "%d, WRITE, %s, 0x%lx, 0x%lx, %lu, %p \n", tid, filename, ip_addr, mem_addr, pgoff, reg_val.byte);
	//   printBacktrace(ctx);
    // }
    // else if (size == 2)
    // {
    //   Data data = new_data(size, (uint8_t *)reg_val.word);
    //   store_aligned(cache, reinterpret_cast<UINT64>(addr), data);
    // }
    // else if (size == 4)
    // {
    //   Data data = new_data(size, (uint8_t *)reg_val.dword);
    //   store_aligned(cache, reinterpret_cast<UINT64>(addr), data);
    // }
    // else if (size == 8)
    // {
    //   Data data = new_data(size, (uint8_t *)reg_val.qword);
    //   store_aligned(cache, reinterpret_cast<UINT64>(addr), data);
    // }
    // else if (size >= 16 && size <= 64)
    // {
    //   assert(size % 8 == 0);
    //   for (uint32_t i = 0; i < size / 8; i++)
    //   {
    //     Data data = new_data(8, (uint8_t *)&reg_val.qword[i]);
    //     store_aligned(cache, reinterpret_cast<UINT64>(addr + i * 8), data);
    //   }
    // }
    // else
    // {
    //   clog << "Unsupported size: " << size << endl;
    // }
	// fprintf(out, "%d, WRITE, %s, 0x%lx, 0x%lx, %lu, %u\n", tid, filename, ip_addr, mem_addr, pgoff, size);
	// printBacktrace(ctx);
	// fflush(out);
	// PIN_ReleaseLock(&pinLock);
}

VOID PrintMemWrite(CONTEXT * ctx, THREADID tid) {
	if (threadArray[tid].mem_addr == NULL)
		return;
	unsigned long pgoff;
	char *filename;
	unsigned long mem_addr = (unsigned long)threadArray[tid].mem_addr;

	//PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	filename = findMemNode(mem_addr, &pgoff, tid);
	//PIN_ReleaseLock(&pinLock);
	PIN_RWMutexUnlock(&RWMutex);

	if (filename == NULL || (target_filename != "" && strstr(filename, target_filename.c_str()) == NULL))
		return;

	PIN_GetLock(&pinLock, tid+1);
	// unsigned long ip_addr = (unsigned long)PIN_GetContextReg(ctx, REG_INST_PTR);
	uint32_t size = threadArray[tid].mem_size;
	// std::string result((char *)mem_addr, size);
	if (record_value) {
		std::string encoded_result = base64_encode((char *)mem_addr, size);
		// TODO: do we need page offset here?
		fprintf(out, "%lu,%d,STORE,%lu,%s,0x%lx,%u,%s;", timestamp, tid, store_id, filename, mem_addr, size, encoded_result.substr(0, encoded_result.size()-1).c_str());
	}
	else {
		fprintf(out, "%lu,%d,STORE,%lu,%s,0x%lx,%u,;", timestamp, tid, store_id, filename, mem_addr, size);
	}
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	store_id++;

	// // if filename contains testdb then print backtrace
	// if (strstr(filename, "testdb") != NULL) {
	// 	printBacktrace(ctx);
	// }

	printBacktrace(ctx);
	fflush(out);
	PIN_ReleaseLock(&pinLock);
	threadArray[tid].mem_addr = NULL;
}

// Capture the return address of the ifunc which is the address of the actual memcpy
VOID * IfuncMemcpyWrapper(CONTEXT * context, AFUNPTR orgFuncptr, THREADID tid)
{
	VOID * ret;

	PIN_CallApplicationFunction( context, PIN_ThreadId(),
			CALLINGSTD_DEFAULT, orgFuncptr,
			NULL, PIN_PARG(void *), &ret,
			PIN_PARG_END() );
	
	//actual_memcpy_add[ifunc_index++] = (ADDRINT)ret;
	printf("%d, ifunc_memcpy() return 0x%lx\n", tid, (unsigned long)ret);
	return ret;
}

VOID BeforePathfinderOpBegin(THREADID tid, ADDRINT workload_tid, ADDRINT op_count) {
	PIN_GetLock(&pinLock, tid+1);
	PIN_RWMutexWriteLock(&RWMutex);
	// if (op_count.find(tid) == op_count.end()) {
	// 	op_count[tid] = 0;
	// }
	PIN_RWMutexUnlock(&RWMutex);
	fprintf(out, "%lu,%d,PATHFINDER_OP_BEGIN,%d,%d\n", timestamp, tid, (int)workload_tid, (int)op_count);
	fflush(out);
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);

}

VOID BeforePathfinderOpEnd(THREADID tid, ADDRINT workload_tid, ADDRINT op_count) {
	PIN_GetLock(&pinLock, tid+1);
	// PIN_RWMutexWriteLock(&RWMutex);
	// assert(op_count.find(tid) != op_count.end());
	// PIN_RWMutexUnlock(&RWMutex);
	fprintf(out, "%lu,%d,PATHFINDER_OP_END,%d,%d\n", timestamp, tid, (int)workload_tid, (int)op_count);
	fflush(out);
	PIN_RWMutexWriteLock(&RWMutex);
	timestamp++;
	// op_count[tid]++;
	PIN_RWMutexUnlock(&RWMutex);
	PIN_ReleaseLock(&pinLock);
}

#if 0
VOID Trace(TRACE trace, VOID *v) {
	int i;

	for (i=0; i<ifunc_index; i++) {
		if (TRACE_Address(trace) == actual_memcpy_add[i]) {
			TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)BeforeMemcpy,
					IARG_ADDRINT, "memcpy(i-func)",
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
					IARG_THREAD_ID,
					IARG_END);

		}
	}
}
#endif

VOID Routine( IMG img, RTN rtn, void * v)
{
	// In some libc implementations, memcpy, memmove  symbols have the same address.
	// In this case, since Pin only creates one RTN per start address, the RTN name
	// will be either memcpy, memmove.
	// bool isMemmove = strcmp(RTN_Name(rtn).c_str(),"memmove")==0 ;
	// bool isMemcpy = strcmp(RTN_Name(rtn).c_str(),"memcpy")==0 ;
	// bool isMemset = strcmp(RTN_Name(rtn).c_str(),"memset")==0 ;
	bool isMain = strcmp(RTN_Name(rtn).c_str(),"main")==0 ;
	

	
	if (isMain) {
		RTN_Open(rtn);

		RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)BeforeMain,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
				IARG_THREAD_ID,
				IARG_END);

		RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)AfterMain,
				IARG_FUNCRET_EXITPOINT_VALUE,
				IARG_THREAD_ID,
				IARG_END);

		RTN_Close(rtn);
		return;
	}

	

	// if (isMemmove || isMemcpy)
	// {
	// 	if (SYM_IFuncResolver(RTN_Sym(rtn)))
	// 	{
	// 		PROTO proto_ifunc_memcpy = PROTO_Allocate( PIN_PARG(void *), CALLINGSTD_DEFAULT,
	// 				"memcpy", PIN_PARG_END() );

	// 		RTN_ReplaceSignature(rtn, AFUNPTR( IfuncMemcpyWrapper ),
	// 				IARG_PROTOTYPE, proto_ifunc_memcpy,
	// 				IARG_CONTEXT,
	// 				IARG_ORIG_FUNCPTR,
	// 				IARG_THREAD_ID,
	// 				IARG_END);
	// 	}
	// 	else
	// 	{
	// 		RTN_Open(rtn);

	// 		// Instrument memcpy() to print the input argument value and the return value.
	// 		RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)BeforeMemcpy,
	// 				IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
	// 				IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
	// 				IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
	// 				IARG_THREAD_ID,
	// 				IARG_END);

	// 		RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)AfterMemcpy,
	// 				IARG_FUNCRET_EXITPOINT_VALUE,
	// 				IARG_THREAD_ID,
	// 				IARG_END);

	// 		RTN_Close(rtn);
	// 	}
	// }
	// if (isMemset)
	// {
	// 	RTN_Open(rtn);

	// 	// Instrument memcpy() to print the input argument value and the return value.
	// 	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)BeforeMemset,
	// 			IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
	// 			IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
	// 			IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
	// 			IARG_THREAD_ID,
	// 			IARG_END);

	// 	RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)AfterMemset,
	// 			IARG_FUNCRET_EXITPOINT_VALUE,
	// 			IARG_THREAD_ID,
	// 			IARG_END);

	// 	RTN_Close(rtn);
	// }
}

VOID Image(IMG img, VOID *v)
{
    for( SEC sec=IMG_SecHead(img); SEC_Valid(sec) ; sec=SEC_Next(sec) )
    {
        if ( SEC_IsExecutable(sec) )
        {
            for( RTN rtn=SEC_RtnHead(sec); RTN_Valid(rtn) ; rtn=RTN_Next(rtn) )
                 Routine(img, rtn,v);
        }
    }

	// Find PATHFINDER_OP_BEGIN and PATHFINDER_OP_END, or any routine that contains them as substring
	// RTN pathfinderOpBeginRtn = RTN_FindByName(img, PATHFINDER_OP_BEGIN);

	// Print all the routines in the image
	for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
		for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
			if (RTN_Name(rtn).find("PATHFINDER_OP_BEGIN") != std::string::npos) {
				RTN_Open(rtn);
				RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)BeforePathfinderOpBegin, 
				IARG_THREAD_ID,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
				IARG_END);
				RTN_Close(rtn);
			}
			else if (RTN_Name(rtn).find("PATHFINDER_OP_END") != std::string::npos) {
				RTN_Open(rtn);
				RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)BeforePathfinderOpEnd,
				IARG_THREAD_ID,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
				IARG_END);
				RTN_Close(rtn);
			}
		}
	}

    // Instrument the malloc() and free() functions.  Print the input argument
    // of each malloc() or free(), and the return value of malloc().
    //
    //  Find the malloc() function.
    // RTN mallocRtn = RTN_FindByName(img, MALLOC);
    // if (RTN_Valid(mallocRtn))
    // {
    //     RTN_Open(mallocRtn);
 
    //     // Instrument malloc() to print the input argument value and the return value.
    //     RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeMalloc, IARG_CONTEXT, IARG_ADDRINT, MALLOC, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
    //                    IARG_END);
    //     RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR)AfterMalloc, IARG_CONTEXT, IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
 
    //     RTN_Close(mallocRtn);
    // }
 
    // // Find the free() function.
    // RTN freeRtn = RTN_FindByName(img, FREE);
    // if (RTN_Valid(freeRtn))
    // {
    //     RTN_Open(freeRtn);
    //     // Instrument free() to print the input argument value.
    //     RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)BeforeFree, IARG_CONTEXT, IARG_ADDRINT, FREE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
    //                    IARG_END);
    //     RTN_Close(freeRtn);
    // }
}

VOID Instruction(INS ins, VOID *v)
{
	// if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
	// {
	// 	INS_InsertPredicatedCall(
	// 			ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
	// 			IARG_CONTEXT,
	// 			IARG_INST_PTR,
	// 			IARG_MEMORYREAD_EA,
	// 			IARG_THREAD_ID,
	// 			IARG_END);
	// }
	// if (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) {
	// 	INS_InsertPredicatedCall(
	// 			ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
	// 			IARG_CONTEXT,
	// 			IARG_INST_PTR,
	// 			IARG_MEMORYREAD2_EA,
	// 			IARG_THREAD_ID,
	// 			IARG_END);
	// }
	// if (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins))
	// {
	// 	// insert RecordMemWrite after memory write ins
	// 	INS_InsertPredicatedCall(
	// 			ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
	// 			IARG_CONTEXT,
	// 			IARG_INST_PTR,
	// 			IARG_MEMORYWRITE_EA,
	// 			IARG_MEMORYWRITE_SIZE,
	// 			IARG_THREAD_ID,
	// 			IARG_END);
				
	// 	if (INS_IsValidForIpointAfter(ins))
	// 			INS_InsertPredicatedCall(
	// 				ins, IPOINT_AFTER, (AFUNPTR)PrintMemWrite,
	// 				IARG_CONTEXT,
	// 				IARG_THREAD_ID,
	// 				IARG_END);
		
	// 	if (INS_IsValidForIpointTakenBranch(ins))
	// 			INS_InsertPredicatedCall(
	// 				ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)PrintMemWrite,
	// 				IARG_CONTEXT,
	// 				IARG_THREAD_ID,
	// 				IARG_END);
	// }

	// Instruments memory accesses using a predicated call, i.e.
	// the instrumentation is called iff the instruction will actually be executed.
	//
	// On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
	// prefixed instructions appear as predicated instructions in Pin.
	UINT32 memOperands = INS_MemoryOperandCount(ins);

	// Iterate over each memory operand of the instruction.
	for (UINT32 memOp = 0; memOp < memOperands; memOp++)
	{
		// if (INS_MemoryOperandIsRead(ins, memOp))
		// {
		// 	INS_InsertPredicatedCall(
		// 			ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
		// 			IARG_CONTEXT,
		// 			IARG_INST_PTR,
		// 			IARG_MEMORYOP_EA, memOp,
		// 			IARG_THREAD_ID,
		// 			IARG_END);
		// }
		// Note that in some architectures a single memory operand can be 
		// both read and written (for instance incl (%eax) on IA-32)
		// In that case we instrument it once for read and once for write.
		if (INS_MemoryOperandIsWritten(ins, memOp))
		{
			INS_InsertPredicatedCall(
					ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
					IARG_CONTEXT,
					IARG_INST_PTR,
					IARG_MEMORYOP_EA, memOp,
					IARG_MEMORYWRITE_SIZE,
					IARG_THREAD_ID,
					IARG_END);
			if (INS_IsValidForIpointAfter(ins)) {
					INS_InsertPredicatedCall(
						ins, IPOINT_AFTER, (AFUNPTR)PrintMemWrite,
						IARG_CONTEXT,
						IARG_THREAD_ID,
						IARG_END);	
			}
			else {
					INS_InsertPredicatedCall(
						ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)PrintMemWrite,
						IARG_CONTEXT,
						IARG_THREAD_ID,
						IARG_END);
			}
		}
	}
}

// This routine is executed once at the end.
VOID Fini(INT32 code, VOID *v) {
	fclose(out);
	PIN_RWMutexFini(&RWMutex);
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage() {
	PIN_ERROR("This Pintool prints a trace of malloc calls in the guest application\n"
			+ KNOB_BASE::StringKnobSummary() + "\n");
	return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(INT32 argc, CHAR **argv) {
	// Initialize the pin lock
	PIN_InitLock(&pinLock);
	PIN_RWMutexInit(&RWMutex);

	// Initialize pin
	if (PIN_Init(argc, argv))
		return Usage();

	// Initialize pin & symbol manager
	PIN_InitSymbolsAlt(SYMBOL_INFO_MODE(UINT32(IFUNC_SYMBOLS)));

	out = fopen(of_knob.Value().c_str(), "w");
	target_filename = tf_knob.Value();
	record_value = rv_knob.Value();

	// Register Analysis routines to be called when a thread begins/ends
	PIN_AddThreadStartFunction(ThreadStart, 0);
	PIN_AddThreadFiniFunction(ThreadFini, 0);

	PIN_AddSyscallEntryFunction(SyscallEntry, 0);
	PIN_AddSyscallExitFunction(SyscallExit, 0);

	// Register Image to be called to instrument functions.
	IMG_AddInstrumentFunction(Image, 0);
	
	INS_AddInstrumentFunction(Instruction, 0);

	// Register Fini to be called when the application exits
	PIN_AddFiniFunction(Fini, 0);

	// Never returns
	PIN_StartProgram();

	return 0;
}