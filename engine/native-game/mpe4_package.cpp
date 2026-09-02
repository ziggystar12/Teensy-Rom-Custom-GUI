#include "mpe4_package.h"
#include <string.h>
namespace mpe4 {
static MPE4_CODE uint16_t pack16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
static MPE4_CODE uint32_t pack32(const uint8_t *p) { return pack16(p) | (uint32_t(pack16(p+2)) << 16); }
MPE4_CODE uint32_t crc32Update(uint32_t value, const uint8_t *data, size_t n) {
  while(n--) { value ^= *data++; for(uint8_t b=0;b<8;b++) value=(value>>1)^((value&1)?0xedb88320u:0u); }
  return value;
}
MPE4_CODE bool Package::raw(uint32_t offset, uint8_t *out, uint16_t n) {
  if(offset > bytes || n > bytes-offset) return false;
  while(n) {
    if(!cacheBytes || offset < cacheStart || offset >= cacheStart+cacheBytes) {
      cacheStart=offset&~511u;
      cacheBytes=uint16_t(bytes-cacheStart > sizeof(cache) ? sizeof(cache) : bytes-cacheStart);
      if(!reader(context,root+cacheStart,cache,cacheBytes)) { cacheBytes=0; return false; }
    }
    uint16_t take=uint16_t(cacheStart+cacheBytes-offset);
    if(take>n) take=n;
    memcpy(out,cache+(offset-cacheStart),take); out+=take; offset+=take; n-=take;
  }
  return true;
}
MPE4_CODE bool Package::entry(uint16_t index, Entry &out) {
  uint8_t b[16];
  if(index>=count || !raw(indexOffset+uint32_t(index)*16,b,sizeof(b)) || pack16(b+2)) return false;
  out.type=b[0]; out.id=b[1]; out.offset=pack32(b+4); out.length=pack32(b+8); out.crc=pack32(b+12);
  return out.type<=6 && out.length && !(out.offset&3) && out.offset>=dataOffset &&
    out.offset<=bytes && out.length<=bytes-out.offset;
}
MPE4_CODE bool Package::open(RawRead fn, void *ctx, uint32_t base, uint32_t limit) {
  ready=previousValid=false; originalStartup=false; cacheBytes=0; reader=fn; context=ctx; root=base; rawLimit=limit;
  uint8_t h[64];
  if(!fn || base>limit || limit-base<64 || !fn(ctx,base,h,64)) return false;
  if(memcmp(h,"M4G1",4) || pack16(h+4)!=1 || pack16(h+6)!=64) return false;
  bytes=pack32(h+8); indexOffset=pack32(h+12); count=pack16(h+16); dataOffset=pack32(h+20); crc=pack32(h+24);
  if(bytes<64 || bytes>limit-base || indexOffset!=64 || !count || count>1027 || pack16(h+18)!=16 ||
     dataOffset != ((64u+uint32_t(count)*16u+3u)&~3u) || dataOffset>bytes || (pack32(h+32)&~1u)) return false;
  originalStartup=(h[32]&1)!=0;
  for(uint8_t i=36;i<64;i++) if(h[i]) return false;
  uint32_t expected=pack32(h+28); memset(h+28,0,4);
  if((crc32Update(0xffffffffu,h,64)^0xffffffffu)!=expected) return false;
  uint32_t c=0xffffffffu;
  for(uint32_t p=64;p<bytes;) {
    uint16_t n=uint16_t(bytes-p>sizeof(cache)?sizeof(cache):bytes-p);
    if(!fn(ctx,base+p,cache,n)) return false;
    c=crc32Update(c,cache,n); p+=n;
  }
  cacheBytes=0;
  if((c^0xffffffffu)!=crc) return false;
  uint32_t end=dataOffset; int32_t key=-1;
  for(uint16_t i=0;i<count;i++) {
    Entry e; if(!entry(i,e)) return false;
    int32_t next=(uint16_t(e.type)<<8)|e.id;
    if(next<=key || e.offset<end) return false;
    key=next; end=e.offset+e.length;
  }
  ready=true;
  for(uint8_t t=4;t<=6;t++) { Entry e; if(!find(t,0,e)) { ready=false; return false; } }
  return true;
}
MPE4_CODE bool Package::find(uint8_t type, uint8_t id, Entry &out) {
  if(!ready) return false;
  if(previousValid && previous.type==type && previous.id==id) { out=previous; return true; }
  uint16_t low=0,high=count,key=(uint16_t(type)<<8)|id;
  while(low<high) {
    uint16_t mid=uint16_t(low+(high-low)/2); Entry e;
    if(!entry(mid,e)) return false;
    uint16_t k=(uint16_t(e.type)<<8)|e.id;
    if(k==key) { previous=out=e; previousValid=true; return true; }
    if(k<key) low=mid+1; else high=mid;
  }
  return false;
}
MPE4_CODE uint32_t Package::size(uint8_t type,uint8_t id) { Entry e; return find(type,id,e)?e.length:0; }
MPE4_CODE bool Package::read(uint8_t type,uint8_t id,uint32_t offset,uint8_t *out,uint16_t n) {
  Entry e; return find(type,id,e) && offset<=e.length && n<=e.length-offset && raw(e.offset+offset,out,n);
}
MPE4_CODE bool Package::verify(uint8_t type,uint8_t id) {
  Entry e; if(!find(type,id,e)) return false;
  uint32_t c=0xffffffffu; uint8_t b[64];
  for(uint32_t p=0;p<e.length;) { uint16_t n=uint16_t(e.length-p>64?64:e.length-p);
    if(!raw(e.offset+p,b,n)) return false; c=crc32Update(c,b,n); p+=n; }
  return (c^0xffffffffu)==e.crc;
}
}
