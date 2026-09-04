// 8086tiny: a tiny, highly functional, highly portable PC emulator/VM
// Copyright 2013-14, Adrian Cable (adrian.cable@gmail.com) - http://www.megalith.co.uk/8086tiny
//
// Revision 1.25
//
// This work is licensed under the MIT License. See included LICENSE.TXT.

#include <memory.h>

#ifdef MPE5_NATIVE
#include "../../mpe5_8086tiny.h"
#define MPE5_FUNCTION MPE5_CODE
#else
#define MPE5_FUNCTION
#include <time.h>
#include <sys/timeb.h>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#endif

#ifndef NO_GRAPHICS
#include "SDL.h"
#endif
#endif

// Emulator system constants
#define IO_PORT_COUNT 0x10000
#define RAM_SIZE 0x10FFF0
#define REGS_BASE 0xF0000
#define VIDEO_RAM_SIZE 0x10000

// Graphics/timer/keyboard update delays (explained later)
#ifndef GRAPHICS_UPDATE_DELAY
#define GRAPHICS_UPDATE_DELAY 360000
#endif
#define KEYBOARD_TIMER_UPDATE_DELAY 20000

// 16-bit register decodes
#define REG_AX 0
#define REG_CX 1
#define REG_DX 2
#define REG_BX 3
#define REG_SP 4
#define REG_BP 5
#define REG_SI 6
#define REG_DI 7

#define REG_ES 8
#define REG_CS 9
#define REG_SS 10
#define REG_DS 11

#define REG_ZERO 12
#define REG_SCRATCH 13

// 8-bit register decodes
#define REG_AL 0
#define REG_AH 1
#define REG_CL 2
#define REG_CH 3
#define REG_DL 4
#define REG_DH 5
#define REG_BL 6
#define REG_BH 7

// FLAGS register decodes
#define FLAG_CF 40
#define FLAG_PF 41
#define FLAG_AF 42
#define FLAG_ZF 43
#define FLAG_SF 44
#define FLAG_TF 45
#define FLAG_IF 46
#define FLAG_DF 47
#define FLAG_OF 48

// Lookup tables in the BIOS binary
#define TABLE_XLAT_OPCODE 8
#define TABLE_XLAT_SUBFUNCTION 9
#define TABLE_STD_FLAGS 10
#define TABLE_PARITY_FLAG 11
#define TABLE_BASE_INST_SIZE 12
#define TABLE_I_W_SIZE 13
#define TABLE_I_MOD_SIZE 14
#define TABLE_COND_JUMP_DECODE_A 15
#define TABLE_COND_JUMP_DECODE_B 16
#define TABLE_COND_JUMP_DECODE_C 17
#define TABLE_COND_JUMP_DECODE_D 18
#define TABLE_FLAGS_BITFIELDS 19

// Bitfields for TABLE_STD_FLAGS values
#define FLAGS_UPDATE_SZP 1
#define FLAGS_UPDATE_AO_ARITH 2
#define FLAGS_UPDATE_OC_LOGIC 4

// Helper macros
// Plain char is unsigned on Cortex-M7. Guest signed8-bit displacements,
// immediates and arithmetic below must explicitly use signed char.

#ifdef MPE5_NATIVE
#define MPE5_MEM(address) mpe5_detail::MemoryRef<unsigned char>(address)
#define MPE5_PORT(address) mpe5_detail::MemoryRef<unsigned char>(RAM_SIZE + (uint32_t)(address))
#else
#define MPE5_MEM(address) mem[address]
#define MPE5_PORT(address) io_ports[address]
#endif

// Decode mod, r_m and reg fields in instruction
#define DECODE_RM_REG scratch2_uint = 4 * !i_mod, \
					  op_to_addr = rm_addr = i_mod < 3 ? SEGREG(seg_override_en ? seg_override : bios_table_lookup[scratch2_uint + 3][i_rm], bios_table_lookup[scratch2_uint][i_rm], regs16[bios_table_lookup[scratch2_uint + 1][i_rm]] + bios_table_lookup[scratch2_uint + 2][i_rm] * i_data1+) : GET_REG_ADDR(i_rm), \
					  op_from_addr = GET_REG_ADDR(i_reg), \
					  i_d && (scratch_uint = op_from_addr, op_from_addr = rm_addr, op_to_addr = scratch_uint)

// Return memory-mapped register location (offset into mem array) for register #reg_id
#define GET_REG_ADDR(reg_id) (REGS_BASE + (i_w ? 2 * reg_id : 2 * reg_id + reg_id / 4 & 7))

// Returns number of top bit in operand (i.e. 8 for 8-bit operands, 16 for 16-bit operands)
#define TOP_BIT 8*(i_w + 1)

// Opcode execution unit helpers
#define OPCODE ;break; case
#define OPCODE_CHAIN ; case

// [I]MUL/[I]DIV/DAA/DAS/ADC/SBB helpers
#define MUL_MACRO(op_data_type,out_regs) (set_opcode(0x10), \
										  out_regs[i_w + 1] = (op_result = CAST(op_data_type)MPE5_MEM(rm_addr) * (op_data_type)*out_regs) >> 16, \
										  regs16[REG_AX] = op_result, \
										  set_OF(set_CF(op_result - (op_data_type)op_result)))
#define DIV_MACRO(out_data_type,in_data_type,out_regs) (scratch_int = CAST(out_data_type)MPE5_MEM(rm_addr)) && !(scratch2_uint = (in_data_type)(scratch_uint = (out_regs[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (out_data_type)scratch2_uint) ? out_regs[i_w+1] = scratch_uint - scratch_int * (*out_regs = scratch2_uint) : pc_interrupt(0)
#define DAA_DAS(op1,op2,mask,min) set_AF((((scratch2_uint = regs8[REG_AL]) & 0x0F) > 9) || regs8[FLAG_AF]) && (op_result = regs8[REG_AL] op1 6, set_CF(regs8[FLAG_CF] || (regs8[REG_AL] op2 scratch2_uint))), \
								  set_CF((((mask & 1 ? scratch2_uint : regs8[REG_AL]) & mask) > min) || regs8[FLAG_CF]) && (op_result = regs8[REG_AL] op1 0x60)
#define ADC_SBB_MACRO(a) OP(a##= regs8[FLAG_CF] +), \
						 set_CF(regs8[FLAG_CF] && (op_result == op_dest) || (a op_result < a(int)op_dest)), \
						 set_AF_OF_arith()

// Execute arithmetic/logic operations in emulator memory/registers
#define R_M_OP(dest,op,src) (i_w ? op_dest = CAST(unsigned short)dest, op_result = CAST(unsigned short)dest op (op_source = CAST(unsigned short)src) \
								 : (op_dest = dest, op_result = dest op (op_source = CAST(unsigned char)src)))
#define MEM_OP(dest,op,src) R_M_OP(MPE5_MEM(dest),op,MPE5_MEM(src))
#define OP(op) MEM_OP(op_to_addr,op,op_from_addr)

// Increment or decrement a register #reg_id (usually SI or DI), depending on direction flag and operand size (given by i_w)
#define INDEX_INC(reg_id) (regs16[reg_id] -= (2 * regs8[FLAG_DF] - 1)*(i_w + 1))

// Helpers for stack operations
#define R_M_PUSH(a) (i_w = 1, R_M_OP(MPE5_MEM(SEGREG(REG_SS, REG_SP, --)), =, a))
#define R_M_POP(a) (i_w = 1, regs16[REG_SP] += 2, R_M_OP(a, =, MPE5_MEM(SEGREG(REG_SS, REG_SP, -2+))))

// Convert segment:offset to linear address in emulator memory space
#define SEGREG(reg_seg,reg_ofs,op) 16 * regs16[reg_seg] + (unsigned short)(op regs16[reg_ofs])

// Returns sign bit of an 8-bit or 16-bit operand
#ifdef MPE5_NATIVE
#define SIGN_OF(a) (1 & (i_w ? (short)(CAST(short)a) : (unsigned char)(a)) >> (TOP_BIT - 1))
#else
#define SIGN_OF(a) (1 & (i_w ? CAST(short)a : a) >> (TOP_BIT - 1))
#endif

// Reinterpretation cast
#ifdef MPE5_NATIVE
#define CAST(a) (mpe5_detail::Caster<a>{}) *
#else
#define CAST(a) *(a*)&
#endif

// Keyboard driver for console. This may need changing for UNIX/non-UNIX platforms
#ifdef MPE5_NATIVE
static MPE5_FUNCTION bool MPE5VendorKeyboardPoll();
static MPE5_FUNCTION bool MPE5VendorDisk(bool write, uint32_t &remaining);
static MPE5_FUNCTION void MPE5VendorSpeaker(uint16_t port, uint8_t value);
#define KEYBOARD_DRIVER MPE5VendorKeyboardPoll()
#elif defined(_WIN32)
#define KEYBOARD_DRIVER kbhit() && (MPE5_MEM(0x4A6) = getch(), pc_interrupt(7))
#else
#define KEYBOARD_DRIVER read(0, mem + 0x4A6, 1) && (int8_asap = (MPE5_MEM(0x4A6) == 0x1B), pc_interrupt(7))
#endif

// Keyboard driver for SDL
#ifdef NO_GRAPHICS
#define SDL_KEYBOARD_DRIVER KEYBOARD_DRIVER
#else
#define SDL_KEYBOARD_DRIVER sdl_screen ? SDL_PollEvent(&sdl_event) && (sdl_event.type == SDL_KEYDOWN || sdl_event.type == SDL_KEYUP) && (scratch_uint = sdl_event.key.keysym.unicode, scratch2_uint = sdl_event.key.keysym.mod, CAST(short)MPE5_MEM(0x4A6) = 0x400 + 0x800*!!(scratch2_uint & KMOD_ALT) + 0x1000*!!(scratch2_uint & KMOD_SHIFT) + 0x2000*!!(scratch2_uint & KMOD_CTRL) + 0x4000*(sdl_event.type == SDL_KEYUP) + ((!scratch_uint || scratch_uint > 0x7F) ? sdl_event.key.keysym.sym : scratch_uint), pc_interrupt(7)) : (KEYBOARD_DRIVER)
#endif

// Global variable definitions
// The native target receives the complete 20-bit map from the exclusive
// PSRAM arena. The original standalone build retains its static map.
#ifdef MPE5_NATIVE
unsigned char *mem;
#else
unsigned char mem[RAM_SIZE];
#endif
#ifdef MPE5_NATIVE
// Port latches live behind the host memory adapter. The decoder table is
// caller-owned so the reset-only Teensy target can keep it outside RAM2.
unsigned char *io_ports, (*bios_table_lookup)[256];
#else
unsigned char io_ports[IO_PORT_COUNT], bios_table_lookup[20][256];
#endif
#ifdef MPE5_NATIVE
unsigned char *opcode_stream, *regs8, i_rm, i_w, i_reg, i_mod, i_mod_size, i_d, i_reg4bit, raw_opcode_id, xlat_opcode_id, extra, rep_mode, seg_override_en, rep_override_en, trap_flag, int8_asap, scratch_uchar, io_hi_lo, *vid_mem_base, spkr_en;
unsigned short *regs16, reg_ip, seg_override, file_index, wave_counter;
unsigned int op_source, op_dest, rm_addr, op_to_addr, op_from_addr, i_data0, i_data1, i_data2, scratch_uint, scratch2_uint, inst_counter, set_flags_type, GRAPHICS_X, GRAPHICS_Y, pixel_colors[16], vmem_ctr;
int op_result, disk[3], scratch_int;
#else
unsigned char *opcode_stream, *regs8, i_rm, i_w, i_reg, i_mod, i_mod_size, i_d, i_reg4bit, raw_opcode_id, xlat_opcode_id, extra, rep_mode, seg_override_en, rep_override_en, trap_flag, int8_asap, scratch_uchar, io_hi_lo, *vid_mem_base, spkr_en;
unsigned short *regs16, reg_ip, seg_override, file_index, wave_counter;
unsigned int op_source, op_dest, rm_addr, op_to_addr, op_from_addr, i_data0, i_data1, i_data2, scratch_uint, scratch2_uint, inst_counter, set_flags_type, GRAPHICS_X, GRAPHICS_Y, pixel_colors[16], vmem_ctr;
int op_result, disk[3], scratch_int;
#endif
#ifndef MPE5_NATIVE
time_t clock_buf;
struct timeb ms_clock;
#endif

#ifndef NO_GRAPHICS
SDL_AudioSpec sdl_audio = {44100, AUDIO_U8, 1, 0, 128};
SDL_Surface *sdl_screen;
SDL_Event sdl_event;
unsigned short vid_addr_lookup[VIDEO_RAM_SIZE], cga_colors[4] = {0 /* Black */, 0x1F1F /* Cyan */, 0xE3E3 /* Magenta */, 0xFFFF /* White */};
#endif

// Helper functions

// Set carry flag
MPE5_FUNCTION char set_CF(int new_CF)
{
	return regs8[FLAG_CF] = !!new_CF;
}

// Set auxiliary flag
MPE5_FUNCTION char set_AF(int new_AF)
{
	return regs8[FLAG_AF] = !!new_AF;
}

// Set overflow flag
MPE5_FUNCTION char set_OF(int new_OF)
{
	return regs8[FLAG_OF] = !!new_OF;
}

// Set auxiliary and overflow flag after arithmetic operations
MPE5_FUNCTION char set_AF_OF_arith()
{
	set_AF((op_source ^= op_dest ^ op_result) & 0x10);
	if (op_result == op_dest)
		return set_OF(0);
	else
		return set_OF(1 & (regs8[FLAG_CF] ^ op_source >> (TOP_BIT - 1)));
}

// Assemble and return emulated CPU FLAGS register in scratch_uint
MPE5_FUNCTION void make_flags()
{
	scratch_uint = 0xF002; // 8086 has reserved and unused flags set to 1
	for (int i = 9; i--;)
		scratch_uint += regs8[FLAG_CF + i] << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i];
}

// Set emulated CPU FLAGS register from regs8[FLAG_xx] values
MPE5_FUNCTION void set_flags(int new_flags)
{
	for (int i = 9; i--;)
		regs8[FLAG_CF + i] = !!(1 << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i] & new_flags);
}

// Convert raw opcode to translated opcode index. This condenses a large number of different encodings of similar
// instructions into a much smaller number of distinct functions, which we then execute
MPE5_FUNCTION void set_opcode(unsigned char opcode)
{
	xlat_opcode_id = bios_table_lookup[TABLE_XLAT_OPCODE][raw_opcode_id = opcode];
	extra = bios_table_lookup[TABLE_XLAT_SUBFUNCTION][opcode];
	i_mod_size = bios_table_lookup[TABLE_I_MOD_SIZE][opcode];
	set_flags_type = bios_table_lookup[TABLE_STD_FLAGS][opcode];
}

// Execute INT #interrupt_num on the emulated machine
MPE5_FUNCTION char pc_interrupt(unsigned char interrupt_num)
{
	set_opcode(0xCD); // Decode like INT

	make_flags();
	R_M_PUSH(scratch_uint);
	R_M_PUSH(regs16[REG_CS]);
	R_M_PUSH(reg_ip);
	MEM_OP(REGS_BASE + 2 * REG_CS, =, 4 * interrupt_num + 2);
	R_M_OP(reg_ip, =, MPE5_MEM(4 * interrupt_num));

	return regs8[FLAG_TF] = regs8[FLAG_IF] = 0;
}

// AAA and AAS instructions - which_operation is +1 for AAA, and -1 for AAS
MPE5_FUNCTION int AAA_AAS(signed char which_operation)
{
	return (regs16[REG_AX] += 262 * which_operation*set_AF(set_CF(((regs8[REG_AL] & 0x0F) > 9) || regs8[FLAG_AF])), regs8[REG_AL] &= 0x0F);
}

#ifndef NO_GRAPHICS
void audio_callback(void *data, unsigned char *stream, int len)
{
	for (int i = 0; i < len; i++)
		stream[i] = (spkr_en == 3) && CAST(unsigned short)MPE5_MEM(0x4AA) ? -((54 * wave_counter++ / CAST(unsigned short)MPE5_MEM(0x4AA)) & 1) : sdl_audio.silence;

	spkr_en = MPE5_PORT(0x61) & 3;
}
#endif

// The desktop entry remains unchanged. The native entry accepts storage and
// device callbacks, then its execution loop is called in finite slices.
#ifdef MPE5_NATIVE
// Reset explicitly before every native start; no constructor or vtable.
static mpe5::CoreHost MPE5Host;
static mpe5::CoreDiagnostic MPE5Diagnostic;
static mpe5::VideoState MPE5Video;
static uint8_t MPE5VideoCrtcIndex;
static uint32_t MPE5ClockStart;
static uint32_t MPE5ClockLastInstruction;
static uint64_t MPE5ClockInstructions;
// Native held-key snapshots carry their own make/break timing. Do not wait
// for the much slower BIOS timer poll to deliver them. Start the interval
// after IRQ1 returns so a make and its break cannot both run before the
// interrupted program gets an opportunity to observe the held state.
static constexpr uint16_t MPE5KeyboardInterval = 512u;
// Slow DOS games poll raw keyboard state hundreds of thousands of guest
// instructions apart. Preserve a quick physical Shift/cursor tap until one
// such poll can observe it, while printable make/break pairs retain the short
// IRQ recovery interval above.
static constexpr uint32_t MPE5StatefulMinimumInstructions = 550000u;
static uint16_t MPE5KeyboardResumeCS, MPE5KeyboardResumeIP;
static uint16_t MPE5KeyboardResumeSS, MPE5KeyboardResumeSP;
static uint16_t MPE5KeyboardCooldown;
static uint32_t MPE5StatefulMakeInstruction[5];
static uint8_t MPE5StatefulDown;
static bool MPE5KeyboardAwaitResume;
static bool MPE5Ready;
static bool MPE5MemoryFailed, MPE5RepeatPending, MPE5DiskPending;
static uint8_t MPE5OpcodeBytes[8];
static uint8_t *MPE5ConsoleShadow, *MPE5ConsoleViewport;
static uint32_t MPE5DiskTarget, MPE5DiskLba, MPE5DiskLength, MPE5DiskOffset;
static uint16_t MPE5TextCursor;
static uint8_t MPE5TextEscapeState, MPE5TextParameterCount;
static uint16_t MPE5TextParameters[2];
static uint8_t MPE5TextScrollTop, MPE5TextScrollBottom;
static bool MPE5TextWrapPending;
static MPE5_FUNCTION void MPE5VendorPutChar(uint8_t character);
static MPE5_FUNCTION void MPE5VendorReset()
{
	MPE5Ready = false;
	MPE5MemoryFailed = MPE5RepeatPending = MPE5DiskPending = false;
	MPE5DiskTarget = MPE5DiskLba = MPE5DiskLength = MPE5DiskOffset = 0;
	MPE5Host = {};
	MPE5Diagnostic = {};
	MPE5Video = {};
	MPE5VideoCrtcIndex = 0;
	MPE5ClockStart = 0;
	MPE5ClockLastInstruction = 0;
	MPE5ClockInstructions = 0;
	MPE5KeyboardResumeCS = MPE5KeyboardResumeIP = 0;
	MPE5KeyboardResumeSS = MPE5KeyboardResumeSP = 0;
	MPE5KeyboardCooldown = 0;
	memset(MPE5StatefulMakeInstruction, 0, sizeof(MPE5StatefulMakeInstruction));
	MPE5StatefulDown = 0;
	MPE5KeyboardAwaitResume = false;
	// Teensy RAM2's DMAMEM section is NOLOAD and is not cleared by startup.
	// Reset every native interpreter field explicitly on both cold launch and
	// reuse. In particular, stale prefixes or TF can interrupt the BIOS before
	// it installs vectors; clearing guest memory alone does not reset the CPU.
	mem = io_ports = opcode_stream = regs8 = vid_mem_base = 0;
	bios_table_lookup = 0;
	regs16 = 0;
	MPE5ConsoleShadow = MPE5ConsoleViewport = 0;
	memset(MPE5OpcodeBytes, 0, sizeof(MPE5OpcodeBytes));
	i_rm = i_w = i_reg = i_mod = i_mod_size = i_d = i_reg4bit =
		raw_opcode_id = xlat_opcode_id = extra = rep_mode = seg_override_en =
		rep_override_en = trap_flag = int8_asap = scratch_uchar = io_hi_lo =
		spkr_en = 0;
	reg_ip = seg_override = file_index = wave_counter = 0;
	op_source = op_dest = rm_addr = op_to_addr = op_from_addr = i_data0 =
		i_data1 = i_data2 = scratch_uint = scratch2_uint = inst_counter =
		set_flags_type = GRAPHICS_X = GRAPHICS_Y = vmem_ctr = 0;
	op_result = scratch_int = 0;
	memset(pixel_colors, 0, sizeof(pixel_colors));
	memset(disk, 0, sizeof(disk));
	MPE5TextCursor = 0;
	MPE5TextEscapeState = MPE5TextParameterCount = 0;
	memset(MPE5TextParameters, 0, sizeof(MPE5TextParameters));
	MPE5TextScrollTop = 0;
	MPE5TextScrollBottom = mpe5::CgaTextRows - 1u;
	MPE5TextWrapPending = false;
}

static MPE5_FUNCTION bool MPE5VendorStart(const mpe5::CoreHost &host)
#else
int main(int argc, char **argv)
#endif
{
#ifndef NO_GRAPHICS
	// Initialise SDL
	SDL_Init(SDL_INIT_AUDIO);
	sdl_audio.callback = audio_callback;
#ifdef _WIN32
	sdl_audio.samples = 512;
#endif
	SDL_OpenAudio(&sdl_audio, 0);
#endif

	// regs16 and reg8 point to F000:0, the start of memory-mapped registers. CS is initialised to F000
#ifdef MPE5_NATIVE
	MPE5VendorReset();
	const bool paged = host.memory.read || host.memory.write || host.memory.reset;
	if (!host.bios ||
		!host.biosBytes || host.biosBytes > 0xFF00 || !host.drive.readSector ||
		!host.drive.sectors || !host.decodeTable ||
		host.decodeTableBytes < 20u * 256u ||
		(paged ? (!host.memory.read || !host.memory.write || !host.memory.reset ||
			(host.conventionalRam ? (((uintptr_t)host.conventionalRam & 1u) ||
				host.conventionalRamBytes < mpe5::ConventionalRamBytes) :
				host.conventionalRamBytes != 0u) ||
			!host.fixedF000 || ((uintptr_t)host.fixedF000 & 1u) || host.fixedF000Bytes < 0x10000u ||
			!host.consoleShadow || !host.consoleViewport) :
			(!host.addressMap || ((uintptr_t)host.addressMap & 1u) || host.addressMapBytes < mpe5::NativeBackingBytes)))
		return false;
	MPE5Host = host;
	bios_table_lookup = (unsigned char (*)[256])host.decodeTable;
	MPE5ClockStart = host.milliseconds ? host.milliseconds() : 0;
	if (paged)
	{
		if (!host.memory.reset(host.memory.context)) return false;
		regs8 = host.fixedF000;
		// The cartridge BIOS may arrive in the same buffer used for the
		// permanent F000 segment. Move it before clearing register/tail bytes.
		memmove(regs8 + 0x100, host.bios, host.biosBytes);
		memset(regs8, 0, 0x100);
		memset(regs8 + 0x100 + host.biosBytes, 0, 0xFF00u - host.biosBytes);
		MPE5ConsoleShadow = host.consoleShadow;
		MPE5ConsoleViewport = host.consoleViewport;
	}
	else
	{
		mem = host.addressMap;
		io_ports = mem + RAM_SIZE;
		memset(mem, 0, RAM_SIZE + IO_PORT_COUNT);
		regs8 = mem + REGS_BASE;
		MPE5ConsoleShadow = mem + mpe5::NativeTextShadowAddress;
		MPE5ConsoleViewport = mem + mpe5::NativeTextViewportAddress;
	}
	for (uint32_t offset = 0; offset < mpe5::NativeTextColumns * mpe5::CgaTextRows * 2u; offset += 2u)
	{ MPE5ConsoleShadow[offset] = ' '; MPE5ConsoleShadow[offset + 1u] = 7; }
	for (uint32_t offset = 0; offset < mpe5::CgaTextCells * 2u; offset += 2u)
	{ MPE5ConsoleViewport[offset] = ' '; MPE5ConsoleViewport[offset + 1u] = 7; }
	regs16 = (unsigned short *)regs8;
#else
	regs16 = (unsigned short *)(regs8 = mem + REGS_BASE);
#endif
	regs16[REG_CS] = 0xF000;

	// Trap flag off
	regs8[FLAG_TF] = 0;

	// Set DL equal to the boot device: 0 for the FD, or 0x80 for the HD. Normally, boot from the FD.
	// But, if the HD image file is prefixed with @, then boot from the HD
	regs8[REG_DL] =
#ifdef MPE5_NATIVE
		0x80;
#else
		((argc > 3) && (*argv[3] == '@')) ? argv[3]++, 0x80 : 0;
#endif

	// Open BIOS (file id disk[2]), floppy disk image (disk[1]), and hard disk image (disk[0]) if specified
	#ifndef MPE5_NATIVE
	for (file_index = 3; file_index;)
		disk[--file_index] = *++argv ? open(*argv, 32898) : 0;
	#endif

	// Set CX:AX equal to the hard disk image size, if present
	#ifdef MPE5_NATIVE
	regs16[REG_AX] = (uint16_t)MPE5Host.drive.sectors;
	regs16[REG_CX] = (uint16_t)(MPE5Host.drive.sectors >> 16);
	#else
	CAST(unsigned)regs16[REG_AX] = *disk ? lseek(*disk, 0, 2) >> 9 : 0;
	#endif

	// Load BIOS image into F000:0100, and set IP to 0100
	reg_ip = 0x100;
	#ifdef MPE5_NATIVE
	if (!paged && MPE5Host.bios != regs8 + reg_ip)
		memcpy(regs8 + reg_ip, MPE5Host.bios, MPE5Host.biosBytes);
	if (!mpe5::patchBiosConventionalMemory(regs8 + reg_ip,
		MPE5Host.biosBytes)) return false;
	#else
	read(disk[2], regs8 + reg_ip, 0xFF00);
	#endif

	// Load instruction decoding helper table
	if (paged) memset(bios_table_lookup, 0, 20u * 256u);
	for (int i = 0; i < 20; i++)
		for (int j = 0; j < 256; j++)
			bios_table_lookup[i][j] = regs8[regs16[0x81 + i] + j];

	// Instruction execution loop. Terminates if CS:IP = 0:0.
#ifdef MPE5_NATIVE
	MPE5Ready = true;
	return true;
}

static MPE5_FUNCTION bool MPE5VendorRun(uint32_t budget)
{
	if (!MPE5Ready || !budget) return MPE5Ready;
	for (uint32_t remaining = budget; remaining && !MPE5MemoryFailed;)
#else
	for (; opcode_stream = mem + 16 * regs16[REG_CS] + reg_ip, opcode_stream != mem;)
#endif
	{
		#ifdef MPE5_NATIVE
		if (MPE5KeyboardAwaitResume)
		{
			if (regs16[REG_CS] == MPE5KeyboardResumeCS && reg_ip == MPE5KeyboardResumeIP &&
				regs16[REG_SS] == MPE5KeyboardResumeSS && regs16[REG_SP] == MPE5KeyboardResumeSP)
			{
				MPE5KeyboardAwaitResume = false;
				MPE5KeyboardCooldown = MPE5KeyboardInterval;
			}
		}
		else if (MPE5KeyboardCooldown && regs8[FLAG_IF]) --MPE5KeyboardCooldown;
		const uint8_t savedSegOverride = seg_override_en, savedRepOverride = rep_override_en;
		if (!MPE5RepeatPending && !MPE5DiskPending)
		{
			const uint32_t instructionAddress = 16u * regs16[REG_CS] + reg_ip;
			if (!instructionAddress) {
				mpe5_detail::recordFailure(mpe5::CoreStop::Stopped, instructionAddress);
				MPE5Ready = false; break;
			}
			// Decode touches offsets0..5. Copy them before another operand
			// can evict the instruction's page, including a cross-page fetch.
			if (MPE5Host.conventionalRam &&
				instructionAddress <= MPE5Host.conventionalRamBytes &&
				6u <= MPE5Host.conventionalRamBytes - instructionAddress)
				memcpy(MPE5OpcodeBytes, MPE5Host.conventionalRam + instructionAddress, 6);
			else if (MPE5Host.fixedF000 && instructionAddress >= 0xf0000u &&
				instructionAddress <= 0x100000u - 6u)
				memcpy(MPE5OpcodeBytes, MPE5Host.fixedF000 + instructionAddress - 0xf0000u, 6);
			else if (!mpe5_detail::readBytes(instructionAddress, MPE5OpcodeBytes, 6)) break;
		}
		MPE5RepeatPending = false;
		opcode_stream = MPE5OpcodeBytes;
		--remaining;
		#endif
		// Set up variables to prepare for decoding an opcode
		set_opcode(*opcode_stream);

		// Extract i_w and i_d fields from instruction
		i_w = (i_reg4bit = raw_opcode_id & 7) & 1;
		i_d = i_reg4bit / 2 & 1;

		// Extract instruction data fields
		#ifdef MPE5_NATIVE
		i_data0 = (short)((uint16_t)opcode_stream[1] | (uint16_t)opcode_stream[2] << 8);
		i_data1 = (short)((uint16_t)opcode_stream[2] | (uint16_t)opcode_stream[3] << 8);
		i_data2 = (short)((uint16_t)opcode_stream[3] | (uint16_t)opcode_stream[4] << 8);
		#else
		i_data0 = CAST(short)opcode_stream[1];
		i_data1 = CAST(short)opcode_stream[2];
		i_data2 = CAST(short)opcode_stream[3];
		#endif

		// seg_override_en and rep_override_en contain number of instructions to hold segment override and REP prefix respectively
		if (seg_override_en)
			seg_override_en--;
		if (rep_override_en)
			rep_override_en--;

		// i_mod_size > 0 indicates that opcode uses i_mod/i_rm/i_reg, so decode them
		if (i_mod_size)
		{
			i_mod = (i_data0 & 0xFF) >> 6;
			i_rm = i_data0 & 7;
			i_reg = i_data0 / 8 & 7;

			if ((!i_mod && i_rm == 6) || (i_mod == 2))
				#ifdef MPE5_NATIVE
				i_data2 = (short)((uint16_t)opcode_stream[4] | (uint16_t)opcode_stream[5] << 8);
				#else
				i_data2 = CAST(short)opcode_stream[4];
				#endif
			else if (i_mod != 1)
				i_data2 = i_data1;
			else // If i_mod is 1, operand is (usually) 8 bits rather than 16 bits
				i_data1 = (signed char)i_data1;

			DECODE_RM_REG;
		}

		// Instruction execution unit
		switch (xlat_opcode_id)
		{
			OPCODE_CHAIN 0: // Conditional jump (JAE, JNAE, etc.)
				// i_w is the invert flag, e.g. i_w == 1 means JNAE, whereas i_w == 0 means JAE 
				scratch_uchar = raw_opcode_id / 2 & 7;
				reg_ip += (signed char)i_data0 * (i_w ^ (regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_A][scratch_uchar]] || regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_B][scratch_uchar]] || regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_C][scratch_uchar]] ^ regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_D][scratch_uchar]]))
			OPCODE 1: // MOV reg, imm
				i_w = !!(raw_opcode_id & 8);
				R_M_OP(MPE5_MEM(GET_REG_ADDR(i_reg4bit)), =, i_data0)
			OPCODE 3: // PUSH regs16
				R_M_PUSH(regs16[i_reg4bit])
			OPCODE 4: // POP regs16
				R_M_POP(regs16[i_reg4bit])
			OPCODE 2: // INC|DEC regs16
				i_w = 1;
				i_d = 0;
				i_reg = i_reg4bit;
				DECODE_RM_REG;
				i_reg = extra
			OPCODE_CHAIN 5: // INC|DEC|JMP|CALL|PUSH
				if (i_reg < 2) // INC|DEC
					MEM_OP(op_from_addr, += 1 - 2 * i_reg +, REGS_BASE + 2 * REG_ZERO),
					op_source = 1,
					set_AF_OF_arith(),
					set_OF(op_dest + 1 - i_reg == 1 << (TOP_BIT - 1)),
					(xlat_opcode_id == 5) && (set_opcode(0x10), 0); // Decode like ADC
				else if (i_reg != 6) // JMP|CALL
					i_reg - 3 || R_M_PUSH(regs16[REG_CS]), // CALL (far)
					i_reg & 2 && R_M_PUSH(reg_ip + 2 + i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6)), // CALL (near or far)
					i_reg & 1 && (regs16[REG_CS] = CAST(short)MPE5_MEM(op_from_addr + 2)), // JMP|CALL (far)
					R_M_OP(reg_ip, =, MPE5_MEM(op_from_addr)),
					set_opcode(0x9A); // Decode like CALL
				else // PUSH
					R_M_PUSH(MPE5_MEM(rm_addr))
			OPCODE 6: // TEST r/m, imm16 / NOT|NEG|MUL|IMUL|DIV|IDIV reg
				op_to_addr = op_from_addr;

				switch (i_reg)
				{
					OPCODE_CHAIN 0: // TEST
						set_opcode(0x20); // Decode like AND
						reg_ip += i_w + 1;
						R_M_OP(MPE5_MEM(op_to_addr), &, i_data2)
					OPCODE 2: // NOT
						OP(=~)
					OPCODE 3: // NEG
						OP(=-);
						op_dest = 0;
						set_opcode(0x28); // Decode like SUB
						set_CF(op_result > op_dest)
					OPCODE 4: // MUL
						i_w ? MUL_MACRO(unsigned short, regs16) : MUL_MACRO(unsigned char, regs8)
					OPCODE 5: // IMUL
						i_w ? MUL_MACRO(short, regs16) : MUL_MACRO(signed char, regs8)
					OPCODE 6: // DIV
						i_w ? DIV_MACRO(unsigned short, unsigned, regs16) : DIV_MACRO(unsigned char, unsigned short, regs8)
					OPCODE 7: // IDIV
						i_w ? DIV_MACRO(short, int, regs16) : DIV_MACRO(signed char, short, regs8);
				}
			OPCODE 7: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP AL/AX, immed
				rm_addr = REGS_BASE;
				i_data2 = i_data0;
				i_mod = 3;
				i_reg = extra;
				reg_ip--;
			OPCODE_CHAIN 8: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP reg, immed
				op_to_addr = rm_addr;
				regs16[REG_SCRATCH] = (i_d |= !i_w) ? (signed char)i_data2 : i_data2;
				op_from_addr = REGS_BASE + 2 * REG_SCRATCH;
				reg_ip += !i_d + 1;
				set_opcode(0x08 * (extra = i_reg));
			OPCODE_CHAIN 9: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP|MOV reg, r/m
				switch (extra)
				{
					OPCODE_CHAIN 0: // ADD
						OP(+=),
						set_CF(op_result < op_dest)
					OPCODE 1: // OR
						OP(|=)
					OPCODE 2: // ADC
						ADC_SBB_MACRO(+)
					OPCODE 3: // SBB
						ADC_SBB_MACRO(-)
					OPCODE 4: // AND
						OP(&=)
					OPCODE 5: // SUB
						OP(-=),
						set_CF(op_result > op_dest)
					OPCODE 6: // XOR
						OP(^=)
					OPCODE 7: // CMP
						OP(-),
						set_CF(op_result > op_dest)
					OPCODE 8: // MOV
						OP(=);
				}
			OPCODE 10: // MOV sreg, r/m | POP r/m | LEA reg, r/m
				if (!i_w) // MOV
					i_w = 1,
					i_reg += 8,
					DECODE_RM_REG,
					OP(=);
				else if (!i_d) // LEA
					seg_override_en = 1,
					seg_override = REG_ZERO,
					DECODE_RM_REG,
					R_M_OP(MPE5_MEM(op_from_addr), =, rm_addr);
				else // POP
					R_M_POP(MPE5_MEM(rm_addr))
			OPCODE 11: // MOV AL/AX, [loc]
				i_mod = i_reg = 0;
				i_rm = 6;
				i_data1 = i_data0;
				DECODE_RM_REG;
				MEM_OP(op_from_addr, =, op_to_addr)
			OPCODE 12: // ROL|ROR|RCL|RCR|SHL|SHR|???|SAR reg/mem, 1/CL/imm (80186)
				scratch2_uint = SIGN_OF(MPE5_MEM(rm_addr)),
				scratch_uint = extra ? // xxx reg/mem, imm
					++reg_ip,
					(signed char)i_data1
				: // xxx reg/mem, CL
					i_d
						? 31 & regs8[REG_CL]
				: // xxx reg/mem, 1
					1;
				if (scratch_uint)
				{
					if (i_reg < 4) // Rotate operations
						scratch_uint %= i_reg / 2 + TOP_BIT,
						R_M_OP(scratch2_uint, =, MPE5_MEM(rm_addr));
					if (i_reg & 1) // Rotate/shift right operations
						R_M_OP(MPE5_MEM(rm_addr), >>=, scratch_uint);
					else // Rotate/shift left operations
						R_M_OP(MPE5_MEM(rm_addr), <<=, scratch_uint);
					if (i_reg > 3) // Shift operations
						set_opcode(0x10); // Decode like ADC
					if (i_reg > 4) // SHR or SAR
						set_CF(op_dest >> (scratch_uint - 1) & 1);
				}

				switch (i_reg)
				{
					OPCODE_CHAIN 0: // ROL
						R_M_OP(MPE5_MEM(rm_addr), += , scratch2_uint >> (TOP_BIT - scratch_uint));
						set_OF(SIGN_OF(op_result) ^ set_CF(op_result & 1))
					OPCODE 1: // ROR
						scratch2_uint &= (1 << scratch_uint) - 1,
						R_M_OP(MPE5_MEM(rm_addr), += , scratch2_uint << (TOP_BIT - scratch_uint));
						set_OF(SIGN_OF(op_result * 2) ^ set_CF(SIGN_OF(op_result)))
					OPCODE 2: // RCL
						R_M_OP(MPE5_MEM(rm_addr), += (regs8[FLAG_CF] << (scratch_uint - 1)) + , scratch2_uint >> (1 + TOP_BIT - scratch_uint));
						set_OF(SIGN_OF(op_result) ^ set_CF(scratch2_uint & 1 << (TOP_BIT - scratch_uint)))
					OPCODE 3: // RCR
						R_M_OP(MPE5_MEM(rm_addr), += (regs8[FLAG_CF] << (TOP_BIT - scratch_uint)) + , scratch2_uint << (1 + TOP_BIT - scratch_uint));
						set_CF(scratch2_uint & 1 << (scratch_uint - 1));
						set_OF(SIGN_OF(op_result) ^ SIGN_OF(op_result * 2))
					OPCODE 4: // SHL
						set_OF(SIGN_OF(op_result) ^ set_CF(SIGN_OF(op_dest << (scratch_uint - 1))))
					OPCODE 5: // SHR
						set_OF(SIGN_OF(op_dest))
					OPCODE 7: // SAR
						scratch_uint < TOP_BIT || set_CF(scratch2_uint);
						set_OF(0);
						R_M_OP(MPE5_MEM(rm_addr), +=, scratch2_uint *= ~(((1 << TOP_BIT) - 1) >> scratch_uint));
				}
			OPCODE 13: // LOOPxx|JCZX
				scratch_uint = !!--regs16[REG_CX];

				switch(i_reg4bit)
				{
					OPCODE_CHAIN 0: // LOOPNZ
						scratch_uint &= !regs8[FLAG_ZF]
					OPCODE 1: // LOOPZ
						scratch_uint &= regs8[FLAG_ZF]
					OPCODE 3: // JCXXZ
						scratch_uint = !++regs16[REG_CX];
				}
				reg_ip += scratch_uint*(signed char)i_data0
			OPCODE 14: // JMP | CALL short/near
				reg_ip += 3 - i_d;
				if (!i_w)
				{
					if (i_d) // JMP far
						reg_ip = 0,
						regs16[REG_CS] = i_data2;
					else // CALL
						R_M_PUSH(reg_ip);
				}
				reg_ip += i_d && i_w ? (signed char)i_data0 : i_data0
			OPCODE 15: // TEST reg, r/m
				MEM_OP(op_from_addr, &, op_to_addr)
			OPCODE 16: // XCHG AX, regs16
				i_w = 1;
				op_to_addr = REGS_BASE;
				op_from_addr = GET_REG_ADDR(i_reg4bit);
			OPCODE_CHAIN 24: // NOP|XCHG reg, r/m
				if (op_to_addr != op_from_addr)
					OP(^=),
					MEM_OP(op_from_addr, ^=, op_to_addr),
					OP(^=)
			OPCODE 17: // MOVSx (extra=0)|STOSx (extra=1)|LODSx (extra=2)
				scratch2_uint = seg_override_en ? seg_override : REG_DS;

				for (scratch_uint = rep_override_en ? regs16[REG_CX] : 1; scratch_uint; scratch_uint--)
				{
					MEM_OP(extra < 2 ? SEGREG(REG_ES, REG_DI,) : REGS_BASE, =, extra & 1 ? REGS_BASE : SEGREG(scratch2_uint, REG_SI,)),
					extra & 1 || INDEX_INC(REG_SI),
					extra & 2 || INDEX_INC(REG_DI);
					#ifdef MPE5_NATIVE
					if (rep_override_en) --regs16[REG_CX];
					if (scratch_uint > 1)
					{
						if (!remaining || (MPE5Host.memory.shouldYield && MPE5Host.memory.shouldYield(MPE5Host.memory.context)))
						{
							seg_override_en = savedSegOverride; rep_override_en = savedRepOverride;
							MPE5RepeatPending = true;
							return MPE5Ready = !MPE5MemoryFailed;
						}
						--remaining;
					}
					#endif
				}

				if (rep_override_en)
					regs16[REG_CX] = 0
			OPCODE 18: // CMPSx (extra=0)|SCASx (extra=1)
				scratch2_uint = seg_override_en ? seg_override : REG_DS;

				if ((scratch_uint = rep_override_en ? regs16[REG_CX] : 1))
				{
					for (; scratch_uint; rep_override_en || scratch_uint--)
					{
						MEM_OP(extra ? REGS_BASE : SEGREG(scratch2_uint, REG_SI,), -, SEGREG(REG_ES, REG_DI,)),
						extra || INDEX_INC(REG_SI),
						INDEX_INC(REG_DI), rep_override_en && !(--regs16[REG_CX] && (!op_result == rep_mode)) && (scratch_uint = 0);
						#ifdef MPE5_NATIVE
						if (scratch_uint && rep_override_en)
						{
							if (!remaining || (MPE5Host.memory.shouldYield && MPE5Host.memory.shouldYield(MPE5Host.memory.context)))
							{
								seg_override_en = savedSegOverride; rep_override_en = savedRepOverride;
								MPE5RepeatPending = true;
								return MPE5Ready = !MPE5MemoryFailed;
							}
							--remaining;
						}
						#endif
					}

					set_flags_type = FLAGS_UPDATE_SZP | FLAGS_UPDATE_AO_ARITH; // Funge to set SZP/AO flags
					set_CF(op_result > op_dest);
				}
			OPCODE 19: // RET|RETF|IRET
				i_d = i_w;
				R_M_POP(reg_ip);
				if (extra) // IRET|RETF|RETF imm16
					R_M_POP(regs16[REG_CS]);
				if (extra & 2) // IRET
					set_flags(R_M_POP(scratch_uint));
				else if (!i_d) // RET|RETF imm16
					regs16[REG_SP] += i_data0
			OPCODE 20: // MOV r/m, immed
				R_M_OP(MPE5_MEM(op_from_addr), =, i_data2)
			OPCODE 21: // IN AL/AX, DX/imm8
				MPE5_PORT(0x20) = 0; // PIC EOI
				MPE5_PORT(0x42) = --MPE5_PORT(0x40); // PIT channel 0/2 read placeholder
				MPE5_PORT(0x3DA) ^= 9; // CGA refresh
				scratch_uint = extra ? regs16[REG_DX] : (unsigned char)i_data0;
				#ifdef MPE5_NATIVE
				// No PC gameport is attached. An absent ISA port reads high;
				// echoing OUT201 made Boulder see its abort/fire buttons held.
				if (scratch_uint == 0x201) MPE5_PORT(0x201) = 0xff;
				#endif
				scratch_uint == 0x60 && (MPE5_PORT(0x64) = 0); // Scancode read flag
				scratch_uint == 0x3D5 && (MPE5_PORT(0x3D4) >> 1 == 7) && (MPE5_PORT(0x3D5) = ((MPE5_MEM(0x49E)*80 + MPE5_MEM(0x49D) + CAST(short)MPE5_MEM(0x4AD)) & (MPE5_PORT(0x3D4) & 1 ? 0xFF : 0xFF00)) >> (MPE5_PORT(0x3D4) & 1 ? 0 : 8)); // CRT cursor position
				R_M_OP(regs8[REG_AL], =, MPE5_PORT(scratch_uint));
			OPCODE 22: // OUT DX/imm8, AL/AX
				scratch_uint = extra ? regs16[REG_DX] : (unsigned char)i_data0;
				R_M_OP(MPE5_PORT(scratch_uint), =, regs8[REG_AL]);
				#ifdef MPE5_NATIVE
				MPE5VendorSpeaker(scratch_uint, regs8[REG_AL]);
				#endif
				scratch_uint == 0x61 && (io_hi_lo = 0, spkr_en |= regs8[REG_AL] & 3); // Speaker control
				(scratch_uint == 0x40 || scratch_uint == 0x42) && (MPE5_PORT(0x43) & 6) && (MPE5_MEM(0x469 + scratch_uint - (io_hi_lo ^= 1)) = regs8[REG_AL]); // PIT rate programming
#ifndef NO_GRAPHICS
				scratch_uint == 0x43 && (io_hi_lo = 0, regs8[REG_AL] >> 6 == 2) && (SDL_PauseAudio((regs8[REG_AL] & 0xF7) != 0xB6), 0); // Speaker enable
#endif
				scratch_uint == 0x3D5 && (MPE5_PORT(0x3D4) >> 1 == 6) && (MPE5_MEM(0x4AD + !(MPE5_PORT(0x3D4) & 1)) = regs8[REG_AL]); // CRT video RAM start offset
				scratch_uint == 0x3D5 && (MPE5_PORT(0x3D4) >> 1 == 7) && (scratch2_uint = ((MPE5_MEM(0x49E)*80 + MPE5_MEM(0x49D) + CAST(short)MPE5_MEM(0x4AD)) & (MPE5_PORT(0x3D4) & 1 ? 0xFF00 : 0xFF)) + (regs8[REG_AL] << (MPE5_PORT(0x3D4) & 1 ? 0 : 8)) - CAST(short)MPE5_MEM(0x4AD), MPE5_MEM(0x49D) = scratch2_uint % 80, MPE5_MEM(0x49E) = scratch2_uint / 80); // CRT cursor position
				scratch_uint == 0x3B5 && MPE5_PORT(0x3B4) == 1 && (GRAPHICS_X = regs8[REG_AL] * 16); // Hercules resolution reprogramming. Defaults are set in the BIOS
				scratch_uint == 0x3B5 && MPE5_PORT(0x3B4) == 6 && (GRAPHICS_Y = regs8[REG_AL] * 4);
			OPCODE 23: // REPxx
				rep_override_en = 2;
				rep_mode = i_w;
				seg_override_en && seg_override_en++
			OPCODE 25: // PUSH reg
				R_M_PUSH(regs16[extra])
			OPCODE 26: // POP reg
				R_M_POP(regs16[extra])
			OPCODE 27: // xS: segment overrides
				seg_override_en = 2;
				seg_override = extra;
				rep_override_en && rep_override_en++
			OPCODE 28: // DAA/DAS
				i_w = 0;
				extra ? DAA_DAS(-=, >=, 0xFF, 0x99) : DAA_DAS(+=, <, 0xF0, 0x90) // extra = 0 for DAA, 1 for DAS
			OPCODE 29: // AAA/AAS
				op_result = AAA_AAS(extra - 1)
			OPCODE 30: // CBW
				regs8[REG_AH] = -SIGN_OF(regs8[REG_AL])
			OPCODE 31: // CWD
				regs16[REG_DX] = -SIGN_OF(regs16[REG_AX])
			OPCODE 32: // CALL FAR imm16:imm16
				R_M_PUSH(regs16[REG_CS]);
				R_M_PUSH(reg_ip + 5);
				regs16[REG_CS] = i_data2;
				reg_ip = i_data0
			OPCODE 33: // PUSHF
				make_flags();
				R_M_PUSH(scratch_uint)
			OPCODE 34: // POPF
				set_flags(R_M_POP(scratch_uint))
			OPCODE 35: // SAHF
				make_flags();
				set_flags((scratch_uint & 0xFF00) + regs8[REG_AH])
			OPCODE 36: // LAHF
				make_flags(),
				regs8[REG_AH] = scratch_uint
			OPCODE 37: // LES|LDS reg, r/m
				i_w = i_d = 1;
				DECODE_RM_REG;
				OP(=);
				MEM_OP(REGS_BASE + extra, =, rm_addr + 2)
			OPCODE 38: // INT 3
				++reg_ip;
				pc_interrupt(3)
			OPCODE 39: // INT imm8
				reg_ip += 2;
				pc_interrupt(i_data0)
			OPCODE 40: // INTO
				++reg_ip;
				regs8[FLAG_OF] && pc_interrupt(4)
			OPCODE 41: // AAM
				if (i_data0 &= 0xFF)
					regs8[REG_AH] = regs8[REG_AL] / i_data0,
					op_result = regs8[REG_AL] %= i_data0;
				else // Divide by zero
					pc_interrupt(0)
			OPCODE 42: // AAD
				i_w = 0;
				regs16[REG_AX] = op_result = 0xFF & regs8[REG_AL] + i_data0 * regs8[REG_AH]
			OPCODE 43: // SALC
				regs8[REG_AL] = -regs8[FLAG_CF]
			OPCODE 44: // XLAT
				regs8[REG_AL] = MPE5_MEM(SEGREG(seg_override_en ? seg_override : REG_DS, REG_BX, regs8[REG_AL] +))
			OPCODE 45: // CMC
				regs8[FLAG_CF] ^= 1
			OPCODE 46: // CLC|STC|CLI|STI|CLD|STD
				regs8[extra / 2] = extra & 1
			OPCODE 47: // TEST AL/AX, immed
				R_M_OP(regs8[REG_AL], &, i_data0)
			OPCODE 48: // Emulator-specific 0F xx opcodes
				switch ((char)i_data0)
				{
			OPCODE_CHAIN 0: // PUTCHAR_AL
						#ifdef MPE5_NATIVE
						MPE5VendorPutChar(regs8[REG_AL]);
						break;
						#else
						write(1, regs8, 1)
						#endif
					OPCODE 1: // GET_RTC
						#ifdef MPE5_NATIVE
						mpe5_detail::writeRtc(SEGREG(REG_ES, REG_BX,));
						break;
						#else
						time(&clock_buf);
						ftime(&ms_clock);
						memcpy(mem + SEGREG(REG_ES, REG_BX,), localtime(&clock_buf), sizeof(struct tm));
						CAST(short)MPE5_MEM(SEGREG(REG_ES, REG_BX, 36+)) = ms_clock.millitm;
						#endif
					OPCODE 2: // DISK_READ
					OPCODE_CHAIN 3: // DISK_WRITE
						#ifdef MPE5_NATIVE
						if (!MPE5VendorDisk((char)i_data0 == 3, remaining))
						{
							seg_override_en = savedSegOverride; rep_override_en = savedRepOverride;
							return MPE5Ready = !MPE5MemoryFailed;
						}
						break;
						#else
						regs8[REG_AL] = ~lseek(disk[regs8[REG_DL]], CAST(unsigned)regs16[REG_BP] << 9, 0)
							? ((char)i_data0 == 3 ? (int(*)())write : (int(*)())read)(disk[regs8[REG_DL]], mem + SEGREG(REG_ES, REG_BX,), regs16[REG_AX])
							: 0;
						#endif
				}
		}

		// Increment instruction pointer by computed instruction length. Tables in the BIOS binary
		// help us here.
		reg_ip += (i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6))*i_mod_size + bios_table_lookup[TABLE_BASE_INST_SIZE][raw_opcode_id] + bios_table_lookup[TABLE_I_W_SIZE][raw_opcode_id]*(i_w + 1);

		// If instruction needs to update SF, ZF and PF, set them as appropriate
		if (set_flags_type & FLAGS_UPDATE_SZP)
		{
			regs8[FLAG_SF] = SIGN_OF(op_result);
			regs8[FLAG_ZF] = !op_result;
			regs8[FLAG_PF] = bios_table_lookup[TABLE_PARITY_FLAG][(unsigned char)op_result];

			// If instruction is an arithmetic or logic operation, also set AF/OF/CF as appropriate.
			if (set_flags_type & FLAGS_UPDATE_AO_ARITH)
				set_AF_OF_arith();
			if (set_flags_type & FLAGS_UPDATE_OC_LOGIC)
				set_CF(0), set_OF(0);
		}

		// Poll timer/keyboard every KEYBOARD_TIMER_UPDATE_DELAY instructions
		if (!(++inst_counter % KEYBOARD_TIMER_UPDATE_DELAY))
			int8_asap = 1;

#ifndef NO_GRAPHICS
		// Update the video graphics display every GRAPHICS_UPDATE_DELAY instructions
		if (!(inst_counter % GRAPHICS_UPDATE_DELAY))
		{
			// Video card in graphics mode?
			if (MPE5_PORT(0x3B8) & 2)
			{
				// If we don't already have an SDL window open, set it up and compute color and video memory translation tables
				if (!sdl_screen)
				{
					for (int i = 0; i < 16; i++)
						pixel_colors[i] = MPE5_MEM(0x4AC) ? // CGA?
							cga_colors[(i & 12) >> 2] + (cga_colors[i & 3] << 16) // CGA -> RGB332
							: 0xFF*(((i & 1) << 24) + ((i & 2) << 15) + ((i & 4) << 6) + ((i & 8) >> 3)); // Hercules -> RGB332

					for (int i = 0; i < GRAPHICS_X * GRAPHICS_Y / 4; i++)
						vid_addr_lookup[i] = i / GRAPHICS_X * (GRAPHICS_X / 8) + (i / 2) % (GRAPHICS_X / 8) + 0x2000*(MPE5_MEM(0x4AC) ? (2 * i / GRAPHICS_X) % 2 : (4 * i / GRAPHICS_X) % 4);

					SDL_Init(SDL_INIT_VIDEO);
					sdl_screen = SDL_SetVideoMode(GRAPHICS_X, GRAPHICS_Y, 8, 0);
					SDL_EnableUNICODE(1);
					SDL_EnableKeyRepeat(500, 30);
				}

				// Refresh SDL display from emulated graphics card video RAM
				vid_mem_base = mem + 0xB0000 + 0x8000*(MPE5_MEM(0x4AC) ? 1 : MPE5_PORT(0x3B8) >> 7); // B800:0 for CGA/Hercules bank 2, B000:0 for Hercules bank 1
				for (int i = 0; i < GRAPHICS_X * GRAPHICS_Y / 4; i++)
					((unsigned *)sdl_screen->pixels)[i] = pixel_colors[15 & (vid_mem_base[vid_addr_lookup[i]] >> 4*!(i & 1))];

				SDL_Flip(sdl_screen);
			}
			else if (sdl_screen) // Application has gone back to text mode, so close the SDL window
			{
				SDL_QuitSubSystem(SDL_INIT_VIDEO);
				sdl_screen = 0;
			}
			SDL_PumpEvents();
		}
#endif

		// Application has set trap flag, so fire INT 1
		if (trap_flag)
			pc_interrupt(1);

		trap_flag = regs8[FLAG_TF];

		// Never inject an interrupt between a prefix and its instruction.
		if ((int8_asap
			#ifdef MPE5_NATIVE
			|| !(inst_counter & 63u)
			#endif
			) && !seg_override_en && !rep_override_en && regs8[FLAG_IF] && !regs8[FLAG_TF])
		{
			#ifdef MPE5_NATIVE
			const bool nativeKey = MPE5Host.keyboard && MPE5Host.keyboard->nativePending();
			const bool nativeReady = nativeKey && !MPE5KeyboardAwaitResume && !MPE5KeyboardCooldown;
			// Interrupt contexts nest: the last one pushed executes first.
			// Run the timer (including release of an older key) before the
			// fresh key-down. Otherwise a due timer can release the new key
			// before the game executes even one instruction to observe it.
			if (nativeReady || (int8_asap && !nativeKey)) MPE5VendorKeyboardPoll();
			if (int8_asap) { pc_interrupt(0xA); int8_asap = 0; }
			#else
			if (int8_asap) { pc_interrupt(0xA), int8_asap = 0, SDL_KEYBOARD_DRIVER; }
			#endif
		}
		#ifdef MPE5_NATIVE
		if (MPE5Host.memory.shouldYield && MPE5Host.memory.shouldYield(MPE5Host.memory.context)) break;
		#endif
	}

#ifndef NO_GRAPHICS
	SDL_Quit();
#endif
#ifdef MPE5_NATIVE
	if (MPE5MemoryFailed) MPE5Ready = false;
	return MPE5Ready;
#else
	return 0;
#endif
}

#ifdef MPE5_NATIVE
static MPE5_FUNCTION uint8_t MPE5StatefulKeyIndex(uint8_t scan)
{
	switch (scan & 0x7fu)
	{
		case 0x36: return 0; // Shift / joystick fire
		case 0x48: return 1; // Up
		case 0x50: return 2; // Down
		case 0x4b: return 3; // Left
		case 0x4d: return 4; // Right
		default: return 0xffu;
	}
}

static MPE5_FUNCTION bool MPE5VendorKeyboardPoll()
{
	mpe5::Key key;
	if (!MPE5Host.keyboard || !MPE5Host.keyboard->peek(key)) return false;
	const uint8_t stateful = MPE5StatefulKeyIndex(key.scan);
	const uint8_t statefulMask = stateful < 5u ? uint8_t(1u << stateful) : 0u;
	if ((key.flags & 0x80u) && (key.scan & 0x80u) &&
		(MPE5StatefulDown & statefulMask) &&
		uint32_t(inst_counter - MPE5StatefulMakeInstruction[stateful]) <
			MPE5StatefulMinimumInstructions)
		return false;
	if (!MPE5Host.keyboard->pop(key)) return false;
	if (key.flags & 0x80u)
	{
		if (statefulMask)
		{
			if (key.scan & 0x80u) MPE5StatefulDown &= uint8_t(~statefulMask);
			else if (!(MPE5StatefulDown & statefulMask))
			{
				MPE5StatefulMakeInstruction[stateful] = inst_counter;
				MPE5StatefulDown |= statefulMask;
			}
		}
		// Shift used to type printable text must release at normal typing
		// speed. Standalone Shift and Shift+cursor remain stateful controls.
		if (key.ascii) MPE5StatefulDown &= uint8_t(~1u);
		// The C64 already supplies PC set-1 scans. Use IRQ1 directly, as a
		// physical keyboard would, instead of reinterpreting arrows as ASCII.
		// Clear the pinned BIOS's old ANSI tap/release/escape state; explicit
		// snapshots now own release timing. Cursor-visible byte4A1 is retained.
		MPE5_MEM(0x49f) = MPE5_MEM(0x4a0) = 0;
		MPE5_MEM(0x4a2) = MPE5_MEM(0x4a3) = MPE5_MEM(0x4a4) = MPE5_MEM(0x4a5) = 0;
		MPE5_MEM(0x417) = (MPE5_MEM(0x417) & 0xf0u) |
			(key.flags & 1u) | ((key.flags & 6u) << 1u);
		MPE5_MEM(0x418) = (MPE5_MEM(0x418) & 0xfcu) | ((key.flags >> 1u) & 3u);
		MPE5_MEM(0x4a6) = key.ascii;
		MPE5_MEM(0x4a7) = 0;
		MPE5_PORT(0x60) = key.scan;
		MPE5_PORT(0x64) = 1;
		MPE5KeyboardResumeCS = regs16[REG_CS]; MPE5KeyboardResumeIP = reg_ip;
		MPE5KeyboardResumeSS = regs16[REG_SS]; MPE5KeyboardResumeSP = regs16[REG_SP];
		MPE5KeyboardAwaitResume = true;
		pc_interrupt(9);
		return true;
	}
	// 8086tiny's BIOS consumes this byte from the BDA after INT 0Ah.  COMMAND
	// needs printable ASCII first; scan codes cover later navigation keys.
	MPE5_MEM(0x4A6) = key.ascii ? key.ascii : key.scan;
	pc_interrupt(7);
	return true;
}

static MPE5_FUNCTION void MPE5ConsoleRefresh()
{
	for (uint16_t row = 0; row < mpe5::CgaTextRows; ++row)
		memcpy(MPE5ConsoleViewport + 2u * mpe5::CgaTextColumns * row,
			MPE5ConsoleShadow + 2u * mpe5::NativeTextColumns * row,
			2u * mpe5::CgaTextColumns);
}

static MPE5_FUNCTION void MPE5ConsoleErase(uint16_t first, uint16_t end)
{
	uint8_t *shadow = MPE5ConsoleShadow;
	for (uint16_t cell = first; cell < end; ++cell)
	{
		shadow[2u * cell] = ' ';
		shadow[2u * cell + 1u] = 7;
	}
}

static MPE5_FUNCTION void MPE5ConsoleScroll(uint8_t top, uint8_t bottom,
	uint16_t count, bool down)
{
	if (top > bottom || bottom >= mpe5::CgaTextRows) return;
	const uint16_t rows = bottom - top + 1u;
	if (count > rows) count = rows;
	const uint16_t stride = 2u * mpe5::NativeTextColumns;
	uint8_t *shadow = MPE5ConsoleShadow;
	memmove(shadow + stride * (top + (down ? count : 0)),
		shadow + stride * (top + (down ? 0 : count)), stride * (rows - count));
	const uint16_t first = (down ? top : bottom + 1u - count) * mpe5::NativeTextColumns;
	MPE5ConsoleErase(first, first + count * mpe5::NativeTextColumns);
}

// The BIOS emits a terminal byte stream through its 0F00 hypercall. Keep its
// display entirely outside the guest address map: the BIOS already uses
// B800/C000/C800 for guest VRAM, console readback, and video-change detection.
// Writing our viewport there feeds host output back into the guest renderer.
static MPE5_FUNCTION void MPE5VendorPutChar(uint8_t character)
{
	static const uint16_t columns = mpe5::NativeTextColumns;
	static const uint16_t visible_columns = mpe5::CgaTextColumns, rows = mpe5::CgaTextRows;
	uint8_t *screen = MPE5ConsoleViewport;
	uint8_t *shadow = MPE5ConsoleShadow;
	if (MPE5TextEscapeState == 1)
	{
		MPE5TextEscapeState = character == '[' ? 2 : 0;
		MPE5TextParameterCount = 0;
		memset(MPE5TextParameters, 0, sizeof(MPE5TextParameters));
		return;
	}
	if (MPE5TextEscapeState == 2)
	{
		// DEC private-mode prefix, used by the BIOS to hide/show its cursor.
		if (character == '?' && !MPE5TextParameterCount && !MPE5TextParameters[0])
			return;
		if (character >= '0' && character <= '9')
		{
			uint8_t index = MPE5TextParameterCount > 1 ? 1 : MPE5TextParameterCount;
			if (MPE5TextParameters[index] < 1000u)
				MPE5TextParameters[index] = 10u * MPE5TextParameters[index] + (character - '0');
			return;
		}
		if (character == ';')
		{
			if (MPE5TextParameterCount < 1) ++MPE5TextParameterCount;
			return;
		}
		uint16_t row = MPE5TextCursor / columns, column = MPE5TextCursor % columns;
		const uint16_t count = MPE5TextParameters[0] ? MPE5TextParameters[0] : 1u;
		bool changed = false;
		if (character == 'H' || character == 'f')
		{
			row = MPE5TextParameters[0] ? MPE5TextParameters[0] - 1u : 0;
			column = MPE5TextParameters[1] ? MPE5TextParameters[1] - 1u : 0;
		}
		else if (character == 'J')
		{
			const uint16_t mode = MPE5TextParameters[0];
			if (mode <= 2)
				MPE5ConsoleErase(mode == 0 ? MPE5TextCursor : 0,
					mode == 1 ? MPE5TextCursor + 1u : rows * columns);
			changed = true;
		}
		else if (character == 'K')
		{
			const uint16_t mode = MPE5TextParameters[0];
			if (mode <= 2)
				MPE5ConsoleErase(mode == 0 ? MPE5TextCursor : row * columns,
					mode == 1 ? MPE5TextCursor + 1u : (row + 1u) * columns);
			changed = true;
		}
		else if (character == 'r')
		{
			const uint16_t top = MPE5TextParameters[0] ? MPE5TextParameters[0] - 1u : 0;
			const uint16_t bottom = MPE5TextParameters[1] ? MPE5TextParameters[1] - 1u : rows - 1u;
			if (top < bottom && bottom < rows)
			{ MPE5TextScrollTop = top; MPE5TextScrollBottom = bottom; row = column = 0; }
		}
		else if (character == 'S' || character == 'T')
		{ MPE5ConsoleScroll(MPE5TextScrollTop, MPE5TextScrollBottom, count, character == 'T'); changed = true; }
		else if ((character == 'M' || character == 'L') && row >= MPE5TextScrollTop && row <= MPE5TextScrollBottom)
		{ MPE5ConsoleScroll(row, MPE5TextScrollBottom, count, character == 'L'); changed = true; }
		else if (character == 'A') row = row > count ? row - count : 0;
		else if (character == 'B') row += count;
		else if (character == 'C') column += count;
		else if (character == 'D') column = column > count ? column - count : 0;
		MPE5TextCursor = (row < rows ? row : rows - 1u) * columns +
			(column < columns ? column : columns - 1u);
		if (character != 'm' && character != 'h' && character != 'l') MPE5TextWrapPending = false;
		if (changed) MPE5ConsoleRefresh();
		MPE5TextEscapeState = 0;
		return;
	}
	if (character == 27) { MPE5TextEscapeState = 1; return; }
	uint16_t column = MPE5TextCursor % columns;
	uint16_t row = MPE5TextCursor / columns;
	if (character == '\r') { column = 0; MPE5TextWrapPending = false; }
	else if (character == '\n')
	{
		if (row == MPE5TextScrollBottom)
		{ MPE5ConsoleScroll(MPE5TextScrollTop, MPE5TextScrollBottom, 1, false); MPE5ConsoleRefresh(); }
		else if (row + 1u < rows) ++row;
		MPE5TextWrapPending = false;
	}
	else if (character == '\b') { if (column) --column; MPE5TextWrapPending = false; }
	else if (character >= ' ')
	{
		// VT terminals defer wrapping until the next printable character.
		// Immediate wrap would scroll after painting the bottom-right cell.
		if (MPE5TextWrapPending)
		{
			column = 0;
			if (row == MPE5TextScrollBottom)
			{ MPE5ConsoleScroll(MPE5TextScrollTop, MPE5TextScrollBottom, 1, false); MPE5ConsoleRefresh(); }
			else if (row + 1u < rows) ++row;
			MPE5TextWrapPending = false;
		}
		shadow[2u * (row * columns + column)] = character;
		shadow[2u * (row * columns + column) + 1u] = 7;
		if (column < visible_columns)
		{
			screen[2u * (row * visible_columns + column)] = character;
			screen[2u * (row * visible_columns + column) + 1u] = 7;
		}
		if (column + 1u == columns) MPE5TextWrapPending = true;
		else ++column;
	}
	MPE5TextCursor = row * columns + column;
}

static MPE5_FUNCTION bool MPE5VendorDisk(bool write, uint32_t &remaining)
{
	uint8_t sector[512];
	// The 8086tiny BIOS maps its DL=80 fixed drive to file handle zero before
	// issuing 0F 02.  Handle zero is therefore the only accepted MPE5 C: disk;
	// accepting the floppy handle would hide a wrong boot configuration.
	if (!MPE5DiskPending)
	{
		const uint32_t length = regs16[REG_AX], lba = regs16[REG_BP];
		const uint32_t target = SEGREG(REG_ES, REG_BX,);
		if (write || regs8[REG_DL] != 0 || !length || (length & 511u) ||
			target > RAM_SIZE || length > RAM_SIZE - target ||
			lba >= MPE5Host.drive.sectors || length / 512u > MPE5Host.drive.sectors - lba)
		{
			regs8[REG_AL] = 1;
			return true;
		}
		MPE5DiskTarget = target; MPE5DiskLba = lba;
		MPE5DiskLength = length; MPE5DiskOffset = 0;
		MPE5DiskPending = true;
	}
	for (uint8_t sectorsThisCall = 0; MPE5DiskOffset < MPE5DiskLength;)
	{
		if (!MPE5Host.drive.readSector(MPE5Host.drive.context, MPE5DiskLba + MPE5DiskOffset / 512u,
			sector) || !mpe5_detail::writeBytes(MPE5DiskTarget + MPE5DiskOffset, sector, sizeof(sector)))
		{ MPE5DiskPending = false; regs8[REG_AL] = 1; return true; }
		MPE5DiskOffset += 512u; ++sectorsThisCall;
		if (MPE5DiskOffset < MPE5DiskLength)
		{
			// A disk hypercall is one guest instruction but may copy127
			// sectors. Retain its arguments and resume between whole sectors.
			if (!remaining || sectorsThisCall == 4 ||
				(MPE5Host.memory.shouldYield && MPE5Host.memory.shouldYield(MPE5Host.memory.context)))
				return false;
			--remaining;
		}
	}
	MPE5DiskPending = false;
	regs8[REG_AL] = 0;
	return true;
}

static MPE5_FUNCTION void MPE5VendorSpeaker(uint16_t port, uint8_t value)
{
	if (MPE5Host.speaker) MPE5Host.speaker->write(port, value);
}

#endif

#undef MPE5_FUNCTION
