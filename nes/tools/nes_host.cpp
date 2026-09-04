#include "nes_machine.h"
#include "nes_sid.h"
#include "nes_video.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
static std::string quote(const std::string& s) {
    std::string result="\"";
    for(unsigned char ch:s) {
        if(ch=='"' || ch=='\\') { result+='\\'; result+=char(ch); }
        else if(ch<32) { const char* hex="0123456789abcdef"; result+="\\u00"; result+=hex[ch>>4]; result+=hex[ch&15]; }
        else result+=char(ch);
    }
    return result+'"';
}
struct FileInfo { nes::RomInfo rom{}; nes::RomError error{}; uint64_t bytes=0; };
static FileInfo inspect_file(const fs::path& file) {
    std::ifstream in(file,std::ios::binary);
    if(!in) throw std::runtime_error("cannot open ROM");
    FileInfo out;
    out.bytes=fs::file_size(file);
    uint8_t header[16]{};
    in.read(reinterpret_cast<char*>(header),16);
    out.error=nes::inspect(header,size_t(in.gcount()),out.bytes,out.rom);
    if(out.error==nes::RomError::None) out.error=nes::supported(out.rom);
    return out;
}
static void print_file(const fs::path& p) {
    std::cout<<"{\"name\":"<<quote(p.filename().u8string());
    try {
        const FileInfo f=inspect_file(p);
        std::cout<<",\"bytes\":"<<f.bytes<<",\"mapper\":"<<f.rom.mapper<<",\"prgBytes\":"<<f.rom.prg_bytes
                 <<",\"chrBytes\":"<<f.rom.chr_bytes<<",\"nes2\":"<<(f.rom.nes2?"true":"false")
                 <<",\"supported\":"<<(f.error==nes::RomError::None?"true":"false")<<",\"reason\":"<<quote(nes::describe(f.error));
    } catch(const std::exception& e) { std::cout<<",\"supported\":false,\"reason\":"<<quote(e.what()); }
    std::cout<<'}';
}
static uint64_t number(const std::string& s) {
    size_t count=0;
    const auto value=std::stoull(s,&count);
    if(count!=s.size() || s.empty() || s[0]=='-') throw std::runtime_error("invalid nonnegative integer");
    return value;
}
struct InputEvent { uint64_t frame; uint8_t state; bool sharp=false; };
static uint8_t buttons(const std::string& s) {
    uint8_t result=0;
    std::istringstream in(s); std::string item;
    while(std::getline(in,item,'+')) {
        if(item=="A") result|=nes::A;
        else if(item=="B" || item=="SPACE") result|=nes::B;
        else if(item=="START" || item=="ENTER") result|=nes::Start;
        else if(item=="SELECT" || item=="SHIFT") result|=nes::Select;
        else if(item=="UP") result|=nes::Up;
        else if(item=="DOWN") result|=nes::Down;
        else if(item=="LEFT") result|=nes::Left;
        else if(item=="RIGHT") result|=nes::Right;
        else if(item!="NONE") throw std::runtime_error("unknown input button: "+item);
    }
    return result;
}
static std::vector<InputEvent> events(const std::string& s) {
    std::vector<InputEvent> out;
    std::istringstream in(s); std::string part;
    while(std::getline(in,part,',')) {
        const auto colon=part.find(':');
        if(colon==std::string::npos) throw std::runtime_error("input must be frame:BUTTON+BUTTON,...");
        const auto action=part.substr(colon+1);
        InputEvent e{number(part.substr(0,colon)),action=="SHARP"?uint8_t(0):buttons(action),action=="SHARP"};
        if(!out.empty() && e.frame<=out.back().frame) throw std::runtime_error("input frames must strictly increase");
        out.push_back(e);
    }
    return out;
}
static void save(const fs::path& p,const void* data,size_t bytes) {
    std::ofstream out(p,std::ios::binary);
    if(!out || !out.write(static_cast<const char*>(data),std::streamsize(bytes))) throw std::runtime_error("cannot write diagnostic output");
}
struct FrameCapture {
    std::array<uint8_t,256*240> current{},completed{};
    nes::SquishRenderer streaming;
    nes::SidAdapter sid;
    nes::SidPacket audio{};
    const nes::Apu* apu=nullptr;
    bool completed_sharp=true;
    static void pixel(void* ctx,uint16_t x,uint16_t y,uint8_t c) {
        auto& f=*static_cast<FrameCapture*>(ctx);
        f.current[y*256+x]=c; f.streaming.pixel(x,y,c);
    }
    static void frame(void* ctx,uint64_t number) {
        auto& f=*static_cast<FrameCapture*>(ctx); f.completed=f.current;
        f.completed_sharp=f.streaming.frame.hires;
        if(f.apu) f.sid.render(*f.apu,f.audio);
        nes::SquishRenderer::finish(&f.streaming,number);
    }
};
int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::runtime_error("inspect FILE... | run FILE FRAMES EVENTS OUTPUT-DIRECTORY SHARP");
        const std::string command=argv[1];
        if(command=="inspect") {
            std::cout<<'[';
            for(int i=2;i<argc;++i) { if(i>2) std::cout<<','; print_file(fs::u8path(argv[i])); }
            std::cout<<"]\n";
            return 0;
        }
        if(command!="run" || argc!=7) throw std::runtime_error("invalid command/arguments");
        const fs::path rom_path=fs::u8path(argv[2]);
        const FileInfo info=inspect_file(rom_path);
        if(info.error!=nes::RomError::None) throw std::runtime_error(nes::describe(info.error));
        const uint64_t frame_count=number(argv[3]);
        if(!frame_count || frame_count>36000) throw std::runtime_error("frames must be 1..36000");
        const auto input=events(argv[4]);
        std::vector<uint8_t> file(size_t(info.bytes)),chr_ram(info.rom.chr_ram);
        std::ifstream in(rom_path,std::ios::binary);
        if(!in.read(reinterpret_cast<char*>(file.data()),std::streamsize(file.size()))) throw std::runtime_error("ROM changed/read failed");
        nes::Cartridge c;
        c.info=info.rom; c.prg=file.data()+c.info.prg_offset;
        c.chr=c.info.chr_bytes?file.data()+c.info.chr_offset:nullptr;
        c.chr_ram=chr_ram.empty()?nullptr:chr_ram.data();
        FrameCapture capture;
        const std::string sharp_arg=argv[6];
        if(sharp_arg!="on" && sharp_arg!="off") throw std::runtime_error("sharp must be on or off");
        nes::SharpControl display; display.enabled=sharp_arg=="on";
        capture.streaming.set_sharp(display.enabled); capture.completed_sharp=display.enabled;
        uint32_t display_toggles=0;
        nes::Machine machine;
        if(!machine.init(c,{&capture,FrameCapture::pixel,FrameCapture::frame})) throw std::runtime_error(nes::describe(machine.error));
        capture.apu=&machine.apu;
        size_t event_index=0;
        const auto begin=std::chrono::steady_clock::now();
        while(machine.ppu.frames<frame_count && machine.error==nes::MachineError::None) {
            while(event_index<input.size() && input[event_index].frame<=machine.ppu.frames) {
                const auto& event=input[event_index++];
                if(event.sharp) {
                    uint8_t rows[8]; std::memset(rows,0xff,8);
                    rows[0]&=uint8_t(~8); rows[7]&=uint8_t(~36);
                    if(display.update(rows)) { capture.streaming.set_sharp(display.enabled); ++display_toggles; }
                    std::memset(rows,0xff,8); display.update(rows);
                } else machine.controller.set(event.state);
            }
            machine.step();
        }
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        nes::VicFrame vic;
        nes::convert_frame(capture.completed.data(),vic,capture.completed_sharp);
        if(machine.error==nes::MachineError::None &&
           std::memcmp(vic.cells,capture.streaming.frame.cells,sizeof(vic.cells)))
            throw std::runtime_error("streaming/reference bitmap mismatch");
        const fs::path output=fs::u8path(argv[5]);
        fs::create_directories(output);
        // Diagnostic outputs only. No source ROM or extracted PRG/CHR is copied.
        save(output/"frame.idx",capture.completed.data(),capture.completed.size());
        save(output/"frame.vic",vic.cells,sizeof(vic.cells));
        std::array<uint8_t,256*240*3> rgb{};
        for(size_t i=0;i<capture.completed.size();++i) {
            const auto col=nes::diagnostic_nes_rgb(capture.completed[i]);
            rgb[i*3]=col.r; rgb[i*3+1]=col.g; rgb[i*3+2]=col.b;
        }
        save(output/"frame.rgb",rgb.data(),rgb.size());
        const uint16_t display_width=vic.hires?320:160;
        std::vector<uint8_t> vrgb(size_t(display_width)*200*3);
        for(uint16_t y=0;y<200;++y) for(uint16_t x=0;x<display_width;++x) {
            const auto col=nes::c64_rgb(nes::vic_pixel(vic,x,y));
            const size_t i=(y*display_width+x)*3;
            vrgb[i]=col.r; vrgb[i+1]=col.g; vrgb[i+2]=col.b;
        }
        save(output/"frame-vic.rgb",vrgb.data(),vrgb.size());
        save(output/"audio.sid",capture.audio.bytes,sizeof(capture.audio.bytes));
        std::cout<<"{\"passed\":"<<(machine.error==nes::MachineError::None?"true":"false")
                 <<",\"state\":"<<quote(nes::describe(machine.error))<<",\"mapper\":"<<info.rom.mapper
                 <<",\"frames\":"<<machine.ppu.frames<<",\"cycles\":"<<machine.cycles
                 <<",\"ppuDots\":"<<machine.ppu.ticks<<",\"instructions\":"<<machine.instructions
                 <<",\"pc\":"<<machine.cpu.PC<<",\"dmaTransfers\":"<<machine.dma_transfers<<",\"dmaCycles\":"<<machine.dma_cycles
                 <<",\"sprite0Hits\":"<<machine.ppu.sprite0_hits<<",\"nmiEdges\":"<<machine.ppu.nmi_edges
                 <<",\"controllerReads\":"<<machine.controller_reads<<",\"controller2Reads\":"<<machine.controller2_reads
                 <<",\"apuWrites\":"<<machine.apu.writes<<",\"mapperWrites\":"<<machine.cart.bank_writes
                 <<",\"sidPackets\":"<<capture.sid.packets<<",\"sidRetriggerPackets\":"<<capture.sid.retriggers
                 <<",\"sidNoiseSteals\":"<<capture.sid.noise_steals<<",\"sidMasterVolume\":"<<unsigned(capture.audio.bytes[25])
                 <<",\"busConflicts\":"<<machine.cart.bus_conflicts<<",\"finalBank\":"<<unsigned(machine.cart.bank)
                 <<",\"machineBytes\":"<<sizeof(machine)<<",\"vicFrameBytes\":"<<sizeof(vic)
                 <<",\"streamingRendererBytes\":"<<sizeof(capture.streaming)
                 <<",\"streamingReferenceEqual\":"<<(machine.error==nes::MachineError::None?"true":"null")
                 <<",\"sharpText\":"<<(vic.hires?"true":"false")<<",\"displayWidth\":"<<display_width
                 <<",\"displayToggles\":"<<display_toggles
                 <<",\"romBackingBytes\":"<<(info.rom.prg_bytes+info.rom.chr_bytes+info.rom.chr_ram)
                 <<",\"wallSeconds\":"<<seconds<<",\"scope\":\"PC host prototype with frame-sampled basic SID packets; no audible output or firmware/hardware acceptance\"}\n";
        return machine.error==nes::MachineError::None?0:2;
    } catch(const std::exception& e) {
        std::cerr<<"NESVM: "<<e.what()<<'\n'; return 1;
    }
}
