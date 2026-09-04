// Module-owned SdFat-shaped facade. No Arduino/SdFat symbols are linked here.
#pragma once
enum { O_RDONLY=VM_OPEN_READ,O_WRONLY=VM_OPEN_WRITE,O_RDWR=3,
       O_CREAT=VM_OPEN_CREATE,O_EXCL=VM_OPEN_EXCLUSIVE,O_TRUNC=VM_OPEN_TRUNCATE,T_WRITE=1 };
static int32_t diskOp(VmFsOp op,uint32_t h=0,uint32_t value=0,uint32_t extra=0,
                      const char *p=nullptr,const char *q=nullptr){
    VmFsRequest r{op,h,value,extra,p,q};return ModuleHost->file_op(&r);
}
struct FsFile {
    uint32_t handle=0,cursor=0;VmFileInfo info{};bool failed=false,listed=false;
    explicit operator bool()const{return handle||listed;}
    bool isOpen()const{return handle!=0;}
    bool isDirectory()const{return info.directory;}
    uint32_t fileSize()const{return info.bytes;}
    bool isReadOnly()const{return info.attributes&1;}
    bool isHidden()const{return info.attributes&2;}
    bool getError()const{return failed;}
    bool close(){bool ok=!handle||diskOp(VmFsOp::Close,handle)==0;handle=0;listed=false;return ok&&!failed;}
    bool seekSet(uint32_t o){cursor=o;return handle!=0;}
    int32_t read(void *p,uint32_t n){auto got=ModuleHost->read(handle,cursor,p,n);if(got<0)failed=true;else cursor+=got;return got;}
    uint32_t write(const void *p,uint32_t n){auto got=ModuleHost->write(handle,cursor,p,n);if(got<0){failed=true;return 0;}cursor+=got;if(cursor>info.bytes)info.bytes=cursor;return got;}
    bool sync(){return diskOp(VmFsOp::Flush,handle)==0;}
    bool truncate(uint32_t n){if(diskOp(VmFsOp::Truncate,handle,n))return false;info.bytes=n;return true;}
    bool timestamp(int,uint16_t y,uint8_t m,uint8_t d,uint8_t h,uint8_t min,uint8_t sec){
        return diskOp(VmFsOp::Timestamp,handle,((y-1980)<<9)|(m<<5)|d,(h<<11)|(min<<5)|(sec/2))==0;
    }
    bool getModifyDateTime(uint16_t *d,uint16_t *t){*d=info.date;*t=info.time;return true;}
    size_t getName(char *p,size_t n){size_t len=strlen(info.name);if(len>=n)return n;memcpy(p,info.name,len+1);return len;}
    bool openNext(FsFile *dir,int){int32_t r=ModuleHost->next(dir->handle,&info);if(r<0)dir->failed=true;listed=r==1;return listed;}
};
struct ModuleFiles {
    uint32_t total=0,free=0,clusterSectors=1;
    static bool path(const char *p,char out[384]){
        // The legacy DOS folder implementation's root is redirected by the
        // module, never by generic firmware; every installed VM owns its root.
        int n=strncmp(p,"/DOSVM/",7)?snprintf(out,384,"%s",p):snprintf(out,384,"%s/%s",ModuleHost->package_root,p+7);
        return n>=0&&n<384;
    }
    FsFile open(const char *p,int flags){FsFile f;char full[384];if(path(p,full))f.handle=ModuleHost->open_flags(full,flags,&f.info);return f;}
    bool exists(const char *p){auto f=open(p,O_RDONLY);bool exists=bool(f);f.close();return exists;}
    bool operation(VmFsOp op,const char *p,bool parents=false,const char *q=nullptr){
        char a[384],b[384];if(!path(p,a)||(q&&!path(q,b)))return false;return diskOp(op,0,parents,0,a,q?b:nullptr)==0;
    }
    bool mkdir(const char *p,bool parents){return operation(VmFsOp::Mkdir,p,parents);}
    bool rmdir(const char *p){return operation(VmFsOp::Rmdir,p);}
    bool remove(const char *p){return operation(VmFsOp::Remove,p);}
    bool rename(const char *p,const char *q){return operation(VmFsOp::Rename,p,false,q);}
    uint32_t clusterCount(){VmFsRequest r{VmFsOp::Space};if(ModuleHost->file_op(&r)||!r.handle){total=free=0;return 0;}clusterSectors=r.handle;total=r.value/clusterSectors;free=r.extra/clusterSectors;return total;}
    uint32_t freeClusterCount(){return free;}
    uint32_t sectorsPerCluster(){return clusterSectors;}
};
static struct { ModuleFiles sdfs; } SD;
