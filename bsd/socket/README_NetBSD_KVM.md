# NetBSD KVM Library Implementation

## Overview
The client.c code has been updated to use the NetBSD kvm library for kernel memory access instead of direct `/dev/kmem` access. This solves the issue where high kernel virtual addresses cannot be accessed directly via `/dev/kmem` on NetBSD/AMD64.

## Changes Made

### 1. Conditional Compilation
- Added `#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))` blocks throughout the code
- NetBSD/AMD64 uses kvm library functions, other platforms use the original `/dev/kmem` approach

### 2. New Includes for NetBSD
```c
#include <kvm.h>
#include <nlist.h>
#include <limits.h>
```

### 3. Global Variables for KVM
```c
static kvm_t *kd = NULL;
static struct nlist nl[] = {
    { "_panel" },
    { NULL }
};
```

### 4. New Functions for NetBSD
- `open_kvm_and_find_panel()`: Opens kvm and looks up the panel symbol
- `read_panel_from_kvm()`: Reads panel data using kvm_read()

### 5. Makefile Updates
- Added conditional linking with `-lkvm` on NetBSD systems

## How It Works

### PDP-11 / Traditional Systems
1. Uses `nm` to find the panel symbol in the kernel
2. Opens `/dev/kmem` directly
3. Uses `lseek()` and `read()` to access kernel memory

### NetBSD/AMD64 Systems
1. Uses `kvm_open()` to open kernel virtual memory
2. Uses `kvm_nlist()` to look up the panel symbol
3. Uses `kvm_read()` to read kernel memory at the symbol address

## Testing on NetBSD

### Prerequisites
- NetBSD system with kvm library installed
- Root access or appropriate permissions
- Panel symbol in kernel (ensure your kernel has the panel structure)

### Compilation
```bash
cd /Users/dave/source/repos/pdpsrc/bsd/socket
make clean
make client
```

### Running
```bash
sudo ./client
```

### Expected Output
```
Connecting to server at 127.0.0.1:8080 via UDP
Panel symbol found at address 0x[kernel_address]
Connected successfully. Sending panel data at 30 Hz...
Packet size: 16 bytes (ps_address: 8, ps_data: 8)
Sent 30 panel updates (addr=0x[value], data=0x[value])
```

## Troubleshooting

### Error: "kvm_open: Permission denied"
- Run as root: `sudo ./client`
- Ensure `/dev/kmem` exists and is readable

### Error: "Panel symbol not found in kernel symbol table"
- Check if panel symbol exists: `nm /netbsd | grep panel`
- Ensure kernel has the panel structure compiled in

### Error: "kvm_read: [error message]"
- Check kernel symbol table consistency
- Verify kernel and userland are compatible

## Benefits

1. **Works with High Kernel Addresses**: The kvm library handles kernel virtual address translation automatically
2. **Proper API**: Uses the official NetBSD API for kernel memory access
3. **Error Handling**: Better error reporting through `kvm_geterr()`
4. **Portable**: Falls back to original method on non-NetBSD systems
5. **Orthogonal**: NetBSD and PDP-11 code paths work independently

## Architecture

The code now has two orthogonal paths:

```
client.c
├── NetBSD/AMD64 (kvm library)
│   ├── open_kvm_and_find_panel()
│   └── read_panel_from_kvm()
└── PDP-11/Other (direct /dev/kmem)
    ├── open_kmem_and_find_panel()
    └── read_panel_from_kmem()
```

Both paths provide the same functionality but use different underlying mechanisms appropriate for their respective platforms.

## Updated Makefile for NetBSD Compatibility

**Issue Fixed**: The original Makefile used GNU make syntax which is not compatible with NetBSD's BSD make.

**Solution**: Replaced GNU make syntax with portable shell conditionals:

```makefile
# Old (GNU make only):
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),NetBSD)
    CLIENT_LIBS = -lkvm
else
    CLIENT_LIBS = 
endif

# New (BSD make compatible):
client: client.c
	@if [ "`uname -s`" = "NetBSD" ]; then \
		$(CC) $(CFLAGS) -o client client.c -lkvm; \
	else \
		$(CC) $(CFLAGS) -o client client.c; \
	fi
```

**Result**: The Makefile now works on NetBSD, macOS, and other Unix systems.
