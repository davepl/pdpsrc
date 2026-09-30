# Linux x64 Panel Implementation

This implements a PDP panel simulator for Linux x64 systems using real kernel register data captured via kprobes.

## Quick Start

### 1. Build Everything
```bash
# Build kernel module, client, and server
make all

# Or build individually:
make module    # Build kernel module
make client    # Build userspace client  
make server    # Build server (in parent directory)
```

### 2. Load Kernel Module
```bash
# Load the kprobe module (captures real CPU register state)
sudo make load

# Verify it's loaded
cat /proc/panel_regs

# Check kernel logs
dmesg | tail
```

### 3. Start Server
```bash
# From the main socket directory
cd ../..
./server
```

### 4. Run Client
```bash
# Send panel data to server
./client

# Or specify server IP
./client -s 192.168.1.100
```

## How It Works

1. **Kernel Module (`kprobe.c`)**: Uses kprobes to hook the `schedule()` function and capture real CPU register state from CPU 0 only. Exports data via `/proc/panel_regs`.

2. **Client (`client.c`)**: Reads register data from `/proc/panel_regs`, parses 21 x64 registers (RIP, RSP, RAX, etc.), and sends 184-byte UDP packets to server at 30 FPS.

3. **Server**: Receives LinuxX64 packets (type=5) and displays register values in binary format, just like other panel implementations.

## Cleanup
```bash
# Unload kernel module
sudo make unload

# Clean build files
make clean
```

## Troubleshooting

- **No register data**: Make sure kernel module is loaded (`sudo make load`)
- **Permission denied**: Need sudo for kernel module operations
- **Client exits**: Check that `/proc/panel_regs` exists and is readable
- **Server not receiving**: Verify client shows "Connected to server" message

## Architecture

- **Real Data Only**: No fake data - exits with error if kernel module not loaded
- **Primary CPU Only**: Only captures from CPU 0 to avoid race conditions
- **Standard Protocol**: Uses same UDP packet format as other panel implementations
- **High Frequency**: 30 FPS updates showing live kernel register state

The implementation follows the same pattern as NetBSD, macOS, and other platform-specific clients in the project.
