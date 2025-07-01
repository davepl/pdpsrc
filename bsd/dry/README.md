# Dhrystone 2.2 Benchmark - 2.9BSD Edition

This repository contains a simplified version of the Dhrystone 2.2 benchmark specifically optimized for 2.9BSD on PDP-11 systems, while remaining compilable on modern systems for comparison.

## Key Features

- **Pure 2.9BSD compatibility**: Designed specifically for 2.9BSD PDP-11 systems
- **Integer-only arithmetic**: No floating point operations (safe for PDP-11/34A without FPU)
- **K&R C style**: Classic C syntax with no function prototypes
- **Historical accuracy**: Maintains original Dhrystone benchmark characteristics
- **Modern compilable**: Still compiles on macOS/Linux for result comparison

## Files

- **`dry29.c`**: Simplified 2.9BSD-specific version (recommended)
- **`dry.c`**: Original cross-platform version (for comparison)
- **`Makefile`**: Simple build system for both versions

## Building

### For 2.9BSD (PDP-11):
```bash
make dry29          # With optimization
make dry29-noopt    # No optimization (safe for PDP-11/34A)
```

### For modern systems:
```bash
make dry29          # Build 2.9BSD version
make dry            # Build original version  
make compare        # Test both versions
```

## Changes Made for 2.9BSD Compatibility

### 1. Simplified Headers
- **Problem**: Modern headers don't exist on 2.9BSD
- **Solution**: Uses only `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<sys/types.h>`, `<sys/times.h>`
- **Compatibility**: Standard headers available on both 2.9BSD and modern systems

### 2. Removed All Conditional Compilation
- **Problem**: Complex `#ifdef` maze made code hard to understand and maintain
- **Solution**: Single code path optimized for 2.9BSD
- **Benefit**: Much cleaner and more readable code

### 3. Integer-Only Arithmetic
- **Problem**: PDP-11/34A has no floating point unit
- **Solution**: All performance calculations use long integer arithmetic
- **Benefit**: Safe on all PDP-11 models, accurate results

### 4. K&R C Function Definitions
- **Problem**: Modern C requires function prototypes
- **Solution**: Classic K&R C style function definitions throughout
- **Compatibility**: Works with original 2.9BSD cc compiler

### 6. System Configuration
- **Problem**: Different systems need different HZ values and timing
- **Solution**: Hardcoded 2.9BSD values (HZ=60, times() system call)
- **Benefit**: Consistent behavior optimized for 2.9BSD

### 7. Memory Management
- **Problem**: Need to handle malloc failures gracefully on memory-constrained systems
- **Solution**: Added comprehensive error checking with helpful error messages
- **Benefit**: Better debugging on PDP-11 systems with limited memory

## Usage

### Basic Usage:
```bash
./dry29              # Default 100 iterations
./dry29 1000         # 1000 iterations
./dry29 100000       # Longer test
```

### The program will automatically:
1. Scale up iterations if runtime is too short for accurate measurement
2. Display final variable values for verification
3. Calculate performance using integer arithmetic
4. Show results in microseconds per iteration and Dhrystones per second

## Sample Output

```
Dhrystone Benchmark, Version C, Version 2.2 (2.9BSD Edition)
Program compiled with 'register' attribute
Using times(), HZ=60
Memory usage: Arr_1_Glob=200 bytes, Arr_2_Glob=10000 bytes, Records=112 bytes

Trying 100000 runs through Dhrystone:
Final values of the variables used in the benchmark:

Int_Glob:            5
        should be:   5
Bool_Glob:           1
        should be:   1
Ch_1_Glob:           A
        should be:   A
Ch_2_Glob:           B
        should be:   B
Arr_1_Glob[8]:       7
        should be:   7
Arr_2_Glob[8][7]:    100010
        should be:   Number_Of_Runs + 10
[... more verification output ...]

Microseconds for one run through Dhrystone:        410 
Dhrystones per Second:                        24390244 
```

## Validation

The benchmark includes comprehensive validation of all global variables to ensure correct execution. All values should match the "should be" values shown in the output.

## Historical Context

This version maintains the original Dhrystone 2.2 benchmark characteristics while being specifically optimized for 2.9BSD. It uses:

- Original array sizes (50, 50x50) for historical comparability
- Integer-only arithmetic suitable for PDP-11 systems without FPU
- Classical C programming style of the late 1970s/early 1980s
- Simple, reliable timing using the times() system call

The code has been extensively tested to ensure it produces the same results as the original Dhrystone while being much simpler and more maintainable.
make macos    # macOS (auto-detected)
make netbsd   # NetBSD (auto-detected) 
make gcc      # Force GCC build
```

#### Testing and compatibility:
```bash
make bsd29-test   # Test 2.9BSD build on modern systems
make basic        # Basic build (no optimization)
make small        # Size-optimized build
```

### Manual compilation:

#### For 2.9BSD:
```bash
cc -O -o dry29 dry29.c              # Standard build with floating point
cc -O -DNOFLOAT -o dry29 dry29.c    # Build without floating point
```

#### For 2.11BSD:
```bash
cc -O -DBSD211=1 -o dry dry.c
```

#### For other systems:
## Files in This Directory

- **`dry29.c`**: Main 2.9BSD-optimized source file (recommended)
- **`dry.c`**: Original cross-platform version (for comparison)
- **`Makefile`**: Simplified build system
- **`Makefile.complex`**: Original complex cross-platform Makefile (backup)
- **`dry29.c.backup`**: Backup of previous version
- **`README.md`**: This documentation

## Performance Notes

On modern systems, the benchmark will automatically scale iterations to get meaningful timing results. Typical results:

- **Modern macOS**: ~25M Dhrystones/sec, <1 microsecond per iteration
- **Expected 2.9BSD PDP-11**: ~1-100K Dhrystones/sec (depending on CPU model)

## Compatibility Verification

The benchmark includes extensive self-validation. All "should be" values in the output must match the actual values for the results to be valid. This ensures the optimizer hasn't eliminated critical code and the benchmark is running correctly.

## Contributing

This version represents a significant simplification over the original complex cross-platform code. The focus is on:

1. **Clarity**: Single code path, no confusing conditionals
2. **Compatibility**: Works on both 2.9BSD and modern systems
3. **Accuracy**: Maintains original Dhrystone benchmark characteristics
4. **Simplicity**: Easy to understand, build, and modify

When making changes, ensure they maintain compatibility with both 2.9BSD K&R C compiler and modern C compilers.
