#ifndef MPE5_REDIRECTOR_IO_PROBE_H
#define MPE5_REDIRECTOR_IO_PROBE_H

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Original 8086 COM test. Exercise DOS itself, including its SFT reference
// counts and process-exit cleanup; no redirector-private guest layout here.
// Success prints IOCHECK PASS and leaves D:\IOCHECK.DAT containing "0123".
static std::vector<uint8_t> mpe5RedirectorIoProbe() {
  struct Fixup { size_t position; std::string name; bool relative; unsigned add; };
  std::vector<uint8_t> bytes;
  std::map<std::string,size_t> labels;
  std::vector<Fixup> fixups;
  const auto byte=[&](uint8_t value){bytes.push_back(value);};
  const auto word=[&](uint16_t value){byte(uint8_t(value));byte(uint8_t(value>>8));};
  const auto emit=[&](std::initializer_list<uint8_t> code){bytes.insert(bytes.end(),code);};
  const auto label=[&](const char *name){labels.emplace(name,bytes.size());};
  const auto address=[&](const char *name,unsigned add=0){fixups.push_back({bytes.size(),name,false,add});word(0);};
  const auto reg=[&](uint8_t opcode,uint16_t value){byte(opcode);word(value);};
  // On success skip the near failure jump. All branches stay 8086 compatible.
  const auto expect=[&](uint8_t successCondition){
    emit({successCondition,3,0xe9});fixups.push_back({bytes.size(),"fail",true,0});word(0);
  };
  const auto dos=[&](){emit({0xcd,0x21});expect(0x73);}; // JNC
  const auto dxAddress=[&](const char *name){byte(0xba);address(name);};

  // Create, write ten bytes, seek backwards from EOF, and verify the read.
  reg(0xb8,0x3c00);emit({0x31,0xc9});dxAddress("filename");dos();
  emit({0x89,0xc3}); // BX = open DOS handle
  reg(0xb8,0x4000);reg(0xb9,10);dxAddress("data");dos();
  byte(0x3d);word(10);expect(0x74); // AX == requested write count
  reg(0xb8,0x4202);reg(0xb9,0xffff);reg(0xba,0xfffd);dos();
  byte(0x3d);word(7);expect(0x74);
  emit({0x83,0xfa,0});expect(0x74); // DX:AX == 7
  reg(0xb8,0x3f00);reg(0xb9,3);dxAddress("buffer");dos();
  byte(0x3d);word(3);expect(0x74);
  emit({0x81,0x3e});address("buffer");word(0x3837);expect(0x74);
  emit({0x80,0x3e});address("buffer",2);byte('9');expect(0x74);

  // A zero-byte DOS write truncates at the current file position.
  reg(0xb8,0x4200);emit({0x31,0xc9});reg(0xba,4);dos();
  reg(0xb8,0x4000);emit({0x31,0xc9});dxAddress("buffer");dos();
  emit({0x09,0xc0});expect(0x74); // AX == zero bytes written
  reg(0xb8,0x6800);dos(); // Commit

  // DOS must close both references on process exit, without losing the save.
  reg(0xb8,0x4500);dos(); // Duplicate BX into another DOS handle
  dxAddress("passed");reg(0xb8,0x0900);emit({0xcd,0x21});
  reg(0xb8,0x4c00);emit({0xcd,0x21});
  label("fail");dxAddress("failed");reg(0xb8,0x0900);emit({0xcd,0x21});
  reg(0xb8,0x4c01);emit({0xcd,0x21});
  const auto text=[&](const char *name,const char *value){
    label(name);for(const char *p=value;*p;++p)byte(uint8_t(*p));byte(0);
  };
  text("filename","D:\\IOCHECK.DAT");text("data","0123456789");
  text("passed","IOCHECK PASS\r\n$");text("failed","IOCHECK FAIL\r\n$");
  label("buffer");emit({0,0,0});
  for(const auto &fix:fixups) {
    const auto found=labels.find(fix.name);
    if(found==labels.end())throw std::runtime_error("IO probe label missing");
    const int target=int(found->second+fix.add);
    const uint16_t value=uint16_t(fix.relative?target-int(fix.position+2):target+0x100);
    bytes[fix.position]=uint8_t(value);bytes[fix.position+1]=uint8_t(value>>8);
  }
  return bytes;
}

#endif
