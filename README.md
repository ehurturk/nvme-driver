# User Space NVMe Driver

This is a C++ User Space NVMe driver that is
based on polling-based queues, uses PRP DMA buffers, and
will eventually have VFIO/IOMMU extension.

Initially, it will consist of:
- PCI device discovery
- BAR mapping
- controller reset/enable
- admin queue setup
- Identify Controller
- Identify Namespace
- Create I/O CQ/SQ
- Basic read/write operations
- PRP construction
- Polling completions

Benchmarking:
- Benchmark against Linux pread/pwrite with O_DIRECT
- io_uring
- SPDK
- my own driver

measuring:
- throughput
- average latency (dprop)
- p99 / p99.9 latency
- CPU usage
- queue depth scaling
- block size scaling

Extension:
- VFIO/IOMMU backend
- Interrupts vs polling expeeriment
- Per-core queue pairs
    - one queue pair per pinned thread
    - one completion poller per core
    - lock-free
    - benchmark with QD=1, QD=8, QD=32, QD=128
    - inspire from SPDK model: use direct BAR mapping with MMIO, and asynchonronous
      submission through queue pairs and polling for completions. SPDK recommends a
      threading model where each thread owns its own queue pair, avoiding locks in the 
      IO path.
- Controller memory buffer support
- Maybe turn this into a Linux loadable kernel module?

Resources:
- Linux NVMe driver: https://github.com/torvalds/linux/blob/master/include/linux/nvme.h
- Linux NVMe implementation: https://github.com/torvalds/linux/blob/master/drivers/nvme/host/core.c#L1806
- NVMe Base Specification: https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-Revision-2.3-2025.08.01-Ratified.pdf
- SPDK NVMe docs: https://github.com/spdk/spdk/blob/master/doc/nvme_spec.md
- NVME Architectures:
    - Oracle: https://blogs.oracle.com/linux/overview-of-nvme-architecture
    - Dell: https://infohub.delltechnologies.com/en-nz/l/an-in-depth-overview-of-nvme-and-nvme-of/types-of-nvme-queues/
- QEMU NVMe implementation: https://github.com/qemu/qemu/tree/master/hw/nvme
- VFIO (Virtual Function IO): https://docs.kernel.org/driver-api/vfio.html
- NVMe driver in rust: https://db.in.tum.de/~ellmann/theses/finished/24/pirhonen_writing_an_nvme_driver_in_rust.pdf
- Linux Userspace Network Drivers: https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/ixy-writing-user-space-network-drivers.pdf
- Linux Modern Storage APIs: https://atlarge-research.com/pdfs/2022-systor-apis.pdf


