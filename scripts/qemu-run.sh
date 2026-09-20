#!/usr/bin/env bash
set -euo pipefail

GUEST_IMG="guest.qcow2"
NVME_IMG="nvme_backing.img"

if [[ ! -f "$NVME_IMG" ]]; then
    qemu-img create -f raw "$NVME_IMG" 1G
fi

qemu-system-x86_64 \
    -trace enable='pci_nvme*' 2>/tmp/qemu.log \
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
    -drive file="${GUEST_IMG}",if=virtio,format=qcow2 \
    \
    -device pcie-root-port,id=rp0,bus=pcie.0,slot=0 \
    \
    -drive file="${NVME_IMG}",if=none,id=nvm0,format=raw,cache=directsync \
    -device nvme,serial=deadbeef,drive=nvm0,bus=rp0 \
    \
    -nographic
