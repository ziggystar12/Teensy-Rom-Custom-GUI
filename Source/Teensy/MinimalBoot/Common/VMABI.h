// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>

// Trusted-local ARMv7E-M hard-float modules. Not a security sandbox.
// All pointers and callbacks live until reset. Never call from an interrupt.
enum : uint32_t { VM_ABI = 2, VM_CODE_BASE = 0x18000, VM_CODE_LIMIT = 0x30000,
                  VM_DATA_BASE = 0x20014000, VM_DATA_LIMIT = 0x20044000,
                  VM_DATA_BYTES = VM_DATA_LIMIT-VM_DATA_BASE,
                  VM_RAM_BASE = 0x20200000, VM_RAM_BYTES = 512*1024 };
struct VmImageHeader {
    uint32_t magic, abi, header_bytes, code_bytes, data_bytes, bss_bytes;
    uint32_t entry, code_base, ram_base, required_services, payload_crc, header_crc;
    uint32_t reserved[4];
};
static_assert(sizeof(VmImageHeader)==64, "MVM1 image header");
struct VmFileInfo { uint32_t bytes; uint8_t directory; char name[96]; uint8_t attributes; uint16_t date,time; };
struct VmInput { uint8_t buttons, display, overflow, protocol; };
struct VmPacket { uint8_t type, flags, length, reserved; uint8_t payload[228]; };
enum : uint32_t { VM_OPEN_READ=1,VM_OPEN_WRITE=2,VM_OPEN_CREATE=4,VM_OPEN_EXCLUSIVE=8,VM_OPEN_TRUNCATE=16 };
enum class VmFsOp : uint32_t { Flush,Truncate,Timestamp,Close,Mkdir,Rmdir,Remove,Rename,Space };
struct VmFsRequest { VmFsOp operation; uint32_t handle,value,extra; const char *path,*destination; };
struct VmHost {
    uint32_t abi, bytes, services;
    uint8_t *workspace; uint32_t workspace_bytes;
    const char *package_root, *content_path;
    uint32_t (*micros_now)();
    // Handles 1..24; zero is failure. read returns -1 on error.
    uint32_t (*open)(const char *path, VmFileInfo *info);
    int32_t (*read)(uint32_t handle, uint32_t offset, void *data, uint32_t count);
    int32_t (*next)(uint32_t directory, VmFileInfo *info); // 1 entry, 0 EOF, -1 error
    void (*close)(uint32_t handle);
    // RAM1 workspace follows module data/BSS. RAM2 is a separate guest arena.
    uint8_t *guest_ram; uint32_t guest_ram_bytes;
    uint32_t (*open_flags)(const char *path,uint32_t flags,VmFileInfo *info);
    int32_t (*write)(uint32_t handle,uint32_t offset,const void *data,uint32_t count);
    // 0 success, -1 failure; Space returns total/free sectors in value/extra,
    // and allocation-unit size (sectors per cluster) in handle.
    int32_t (*file_op)(VmFsRequest *request);
    // Cooperative foreground yield for pending input, ACK, retry or time slice.
    bool (*should_yield)();
    void (*fail)(uint8_t code,uint32_t detail);
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
                  VM_SERVICE_WRITE=8, VM_SERVICE_GUEST_RAM=16,
                  VM_SERVICES=31, VM_IMAGE_MAGIC=0x314d564d };
static inline uint32_t vm_crc32(const void *data, uint32_t size) {
    auto p=static_cast<const uint8_t *>(data); uint32_t c=~0u;
    while(size--) { c^=*p++; for(unsigned b=0;b<8;b++) c=(c>>1)^((0u-(c&1))&0xedb88320u); }
    return ~c;
}
static inline bool vm_valid_header(const VmImageHeader &h, uint32_t file_bytes) {
    if(h.magic!=VM_IMAGE_MAGIC || h.abi!=VM_ABI || h.header_bytes!=sizeof h ||
       h.code_base!=VM_CODE_BASE || h.ram_base!=VM_DATA_BASE || !h.code_bytes ||
       h.code_bytes>VM_CODE_LIMIT-VM_CODE_BASE || h.data_bytes>VM_DATA_BYTES ||
       h.bss_bytes>VM_DATA_BYTES-h.data_bytes || (h.required_services&~VM_SERVICES) ||
       file_bytes!=sizeof h+h.code_bytes+h.data_bytes || !(h.entry&1) ||
       (h.entry&~1u)<VM_CODE_BASE || (h.entry&~1u)>=VM_CODE_BASE+h.code_bytes) return false;
    for(auto r:h.reserved) if(r) return false;
    VmImageHeader check=h; check.header_crc=0;
    return vm_crc32(&check,sizeof check)==h.header_crc;
}
