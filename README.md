# xcp-ng-vdi-stream

Library to stream virtual disk images and differencing disks.

Only QCOW2 images are supported for the moment and contrary to `qemu-img` a readable stream can be created directly from a QCow2 image chain without using a temporary file.  It's the main goal of this lib: Create a stream without writing to disk.

## Dependencies

[xcp-ng-generic-lib](https://github.com/xcp-ng/xcp-ng-generic-lib) is required to build this project. You must build it before the next step.

## Build

Run these commands in the project directory:

```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=<path_to_xcp-ng-generic-build_dir>
make
```

## Tools

Two tools linked to this library are provided:
- `dump-info` to extract metadata of an image
- `stream-to-file` to stream an image chain to a file

Examples:

```
# Show metadata of the differencing image 10.qcow2.
> ./tools/dump-info qcow2 ../tests/images/10.qcow2
QCOW Image Header
version: 3
header length: 104
virtual size: 2147483648 bytes
backing file: 9.qcow2
crypt method: 0
cluster size: 2097152 bytes
nb sectors per cluster: 4096
refcount table size (max nb of entries): 262144
refcount block size: 1048576
l1 size (current nb of entries): 1
l2 size (max nb of entries): 262144
nb snapshots: 0
incompatible features: 0
compatible features: 0
autoclear features: 0

# Write in output.qcow the full export of 9.qcow2.
./tools/stream-to-file output.qcow2 qcow2 ../tests/images/9.qcow2

# Write in output.qcow the delta between 12.qcow2 and 11.qcow2.
./tools/stream-to-file output.qcow2 qcow2 ../tests/images/12.qcow2 ../tests/images/11.qcow2

# Write in output.qcow the delta between 5.qcow2 and 5.qcow2.
# So... The output image cannot contains data in this case.
./tools/stream-to-file output.qcow2 qcow2 ../tests/images/5.qcow2 ../tests/images/5.qcow2

```
