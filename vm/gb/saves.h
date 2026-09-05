// SPDX-License-Identifier: GPL-2.0-or-later
// Two checked slots: never overwrite the last verified battery save. ROMs and
// unrelated saves are read-only. Power loss is detected by length/CRC checks.
#pragma once
class BatteryStore {
    struct Header {uint32_t magic,rom,bytes,sequence,crc;};
    const VmHost *host=nullptr;
    char directory[320],path[352];
    uint32_t identity=0,serial=0,savedRevision=0;
    int active=-1;
    bool enabled=false;
    uint8_t *scratch(){return host->workspace;}
    void filename(unsigned slot){snprintf(path,sizeof path,"%s/%08lx.s%u",directory,(unsigned long)identity,slot);}
    bool readSlot(unsigned slot,Header &header,bool &exists){
        filename(slot);VmFileInfo info{};auto f=host->open(path,&info);exists=f!=0;
        if(!f)return false;
        const unsigned bytes=gb::saveBytes();
        bool ok=!info.directory&&info.bytes==sizeof header+bytes&&
            host->read(f,0,&header,sizeof header)==sizeof header&&
            header.magic==0x31534247&&header.rom==identity&&header.bytes==bytes&&
            host->read(f,sizeof header,scratch(),bytes)==int32_t(bytes)&&
            vm_crc32(scratch(),bytes)==header.crc;
        host->close(f);return ok;
    }
public:
    bool load(const VmHost *h,uint32_t romCrc){
        host=h;identity=romCrc;serial=savedRevision=0;active=-1;enabled=gb::saveBytes()!=0;
        if(!enabled)return true;
        if(!host->workspace||host->workspace_bytes<gb::saveBytes()||!host->open_flags||!host->write||!host->file_op)return false;
        if(snprintf(directory,sizeof directory,"%s/SAVES",host->package_root)>=(int)sizeof directory)return false;
        bool found=false;
        for(unsigned slot=0;slot<2;slot++){
            Header header{};bool exists=false;
            if(readSlot(slot,header,exists)&&(active<0||int32_t(header.sequence-serial)>0)){
                memcpy(gb::saveData(),scratch(),gb::saveBytes());serial=header.sequence;active=slot;
            }
            found|=exists;
        }
        // Do not silently replace damaged existing saves with a new game.
        return !found||active>=0;
    }
    bool dirty() const {return enabled&&gb::saveRevision()!=savedRevision;}
    bool flush(){
        if(!dirty())return true;
        VmFileInfo info{};auto dir=host->open(directory,&info);
        if(dir){host->close(dir);if(!info.directory)return false;}
        else{VmFsRequest request{VmFsOp::Mkdir,0,1,0,directory,nullptr};if(host->file_op(&request))return false;}
        const unsigned slot=active<0?0:1-active,bytes=gb::saveBytes();
        filename(slot);auto f=host->open_flags(path,VM_OPEN_WRITE|VM_OPEN_CREATE|VM_OPEN_TRUNCATE,&info);
        if(!f)return false;
        const Header header{0x31534247,identity,bytes,serial+1,vm_crc32(gb::saveData(),bytes)};
        bool ok=host->write(f,sizeof header,gb::saveData(),bytes)==int32_t(bytes)&&
            host->write(f,0,&header,sizeof header)==sizeof header;
        VmFsRequest request{VmFsOp::Flush,f,0,0,nullptr,nullptr};
        if(ok)ok=host->file_op(&request)==0;
        request.operation=VmFsOp::Close;if(host->file_op(&request))ok=false;
        Header checked{};bool exists=false;
        if(!ok||!readSlot(slot,checked,exists)||memcmp(&header,&checked,sizeof header))return false;
        active=slot;serial=header.sequence;savedRevision=gb::saveRevision();return true;
    }
};
