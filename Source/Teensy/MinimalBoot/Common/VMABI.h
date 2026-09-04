// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>

// Trusted-local ARMv7E-M hard-float modules. Not a security sandbox.
// All pointers and callbacks live until reset. Never call from an interrupt.
enum : uint32_t { VM_ABI = 1, VM_CODE_BASE = 0x20000, VM_CODE_LIMIT = 0x40000,
                  VM_RAM_BASE = 0x20200000, VM_RAM_BYTES = 512*1024 };
struct VmImageHeader {
    uint32_t magic, abi, header_bytes, code_bytes, data_bytes, bss_bytes;
    uint32_t entry, code_base, ram_base, required_services, payload_crc, header_crc;
    uint32_t reserved[4];
};
static_assert(sizeof(VmImageHeader)==64, "MVM1 image header");
struct VmFileInfo { uint32_t bytes; uint8_t directory; char name[96]; };
struct VmInput { uint8_t buttons, display, overflow, protocol; };
struct VmPacket { uint8_t type, flags, length, reserved; uint8_t payload[228]; };
struct VmHost {
    uint32_t abi, bytes, services;
    uint8_t *workspace; uint32_t workspace_bytes;
    const char *package_root, *content_path;
    uint32_t (*micros_now)();
    // Handles 1..8; zero is failure. read returns -1 on error.
    uint32_t (*open)(const char *path, VmFileInfo *info);
    int32_t (*read)(uint32_t handle, uint32_t offset, void *data, uint32_t count);
    int32_t (*next)(uint32_t directory, VmFileInfo *info); // 1 entry, 0 EOF, -1 error
    void (*close)(uint32_t handle);
};
struct VmModule {
    uint32_t abi, bytes;
    // pump is permitted while awaiting ACK; it must not alter frozen output.
    void (*input)(const VmInput *input);
    void (*pump)();
    bool (*packet)(VmPacket *out);
    void (*ack)();
};
using VmEntry = const VmModule *(*)(const VmHost *host);
enum : uint32_t { VM_SERVICE_FILES=1, VM_SERVICE_CLOCK=2, VM_SERVICE_PACKETS=4,
                  VM_SERVICES=7, VM_IMAGE_MAGIC=0x314d564d };
static inline uint32_t vm_crc32(const void *data, uint32_t size) {
    auto p=static_cast<const uint8_t *>(data); uint32_t c=~0u;
    while(size--) { c^=*p++; for(unsigned b=0;b<8;b++) c=(c>>1)^((0u-(c&1))&0xedb88320u); }
    return ~c;
}
static inline bool vm_valid_header(const VmImageHeader &h, uint32_t file_bytes) {
    if(h.magic!=VM_IMAGE_MAGIC || h.abi!=VM_ABI || h.header_bytes!=sizeof h ||
       h.code_base!=VM_CODE_BASE || h.ram_base!=VM_RAM_BASE || !h.code_bytes ||
       h.code_bytes>VM_CODE_LIMIT-VM_CODE_BASE || h.data_bytes>VM_RAM_BYTES ||
       h.bss_bytes>VM_RAM_BYTES-h.data_bytes || (h.required_services&~VM_SERVICES) ||
       file_bytes!=sizeof h+h.code_bytes+h.data_bytes || !(h.entry&1) ||
       (h.entry&~1u)<VM_CODE_BASE || (h.entry&~1u)>=VM_CODE_BASE+h.code_bytes) return false;
    for(auto r:h.reserved) if(r) return false;
    VmImageHeader check=h; check.header_crc=0;
    return vm_crc32(&check,sizeof check)==h.header_crc;
}
