#!/usr/bin/env bash
set -euo pipefail

GUEST_IMG="guest.qcow2"
NVME_IMG="nvme_backing.img"

if [[ ! -f "$NVME_IMG" ]]; then
    qemu-img create -f raw "$NVME_IMG" 1G
fi

qemu-system-x86_64 \
    -machine q35,accel=kvm,kernel-irqchip=split \
    -cpu host \
    -smp 4 \
    -m 4G \
    -enable-kvm \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device e1000,netdev=net0 \
    \
    `# emulated Intel IOMMU — caching-mode is REQUIRED for VFIO passthrough` \
    -device intel-iommu,intremap=on,caching-mode=on \
    \
    `# guest OS disk` \
    -drive file="${GUEST_IMG}",if=virtio,format=qcow2 \
    \
    `# dedicated PCIe root port → clean IOMMU group for the NVMe` \
    -device pcie-root-port,id=rp0,bus=pcie.0,slot=0 \
    \
    `# emulated NVMe controller on that root port` \
    -drive file="${NVME_IMG}",if=none,id=nvm0,format=raw \
    -device nvme,serial=deadbeef,drive=nvm0,bus=rp0 \
    \
    -nographic
