// SPDX-License-Identifier: GPL-2.0-or-later

#include <direct.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ff.h"

#define MHS_DOOM_ZONE_BYTES (8u * 1024u * 1024u)

// MCUME's Teensy port places this zone in 8 MiB of external PSRAM. Keeping the
// exact size here makes this a useful porting proof instead of a desktop-sized
// approximation.
unsigned char MemPool[MHS_DOOM_ZONE_BYTES] __attribute__((aligned(16)));
long systime;
int joystick;

static uint32_t host_milliseconds;

void host_advance_time(uint32_t milliseconds)
{
    host_milliseconds += milliseconds;
    systime = (long) host_milliseconds;
}

void emu_GetTimeOfDay(int *microseconds, int *seconds)
{
    // The hardware timer advances independently while Doom waits for a tic.
    // Advancing one millisecond per query prevents a host-side busy wait while
    // preserving deterministic timing.
    ++host_milliseconds;
    systime = (long) host_milliseconds;
    *seconds = (int) (host_milliseconds / 1000u);
    *microseconds = (int) ((host_milliseconds % 1000u) * 1000u);
}

void *emu_Malloc(int size)
{
    return size > 0 ? calloc(1u, (size_t) size) : NULL;
}

void emu_printf(const char *text)
{
    if (text != NULL)
    {
        fprintf(stderr, "MCUME: %s\n", text);
        fflush(stderr);
    }
}

void delay(unsigned long milliseconds)
{
    // In this port I_Error pauses for ten seconds and then returns. A host
    // proof must fail closed instead of continuing after a fatal engine error.
    if (milliseconds >= 10000ul)
    {
        fflush(NULL);
        _Exit(70);
    }

    host_advance_time((uint32_t) milliseconds);
}

void emu_DrawLine16(unsigned short *pixels, int width, int height, int line)
{
    (void) pixels;
    (void) width;
    (void) height;
    (void) line;
}

static FILE *mhs_file(FIL *file)
{
    return file != NULL ? (FILE *) (uintptr_t) (*file) : NULL;
}

FRESULT f_open(FIL *file, const char *path, unsigned char mode)
{
    const char *open_mode;
    FILE *stream;

    if (file == NULL || path == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    open_mode = (mode & FA_WRITE) != 0
        ? (((mode & FA_CREATE_ALWAYS) != 0) ? "w+b" : "r+b")
        : "rb";
    stream = fopen(path, open_mode);
    if (stream == NULL && (mode & FA_OPEN_ALWAYS) != 0)
    {
        stream = fopen(path, "w+b");
    }
    if (stream == NULL)
    {
        return FR_NO_FILE;
    }

    *file = (FIL) (uintptr_t) stream;
    return FR_OK;
}

FRESULT f_close(FIL *file)
{
    FILE *stream = mhs_file(file);
    int result;

    if (stream == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    result = fclose(stream);
    *file = NULL;
    return result == 0 ? FR_OK : FR_DISK_ERR;
}

FRESULT f_read(FIL *file, void *buffer, unsigned int bytes,
               unsigned int *bytes_read)
{
    FILE *stream = mhs_file(file);
    size_t count;

    if (stream == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    count = fread(buffer, 1u, bytes, stream);
    if (bytes_read != NULL)
    {
        *bytes_read = (unsigned int) count;
    }
    return ferror(stream) != 0 ? FR_DISK_ERR : FR_OK;
}

FRESULT f_readn(FIL *file, void *buffer, unsigned int bytes,
                unsigned int *bytes_read)
{
    return f_read(file, buffer, bytes, bytes_read);
}

FRESULT f_write(FIL *file, const void *buffer, unsigned int bytes,
                unsigned int *bytes_written)
{
    FILE *stream = mhs_file(file);
    size_t count;

    if (stream == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    count = fwrite(buffer, 1u, bytes, stream);
    if (bytes_written != NULL)
    {
        *bytes_written = (unsigned int) count;
    }
    return count == bytes ? FR_OK : FR_DISK_ERR;
}

FRESULT f_writen(FIL *file, const void *buffer, unsigned int bytes,
                 unsigned int *bytes_written)
{
    return f_write(file, buffer, bytes, bytes_written);
}

FRESULT f_lseek(FIL *file, unsigned long offset)
{
    FILE *stream = mhs_file(file);
    return stream != NULL && fseek(stream, (long) offset, SEEK_SET) == 0
        ? FR_OK
        : FR_DISK_ERR;
}

unsigned long f_tell(FIL *file)
{
    FILE *stream = mhs_file(file);
    long offset = stream != NULL ? ftell(stream) : -1;
    return offset >= 0 ? (unsigned long) offset : 0ul;
}

unsigned long f_size(FIL *file)
{
    FILE *stream = mhs_file(file);
    long current;
    long size;

    if (stream == NULL)
    {
        return 0ul;
    }

    current = ftell(stream);
    if (current < 0 || fseek(stream, 0, SEEK_END) != 0)
    {
        return 0ul;
    }
    size = ftell(stream);
    (void) fseek(stream, current, SEEK_SET);
    return size >= 0 ? (unsigned long) size : 0ul;
}

FRESULT f_unlink(const char *path)
{
    return remove(path) == 0 ? FR_OK : FR_DISK_ERR;
}

FRESULT f_rename(const char *old_path, const char *new_path)
{
    return rename(old_path, new_path) == 0 ? FR_OK : FR_DISK_ERR;
}

FRESULT f_stat(const char *path, FILINFO *info)
{
    struct _stat state;

    if (_stat(path, &state) != 0)
    {
        return FR_NO_FILE;
    }
    if (info != NULL)
    {
        info->fsize = (signed long) state.st_size;
    }
    return FR_OK;
}

FRESULT f_mkdir(const char *path)
{
    return _mkdir(path) == 0 || errno == EEXIST ? FR_OK : FR_DISK_ERR;
}
