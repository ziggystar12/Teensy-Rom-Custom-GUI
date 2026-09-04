#include "nes_machine.h"
#include "nes_sid.h"
#include "nes_video.h"
// Object-file size probes only. Never link these arrays into firmware.
extern "C" {
unsigned char nes_size_machine[sizeof(nes::Machine)];
unsigned char nes_size_renderer[sizeof(nes::SquishRenderer)];
unsigned char nes_size_presented[sizeof(nes::VicFrame)];
unsigned char nes_size_sid[sizeof(nes::SidAdapter)+sizeof(nes::SidPacket)];
}
