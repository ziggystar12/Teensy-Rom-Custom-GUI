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
// Optional host-owned video transport. VIC_CELL10 is the first, deliberately
// narrow tier: the existing row-major 40x25 representation of eight bitmap
// bytes, screen byte and colour byte per cell. It transfers those three planes
// into an already-established VIC display; background must be zero and HIRES
// must match the current terminal mode. The VM then publishes its ordinary
// frame-end packet, which remains the owner of display-policy and audio commit.
// The submitted bytes remain module-owned and immutable for the duration of
// the synchronous call. Unavailable/Busy/Failed leave ownership with the VM,
// which may retry or use its ordinary packet receiver without changing state.
enum : uint32_t { VM_VIDEO_FORMAT_VIC_CELL10=1, VM_VIDEO_FLAG_HIRES=1 };
enum class VmVideoResult : uint32_t { Unavailable, Transferred, Busy, Failed };
struct VmVideoFrame {
    uint32_t bytes, format, flags, generation;
    uint16_t width, height, stride;
    uint8_t background, reserved;
    const uint8_t *pixels;
};
enum : uint32_t { VM_INDEXED_VIDEO_WORKSPACE_BYTES=24576 };
// Optional setup.reserved geometry flags. Zero preserves prior NES/DOS behavior.
enum : uint16_t { VM_INDEXED_NATIVE_HEIGHT=1, VM_INDEXED_DOUBLE_WIDTH=2 };
// Opt-in indexed service: packed RGB palette and row-major 8-bit indices.
// Modes 0 Color, 1 Auto-8, 2 Enhanced-25, 3 Sharp; capability bit = 1<<mode.
// Configuration lends an aligned, lifetime-long RAM1 workspace to firmware.
// Pixels/palette stay immutable across Busy until Transferred (including
// receiver resume ACK). Poll with the SAME generation. Never call from ISR.
// resolved_mode is output only; firmware owns hotkeys and mode selection.
struct VmIndexedVideoSetup {
    uint32_t bytes;void *workspace;uint32_t workspace_bytes;
    uint8_t default_mode,capabilities;uint16_t reserved;
};
struct VmIndexedFrame {
    uint32_t bytes,generation;
    const uint8_t *pixels,*palette;
    uint32_t pixel_bytes,palette_bytes;
    uint16_t width,height,stride,colors;
    uint8_t resolved_mode;
};
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
    VmVideoResult (*video_present)(const VmVideoFrame *frame);
    bool (*video_configure)(const VmIndexedVideoSetup *setup);
    VmVideoResult (*video_indexed)(VmIndexedFrame *frame);
};
// The video callback is a tail extension. Modules which do not require it may
// still run against an ABI-2 host whose VmHost ends immediately before it.
static constexpr uint32_t VM_HOST_BASE_BYTES=offsetof(VmHost,video_present);
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
                  VM_SERVICE_VIDEO=32, VM_SERVICES=31,
                  VM_SERVICE_INDEXED_VIDEO=64,
                  VM_HOST_SERVICES=VM_SERVICES|VM_SERVICE_VIDEO|VM_SERVICE_INDEXED_VIDEO,
                  VM_KNOWN_SERVICES=VM_HOST_SERVICES, VM_IMAGE_MAGIC=0x314d564d };
static inline uint32_t vm_crc32(const void *data, uint32_t size) {
    auto p=static_cast<const uint8_t *>(data); uint32_t c=~0u;
    while(size--) { c^=*p++; for(unsigned b=0;b<8;b++) c=(c>>1)^((0u-(c&1))&0xedb88320u); }
    return ~c;
}
static inline bool vm_valid_header(const VmImageHeader &h, uint32_t file_bytes) {
    if(h.magic!=VM_IMAGE_MAGIC || h.abi!=VM_ABI || h.header_bytes!=sizeof h ||
       h.code_base!=VM_CODE_BASE || h.ram_base!=VM_DATA_BASE || !h.code_bytes ||
       h.code_bytes>VM_CODE_LIMIT-VM_CODE_BASE || h.data_bytes>VM_DATA_BYTES ||
       h.bss_bytes>VM_DATA_BYTES-h.data_bytes || (h.required_services&~VM_KNOWN_SERVICES) ||
       file_bytes!=sizeof h+h.code_bytes+h.data_bytes || !(h.entry&1) ||
       (h.entry&~1u)<VM_CODE_BASE || (h.entry&~1u)>=VM_CODE_BASE+h.code_bytes) return false;
    for(auto r:h.reserved) if(r) return false;
    VmImageHeader check=h; check.header_crc=0;
    return vm_crc32(&check,sizeof check)==h.header_crc;
}
