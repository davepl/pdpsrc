# Socket Client Project - Platform-Specific Implementations

This project has been refactored to provide clean, platform-specific implementations of the socket client without conditional compilation clutter.

## Directory Structure

```
bsd/socket/
├── common.c              # Shared functions for all platforms
├── panel_packet.h        # Common packet header structure
├── Makefile.master       # Master build file for all platforms
├── 211BSD/              # PDP-11 2.11BSD implementation
│   ├── client.c         # PDP-11 specific client
│   ├── panel_state.h    # PDP-11 panel structure definitions
│   └── Makefile         # PDP-11 build configuration
├── NetBSDx64/           # NetBSD x64 implementation
│   ├── client.c         # NetBSD x64 specific client (uses kvm)
│   ├── panel_state.h    # NetBSD x64 panel structure definitions
│   └── Makefile         # NetBSD x64 build configuration
└── NetBSDVAX/           # NetBSD VAX implementation
    ├── client.c         # NetBSD VAX specific client
    ├── panel_state.h    # NetBSD VAX panel structure definitions
    └── Makefile         # NetBSD VAX build configuration
```

## Panel Packet Protocol

The communication protocol uses a structured packet format with a common header followed by platform-specific panel data.

### Common Packet Header

Defined in `panel_packet.h`:
```c
struct panel_packet_header {
    u16_t pp_byte_count;    /* Size of the panel_state payload */
    u32_t pp_byte_flags;    /* Panel type flags */
};
```

### Panel Type Flags

The `pp_byte_flags` field identifies the client type:
- `PANEL_PDP1170` (0x01) - PDP-11/70 panel data
- `PANEL_VAX` (0x02) - VAX panel data  
- `PANEL_NETBSDX64` (0x03) - NetBSD x64 panel data

### Platform-Specific Packet Structures

Each platform defines its own packet structure that includes the common header:

**PDP-11 (211BSD/panel_state.h)**:
```c
struct pdp_panel_packet {
    struct panel_packet_header header;
    struct pdp_panel_state panel_state;
};
```

**NetBSD x64 (NetBSDx64/panel_state.h)**:
```c
struct netbsdx64_panel_packet {
    struct panel_packet_header header;
    struct netbsdx64_panel_state panel_state;
};
```

**NetBSD VAX (NetBSDVAX/panel_state.h)**:
```c
struct vax_panel_packet {
    struct panel_packet_header header;
    struct vax_panel_state panel_state;
};
```

### Server Implementation Benefits

This design allows servers to:
1. Read the 6-byte header first to determine packet type and size
2. Use `pp_byte_flags` to identify the client platform
3. Read exactly `pp_byte_count` bytes for the panel data
4. Cast the panel data to the appropriate platform-specific structure

Example server packet handling:
```c
struct panel_packet_header header;
recv(sockfd, &header, sizeof(header), 0);

switch (header.pp_byte_flags) {
    case PANEL_PDP1170:
        /* Read PDP-11 panel data */
        break;
    case PANEL_VAX:
        /* Read VAX panel data */
        break;
    case PANEL_NETBSDX64:
        /* Read NetBSD x64 panel data */
        break;
}
```

## Platform Implementations

### 211BSD (PDP-11)
- **File**: `211BSD/client.c`
- **Features**:
  - K&R C compatible
  - Uses `/dev/kmem` for kernel memory access
  - Octal address parsing for PDP-11 symbol tables
  - Simple panel structure with 32-bit addresses
  - No external library dependencies

### NetBSDx64 (x86_64)
- **File**: `NetBSDx64/client.c`
- **Features**:
  - Modern C with `stdint.h` types
  - Uses `kvm` library for safe kernel memory access
  - Complete `clockframe` structure definition
  - 64-bit addressing and CPU register access
  - Links with `-lkvm`

### NetBSDVAX
- **File**: `NetBSDVAX/client.c`
- **Features**:
  - Modern C compatible
  - Uses `/dev/kmem` for kernel memory access
  - Similar to PDP-11 but with NetBSD-specific paths
  - 32-bit addressing appropriate for VAX
  - No external library dependencies

## Common Code

The `common.c` file contains shared functionality that works across all platforms:
- `create_udp_socket()` - UDP socket creation and server address setup
- `usage()` - Command line usage display
- `precise_delay()` - Timing function using `select()`
- Common constants and definitions

## Building

### Build All Platforms
```bash
make -f Makefile.master all
```

### Build Specific Platform
```bash
make -f Makefile.master 211bsd     # Build PDP-11 version
make -f Makefile.master netbsdx64  # Build NetBSD x64 version
make -f Makefile.master netbsdvax  # Build NetBSD VAX version
```

### Build in Platform Directory
```bash
cd 211BSD && make              # Build PDP-11 version
cd NetBSDx64 && make           # Build NetBSD x64 version
cd NetBSDVAX && make           # Build NetBSD VAX version
```

### Clean All Builds
```bash
make -f Makefile.master clean
```

## Key Differences

### Memory Access
- **211BSD**: Direct `/dev/kmem` access with simple lseek/read
- **NetBSDx64**: `kvm` library with `kvm_open()`, `kvm_nlist()`, `kvm_read()`
- **NetBSDVAX**: Direct `/dev/kmem` access with `/dev/mem` fallback

### Data Structures
- **211BSD**: Simple panel structure with basic PDP-11 registers
- **NetBSDx64**: Complete `clockframe` structure with all CPU registers  
- **NetBSDVAX**: Panel structure similar to PDP-11 but VAX-appropriate

### Panel Data Structures

Each platform defines its own panel state structure:

**PDP-11 Panel State**:
```c
struct pdp_panel_state {
    long ps_address;    /* panel switches - 32-bit address */
    short ps_data;      /* panel lamps - 16-bit data */
    short ps_psw;       /* processor status word */
    short ps_mser;      /* machine status register */
    short ps_cpu_err;   /* CPU error register */
    short ps_mmr0;      /* memory management register 0 */
    short ps_mmr3;      /* memory management register 3 */
};
```

**NetBSD x64 Panel State**:
```c
struct netbsdx64_panel_state {
    struct clockframe ps_frame;  /* Complete CPU register state */
};
```

**NetBSD VAX Panel State**:
```c
struct vax_panel_state {
    long ps_address;    /* panel switches - 32-bit address */
    short ps_data;      /* panel lamps - 16-bit data */
    short ps_psw;       /* processor status word */
    short ps_mser;      /* machine status register */
    short ps_cpu_err;   /* CPU error register */
    short ps_mmr0;      /* memory management register 0 */
    short ps_mmr3;      /* memory management register 3 */
};
```

### Symbol Resolution
- **211BSD**: Uses `nm /unix` or `nm /vmunix` with octal parsing
- **NetBSDx64**: Uses `kvm_nlist()` for symbol resolution
- **NetBSDVAX**: Uses `nm /netbsd` with hex parsing

## Advantages of This Structure

1. **Clean Code**: No conditional compilation clutter
2. **Platform-Specific**: Each implementation optimized for its platform
3. **Maintainable**: Easy to modify platform-specific behavior
4. **Shared Logic**: Common functionality in one place
5. **Build Flexibility**: Can build all or specific platforms
6. **Protocol Consistency**: Common packet header enables cross-platform server support
7. **Type Safety**: Platform-specific structures prevent data corruption
8. **Extensible**: Easy to add new platforms or panel types

## Usage

Each client binary has the same command-line interface:
```bash
./client [-s server_ip] [-h]
```

- `-s server_ip`: IP address of the server (default: 127.0.0.1)
- `-h`: Show help message

The client will connect to the server and continuously send panel data at 30 Hz.
- Run continuously until interrupted

**Note**: The client must run as root to access `/dev/kmem`.

## Testing

1. Start the server in one terminal
2. Start the client in another terminal
3. Observe the `*` characters printed by the server
4. Use Ctrl+C to stop either program

## Compatibility

These programs are written in K&R C (second edition) and should compile and run on:
- macOS with standard development tools
- 211BSD on PDP-11 systems
- Other Unix-like systems with socket support

## PDP-11 Timing Considerations

The client uses `select()` for precise timing instead of `usleep()` because:
- PDP-11 clock resolution is limited to line frequency (50/60Hz)
- `usleep()` delays are quantized to ~16,667µs (60Hz) or ~20,000µs (50Hz)
- `select()` with timeout provides more reliable frame rate control

This ensures consistent frame rates even on systems with coarse clock resolution.
