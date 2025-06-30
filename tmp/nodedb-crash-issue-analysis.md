# NodeDB Vector Out-of-Range Crashes and Bluetooth Concurrency Issues

## Problem Description

The firmware is experiencing critical crashes with `std::out_of_range` exceptions when accessing the NodeDB vector, particularly manifesting as:

```
terminate called after throwing an instance of 'std::out_of_range'
what(): vector::_M_range_check: __n (which is 0) >= this->size() (which is 0)
Aborted
```

This issue affects multiple platforms but is particularly problematic on Linux native daemon installations where it causes immediate service termination.

## Root Cause Analysis

Through extensive investigation by Jonathan Bennett and analysis of crash logs, multiple interconnected issues have been identified:

### 1. std::sort Vector Move/Swap Issues

- `std::sort` in `sortMeshDB()` performs complex move/swap operations on `NodeInfoLite` structures
- These operations are not safe with the current `NodeInfoLite` implementation containing complex members
- Asynchronous Bluetooth packet handling interrupts sorting operations, corrupting vector state
- Log evidence shows: `INFO | Inside sort!` followed immediately by `DEBUG | New ToRadio packet`

### 2. Memory Corruption from memset on Vectors

**Critical Discovery by Jonathan Bennett:**

> "We call memset(dest_struct, 0, objSize) on our destination objects before we load the saved protobuf. But that dest_struct contains a vector in this case. And memset on that is just asking for a kablooey"

- `memset(dest_struct, 0, objSize)` in `loadProto()` corrupts `NodeDatabase` structures containing `std::vector<NodeInfoLite>`
- Vector internal pointers become invalid, leading to undefined behavior and crashes
- This affects protobuf loading operations during startup and configuration changes

### 3. Vector Access Without Bounds Checking

- Multiple functions access `meshNodes->at(index)` without verifying `numMeshNodes > 0`
- `getMeshNodeByIndex()` uses assert() which is disabled in release builds
- Empty vector access attempts trigger `std::out_of_range` exceptions
- Particularly problematic during initial startup when node database is empty

### 4. Bluetooth Concurrency Race Conditions

**Threading Problem Identified:**

- `handleToRadio()` called directly from Bluetooth callbacks in interrupt context
- Asynchronous packet processing breaks NodeDB sorting assumptions
- Race conditions between Bluetooth threads and main thread accessing NodeDB
- Log evidence shows packet processing interrupting sort operations

### 5. Self-Assignment in cleanupMeshDB()

- Potential self-assignment when `newPos == i` without proper checking
- Can trigger undefined behavior in complex copy operations
- Contributes to vector corruption during cleanup operations

## Technical Solution Strategy

### Replace std::sort with Bubble Sort

- Eliminates complex move/swap operations that cause crashes
- Simple element swapping is safer for NodeInfoLite structures
- Maintains sorting functionality without vector corruption risk
- Less vulnerable to interruption by concurrent operations

### Fix Memory Corruption Issues

- Conditional memset() to avoid corrupting vector-containing structures
- Specifically avoid memset on `NodeDatabase` structures
- Proper self-assignment prevention in cleanup operations

### Add Comprehensive Bounds Checking

- Verify `numMeshNodes > 0` before vector access
- Replace assert() with proper error handling and logging
- Prevent out-of-range exceptions in all access paths
- Graceful handling of empty node database states

### Implement Bluetooth Packet Queuing

- Queue Bluetooth packets instead of immediate processing
- Use OSThread-based processing to avoid concurrency issues
- Mutex protection for thread-safe queue operations
- Eliminate race conditions between Bluetooth and NodeDB operations

## Impact Assessment

### Affected Platforms

- **Linux Native Daemon**: Critical crashes causing service termination
- **ESP32 with Bluetooth**: Intermittent crashes under load
- **T-Deck with BaseUI**: Threading issues particularly problematic
- **Any platform using Bluetooth + NodeDB operations**

### Severity

- **Critical**: Complete system failure requiring restart
- **Data Loss**: Potential corruption of node database
- **User Experience**: Unreliable Bluetooth connectivity
- **Production Impact**: Unsuitable for deployment in current state

### Benefits of Fix

- Eliminates critical crashes affecting Linux native daemon and embedded devices
- Improves system stability under concurrent Bluetooth operations
- Maintains full NodeDB functionality while preventing memory corruption
- Enables reliable long-running operation with active Bluetooth connections

## Testing Strategy

### Validation Requirements

- Test Bluetooth connectivity under high packet load
- Verify NodeDB operations with empty and full node lists
- Validate sorting functionality with concurrent Bluetooth operations
- Test Linux native daemon stability over extended periods
- Stress test with multiple simultaneous Bluetooth connections

### Regression Testing

- Ensure no performance degradation in normal operations
- Verify all existing NodeDB functionality remains intact
- Test protobuf loading/saving operations
- Validate node discovery and mesh networking features

## References

- Original issue report with crash logs
- Jonathan Bennett's investigation findings
- Chat logs showing concurrent packet processing during sorts
- Valgrind analysis showing memory corruption patterns
