// Pixel geometry shared by every surface in the desktop design study.
// Native counterparts live in GeosWidgets.s; both use integer 320x200 pixels.
export const geometry = Object.freeze({ window: [4,12,312,176], close: 11,
  browserWindow: [4,12,312,188],
  grid: { x:8, rowY:[36,68,100,132,164], columns:4, rows:5, width:72, height:32 },
  scroll: [302,36,12,164], dialog: [24,42,272,116],
  control: [40,16,240,168], settings: [16,12,288,176] });
export const contains = (r,x,y) => x>=r[0] && y>=r[1] && x<r[0]+r[2] && y<r[1]+r[3];

export function createWidgets(ctx, assets) {
  const ink='#000', paper='#fff';
  const fill=(x,y,w,h,color=ink)=>{ctx.fillStyle=color;ctx.fillRect(x,y,w,h);};
  const frame=(x,y,w,h,color=paper)=>{fill(x,y,w,h);fill(x+1,y+1,w-2,h-2,color);};
  const text=(s,x,y,color=ink)=>{
    for(const ch of String(s)) {
      const code=ch.charCodeAt(0), offset=((code>=32&&code<127?code:63)-32)*8;
      for(let row=0;row<7;row++)for(let col=0;col<5;col++)
        if(assets.font[offset+row]&(128>>col))fill(x+col,y+row,1,1,color);
      x+=6;
    }
  };
  const centered=(s,x,y,w,color=ink)=>text(s,x+Math.floor((w-(s.length*6-1))/2),y,color);
  const glyph=(rows,x,y,color=ink)=>rows.forEach((row,j)=>[...row].forEach((v,i)=>{if(v==='1')fill(x+i,y+j,1,1,color);}));
  const close=(x,y,pressed=false)=>{
    frame(x,y,11,11,pressed?ink:paper);
    glyph(['1000001','0100010','0010100','0001000','0010100','0100010','1000001'],x+2,y+2,pressed?paper:ink);
    return [x,y,11,11];
  };
  const button=(label,x,y,w,{focused=false,pressed=false,disabled=false}={})=>{
    const active=focused||pressed;
    frame(x,y,w,15,active?ink:paper);
    centered(label,x,y+4,w,active?paper:ink);
    if(disabled)for(let yy=y+2;yy<y+13;yy++)for(let xx=x+2+(yy&1);xx<x+w-2;xx+=2)fill(xx,yy,1,1,paper);
    return [x,y,w,15];
  };
  const window=(title,rect=geometry.window)=>{
    const [x,y,w,h]=rect;frame(x,y,w,h);fill(x+1,y+16,w-2,1);
    centered(title,x,y+4,w);
    return {rect,close:close(x+w-14,y+2),body:[x+1,y+17,w-2,h-18]};
  };
  const checkbox=(label,x,y,checked)=>{
    frame(x,y,9,9);if(checked)glyph(['00001','00010','10100','01000'],x+2,y+2);
    text(label,x+15,y+1);return[x,y,label.length*6+15,9];
  };
  const scrollbar=(rect,top,total,visible)=>{
    const [x,y,w,h]=rect;frame(x,y,w,h);
    fill(x+1,y+11,w-2,1);fill(x+1,y+h-12,w-2,1);
    glyph(['00100','01110','11111'],x+3,y+4);
    glyph(['11111','01110','00100'],x+3,y+h-7);
    const track=[x+1,y+12,w-2,h-24], travel=track[3];
    const thumbH=total<=visible?travel:Math.max(11,Math.floor(travel*visible/total));
    const thumbY=track[1]+Math.round((travel-thumbH)*(top/Math.max(1,total-visible)));
    const thumb=[x+1,thumbY,w-2,thumbH];fill(...thumb);
    return {rect,track,thumb,up:[x,y,w,12],down:[x,y+h-12,w,12]};
  };
  const icon=(id,x,y)=>{
    const found=assets.icons.find(v=>v.id===id);
    if(found){for(let row=0;row<16;row++)for(let col=0;col<24;col++)if(found.bytes[row*3+(col>>3)]&(128>>(col&7)))fill(x+col,y+row,1,1);return;}
    frame(x+5,y,14,16);fill(x+14,y,5,5,paper);fill(x+14,y,1,5);fill(x+14,y+4,5,1);
    if(id==='program'){fill(x+8,y+7,7,1);fill(x+8,y+9,5,1);fill(x+8,y+11,7,1);}
    else {fill(x+8,y+7,8,1);fill(x+8,y+10,8,1);}
  };
  return {fill,frame,text,centered,glyph,close,button,window,checkbox,scrollbar,icon,ink,paper};
}

export function filenameLines(name,width=11) {
  if(name.length<=width)return[name];
  if(name.length<=width*2){
    const split=name.lastIndexOf('.',width);
    const at=split>0&&name.length-split<=width?split:width;
    return[name.slice(0,at),name.slice(at)];
  }
  const dot=name.lastIndexOf('.'),ext=dot>0?name.slice(dot):'';
  const shortened=ext.length>0&&ext.length<=width
    ?name.slice(0,width*2-3-ext.length)+'...'+ext
    :name.slice(0,width*2-3)+'...';
  return[shortened.slice(0,width),shortened.slice(width)];
}
