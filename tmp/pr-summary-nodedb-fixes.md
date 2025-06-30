# PR Summary: Fix NodeDB Vector Crashes and Bluetooth Concurrency Issues

## Title

Fix NodeDB vector crashes and Bluetooth concurrency issues

## Description

This PR addresses critical `std::out_of_range` crashes in NodeDB operations and resolves Bluetooth concurrency issues that were causing system instability. The fixes are based on extensive investigation into vector access patterns and asynchronous operation handling.

### Problem Summary

The firmware was experiencing crashes with the error:

```
vector::_M_range_check: __n (which is 0) >= this->size() (which is 0)
```

Investigation revealed multiple interconnected issues:

1. **Memory corruption**: `memset(0)` on structures containing `std::vector` corrupts internal pointers
2. **Concurrency issues**: Bluetooth packet processing interrupting NodeDB sort operations
3. **Unsafe sorting**: `std::sort` move/swap operations not safe with `NodeInfoLite` structures
4. **Missing bounds checking**: Vector access without verifying non-empty state

### Changes Made

#### 1. Replace std::sort with Bubble Sort (`src/mesh/NodeDB.cpp`)

- **Problem**: `std::sort` performs complex move/swap operations unsafe with `NodeInfoLite` structures
- **Solution**: Implemented bubble sort algorithm using simple element swapping
- **Benefit**: Eliminates vector corruption during concurrent operations

#### 2. Fix Memory Corruption in loadProto (`src/mesh/NodeDB.cpp`)

- **Problem**: `memset(0)` on `NodeDatabase` structures corrupts `std::vector` internal pointers
- **Solution**: Conditional memset to avoid corrupting vector-containing structures
- **Benefit**: Prevents vector corruption during protobuf loading operations

#### 3. Add Comprehensive Bounds Checking (`src/mesh/NodeDB.h`, `src/mesh/NodeDB.cpp`)

- **Problem**: Vector access without verifying non-empty state causes crashes
- **Solution**: Added bounds checking in `getMeshNodeByIndex()` and `sortMeshDB()`
- **Benefit**: Prevents crashes when `numMeshNodes == 0`

#### 4. Implement Bluetooth Packet Queuing (`src/nimble/NimbleBluetooth.cpp`)

- **Problem**: Direct `handleToRadio()` calls from Bluetooth callbacks cause race conditions
- **Solution**: Queue-based processing using OSThread with mutex protection
- **Benefit**: Eliminates race conditions between Bluetooth and NodeDB operations

#### 5. Prevent Self-Assignment in cleanupMeshDB (`src/mesh/NodeDB.cpp`)

- **Problem**: Potential undefined behavior from self-assignment when `newPos == i`
- **Solution**: Added explicit check to avoid unnecessary assignment operations
- **Benefit**: Eliminates potential undefined behavior in vector operations

### Technical Details

#### Concurrency Safety

- Bluetooth packets are now queued and processed sequentially
- Mutex protection ensures thread-safe queue operations
- OSThread-based processing prevents callback-induced race conditions

#### Memory Safety

- Bounds checking prevents vector out-of-range access
- Conditional memory operations avoid corrupting complex structures
- Self-assignment prevention eliminates undefined behavior

#### Performance Considerations

- Bubble sort has O(n²) complexity but is safer for concurrent operations
- Queue processing adds minimal latency while ensuring stability
- Bounds checking overhead is negligible compared to crash recovery

### Testing Recommendations

- Test Bluetooth connectivity under high packet load
- Verify NodeDB operations with empty and full node lists
- Validate sorting functionality with concurrent Bluetooth operations
- Test Linux native daemon stability over extended periods

### Compatibility

- No breaking changes to existing APIs
- Maintains full NodeDB functionality
- Compatible with all existing Bluetooth implementations
- No changes to protobuf structures or network protocols

### Fixes

- Resolves critical crashes affecting Linux native daemon
- Eliminates `std::out_of_range` exceptions in NodeDB operations
- Prevents memory corruption during protobuf operations
- Ensures stable operation under concurrent Bluetooth activity

This PR is based on investigation work by Jonathan Bennett who identified the core memory corruption and concurrency issues causing the crashes.
