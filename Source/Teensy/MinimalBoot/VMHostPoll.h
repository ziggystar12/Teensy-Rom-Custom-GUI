// Foreground scheduler shared verbatim by firmware and the integration test.
// Platform/bus services are supplied by VMHost.h (test stubs only replace I/O).
void VMHostPoll(){
    using namespace VmRuntime;if(!active)return;
    if(startRequested&&!started){started=true;startRequested=false;if(failure){fail(failure);return;}EZFlashRAM[0xf5]=2;}
    if(!started||failure||!module)return;
    if(inputPending){VmInput in{input.buttons,input.display,input.overflow,input.protocol};inputPending=false;
        if(in.protocol==0x83){
            if(indexedVideo.configured&&in.display<4){
                const uint8_t mode=in.display==0?indexedVideo.preferred:in.display;
                if(indexedVideo.capabilities&(1u<<mode))indexedVideo.requested=mode;
                in.protocol=0x81;in.display=1;module->input(&in);
            }
        }else module->input(&in);
    }
    if(pending&&EZFlashRAM[0xf6]==sequence){
        if(indexedVideo.hostPacket){indexedVideo.phase=indexedVideo.phase==1?2:4;indexedVideo.hostPacket=false;}else module->ack();
        pending=false;quietRequested=false;EZFlashRAM[0xf5]=2;
    }
    // Consume an ACK BEFORE pumping: a module may defer input/scene changes
    // while its packet is frozen. Pump-before-ACK followed immediately by
    // packet() starves such input forever when every idle turn emits a packet.
    // An acknowledged packet must NOT consume the next emulation slice.
    // Otherwise a responsive client + recurring SID packets can starve the
    // game clock indefinitely. Publication still follows one bounded pump.
    sliceStarted=micros();
    if(quietRequested){EZFlashRAM[0xf5]=0x12;}else module->pump();
    if(failure||pending)return;
    if(quietRequested)return;
    if(indexedVideo.phase==2){if(!transferIndexedVideo()){fail(0x17);return;}indexedVideo.phase=3;}
    indexedVideo.hostPacket=indexedVideoPacket(packet);
    if(!indexedVideo.hostPacket){
        if(!module->packet(&packet)){
            indexedVideo.hostPacket=indexedVideoPacket(packet);if(!indexedVideo.hostPacket)return;
        }
    }
    if(failure)return;
    if(packet.length>228||packet.reserved||!packet.type){fail(0x15);return;}
    uint8_t bytes[240];sequence=sequence==255?1:sequence+1;
    bytes[0]='M';bytes[1]='3';bytes[2]=1;bytes[3]=packet.type;bytes[4]=sequence;bytes[5]=packet.flags;bytes[6]=packet.length;bytes[7]=0;
    memcpy(bytes+8,packet.payload,packet.length);const auto crc=crc16(bytes,8+packet.length);bytes[8+packet.length]=crc;bytes[9+packet.length]=crc>>8;
    for(unsigned i=0;i<10u+packet.length;i++)EZFlashRAM[i]=bytes[i];
    pending=true;
#if defined(__arm__)
    __asm__ volatile("dmb":::"memory");
#endif
    EZFlashRAM[0xf7]=sequence;
}
