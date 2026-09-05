// MPE port changes 2026-09-04; derived from MCUME gnuboy, see ../README.md.
#ifndef __DEFS_H__
#define __DEFS_H__


#define IS_LITTLE_ENDIAN
//#define LSB_FIRST     1
#define SOUND_PRESENT 1


#define XRES 160
#define YRES 144

#define die(...) core_fault(__VA_ARGS__)


#ifdef IS_LITTLE_ENDIAN
#define LO 0
#define HI 1
#else
#define LO 1
#define HI 0
#endif


typedef unsigned char uint8;
typedef unsigned short int uint16;
typedef uint32_t uint32;
typedef signed char int8;
typedef signed short int int16;
typedef int32_t int32;

//#define NULL 0

typedef unsigned char un8;
typedef unsigned short un16;
typedef unsigned int un32;
typedef signed char n8;
typedef signed short n16;
typedef signed int n32;

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned short addr;

#endif
