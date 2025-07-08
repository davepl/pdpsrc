# Socket Load Test Programs

This directory contains a client/server pair for load testing socket communication on PDP-11 systems running 211BSD.

## Files

- `client.c` - Client program that sends 16-word frames to server
- `server.c` - Server program that receives frames and prints status
- `Makefile` - Build configuration for both programs

## Building

To compile both programs:
```
make
```

To compile individually:
```
make client
make server
```

## Usage

### Server
Start the server first:
```
./server
```

The server will:
- Listen on port 8080 for UDP packets
- Accept panel data from clients
- Print a `*` for each panel update received
- Display actual ADDRESS and DATA values from the PDP-11 front panel simulation
- Print update counts every 10 seconds

### Client
Connect to the server:
```
./client                    # Connect to localhost (127.0.0.1)
./client -s 192.168.1.100   # Connect to specific IP address
```

The client will:
- Open `/dev/kmem` and locate the kernel `panel` symbol
- Read the panel structure from kernel memory at 30 Hz
- Send panel data (address and data values) to the server
- Print connection status and periodic updates with actual panel values
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
