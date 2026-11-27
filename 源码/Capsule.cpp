#define NOMINMAX
#include "Capsule.h"
#include "Dpi.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Gdiplus;
namespace {
void RoundPath(GraphicsPath& p,RectF r,float radius){
    float d=radius*2;p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
    p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();
}
constexpr float OuterPadding=7, ColumnGap=6, NameGap=3, IconSpace=15;
float FontPixels(float rowHeight,float scale){return std::max(2.f*scale,std::min(13.5f*scale,rowHeight-1.5f*scale));}
bool Percent(const std::wstring& value){return value.ends_with(L'%');}
bool Approximate(const std::wstring& value){return value.starts_with(L'约');}
bool SameWidth(const MonitorItem& a,const MonitorItem& b){
    if(a.batteryIcon!=b.batteryIcon || a.networkRate!=b.networkRate || a.Label()!=b.Label())return false;
    if(a.networkRate)return true;
    if(Percent(a.value) && Percent(b.value))return Approximate(a.value)==Approximate(b.value);
    return a.value==b.value;
}
float Width(Graphics&g,const std::wstring&s,Font*f){RectF b;StringFormat sf(StringFormat::GenericTypographic());g.MeasureString(s.c_str(),int(s.size()),f,PointF(0,0),&sf,&b);return std::ceil(b.Width);}
struct ValueParts {
    std::wstring digits;
    float prefixWidth, digitWidth, unitWidth;
    bool percent, approximate;
    float Total() const {return prefixWidth+digitWidth+unitWidth;}
};
ValueParts MeasureValue(Graphics& g,const std::wstring& value,Font* numbers,Font* units,float scale,bool reserve){
    bool percent=Percent(value), approximate=Approximate(value);
    auto digits=value.substr(approximate?1:0,value.size()-size_t(approximate)-size_t(percent));
    float prefix=approximate?Width(g,L"约",units)+scale:0;
    float unit=percent?Width(g,L"%",units)+1.5f*scale:0;
    float number=Width(g,reserve&&percent?L"888":digits,numbers);
    return {std::move(digits),prefix,number,unit,percent,approximate};
}
struct CellWidth { float preferred, minimum; };
CellWidth MeasureCell(Graphics& g,const MonitorItem& item,Font* label,Font* numbers,Font* units,float scale,bool reserve){
    const float name=Width(g,item.Label(),label);
    if(item.value.empty())return {name,std::min(name,12*scale)};
    float value=reserve && item.networkRate ? Width(g,L"888 MB/s",numbers) :
        MeasureValue(g,item.value,numbers,units,scale,reserve).Total();
    float fixed=value+(NameGap+(item.batteryIcon?IconSpace:0))*scale;
    return {fixed+name, fixed+std::min(name,12*scale)};
}
// Share any width shortage between long labels; retain room for both values.
float LeftWidth(CellWidth left,CellWidth right,float available){
    const float low=std::min(left.minimum,available/2);
    const float high=std::max(low,available-std::min(right.minimum,available));
    const float split=std::clamp(available/2,low,high);
    return std::clamp(std::min(left.preferred,std::max(split,available-right.preferred)),low,high);
}
void Text(Graphics&g,const std::wstring&s,Font*f,Color color,RectF box,float haloOffset,bool light,bool crop=true){
    if(box.Width<=0)return;std::wstring shown=s;
    if(crop&&Width(g,shown,f)>box.Width){while(!shown.empty()&&Width(g,shown+L"…",f)>box.Width)shown.pop_back();shown+=L"…";}
    SolidBrush brush(color);StringFormat sf(StringFormat::GenericTypographic());sf.SetFormatFlags(StringFormatFlagsNoWrap|StringFormatFlagsNoClip);sf.SetLineAlignment(StringAlignmentNear);sf.SetAlignment(StringAlignmentNear);
    // A layout rectangle can drop an entire number at fractional font sizes.
    // Explicit baseline placement keeps small rows readable at every DPI.
    PointF origin(box.X,box.Y+(box.Height-f->GetHeight(&g))/2);
    if(haloOffset>0){
        SolidBrush halo(light?Color(155,255,255,255):Color(155,0,0,0));
        for(PointF offset:{PointF(-haloOffset,0),PointF(haloOffset,0),PointF(0,-haloOffset),PointF(0,haloOffset)})
            g.DrawString(shown.c_str(),int(shown.size()),f,PointF(origin.X+offset.X,origin.Y+offset.Y),&sf,&halo);
    }
    g.DrawString(shown.c_str(),int(shown.size()),f,origin,&sf,&brush);
}
void Battery(Graphics&g,RectF r,int percentage,Color color,float scale){
    GraphicsPath path;RoundPath(path,r,1.4f*scale);Pen edge(color,scale);g.DrawPath(&edge,&path);
    SolidBrush brush(color);g.FillRectangle(&brush,r.GetRight()+scale,r.Y+r.Height*.3f,1.4f*scale,r.Height*.4f);
    if(percentage>0){RectF charge(r.X+1.8f*scale,r.Y+1.8f*scale,(r.Width-3.6f*scale)*std::min(100,percentage)/100.f,r.Height-3.6f*scale);g.FillRectangle(&brush,charge);}
}
double Luminance(Color color){
    auto linear=[](BYTE channel){
        double value=channel/255.0;
        return value<=0.04045 ? value/12.92 : std::pow((value+0.055)/1.055,2.4);
    };
    return 0.2126*linear(color.GetR()) + 0.7152*linear(color.GetG()) + 0.0722*linear(color.GetB());
}
Color ReadableInk(Color preferred, double backgroundLuminance, Color fallback, double minimum){
    auto ink=Luminance(preferred);
    double ratio=(std::max(ink,backgroundLuminance)+0.05)/(std::min(ink,backgroundLuminance)+0.05);
    return ratio>=minimum ? preferred : fallback;
}
bool SavePng(Bitmap&image,const std::wstring&path){
    CLSID encoder;UINT n=0,size=0;GetImageEncodersSize(&n,&size);std::vector<BYTE> storage(size);auto entries=reinterpret_cast<ImageCodecInfo*>(storage.data());GetImageEncoders(n,size,entries);
    for(UINT i=0;i<n;i++)if(wcscmp(entries[i].MimeType,L"image/png")==0){encoder=entries[i].Clsid;return image.Save(path.c_str(),&encoder,nullptr)==Ok;}return false;
}
}

CapsulePainter::CapsulePainter(){GdiplusStartupInput input;if(GdiplusStartup(&token,&input,nullptr)!=Ok)throw std::runtime_error("GDI+ startup failed");}
void CapsulePainter::ReleaseBuffer(){canvas.reset();surface.reset();if(bufferDc){if(previousBitmap)SelectObject(bufferDc,previousBitmap);if(bufferBitmap)DeleteObject(bufferBitmap);DeleteDC(bufferDc);}bufferDc=nullptr;bufferBitmap=nullptr;previousBitmap=nullptr;bufferWidth=bufferHeight=0;}
void CapsulePainter::ReleaseHoverBuffer(){hoverCanvas.reset();hoverSurface.reset();if(hoverBufferDc){if(hoverPreviousBitmap)SelectObject(hoverBufferDc,hoverPreviousBitmap);if(hoverBufferBitmap)DeleteObject(hoverBufferBitmap);DeleteDC(hoverBufferDc);}hoverBufferDc=nullptr;hoverBufferBitmap=nullptr;hoverPreviousBitmap=nullptr;hoverBufferWidth=hoverBufferHeight=0;}
CapsulePainter::~CapsulePainter(){ReleaseBuffer();ReleaseHoverBuffer();labelFont.reset();valueFont.reset();unitFont.reset();if(token)GdiplusShutdown(token);}
void CapsulePainter::Fonts(float size){
    if(std::abs(fontSize-size)<.1f&&labelFont)return;fontSize=size;
    labelFont=std::make_unique<Font>(L"Microsoft YaHei UI",size*.86f,FontStyleRegular,UnitPixel);
    valueFont=std::make_unique<Font>(L"Segoe UI Semibold",size,FontStyleRegular,UnitPixel);
    unitFont=std::make_unique<Font>(L"Microsoft YaHei UI",size*.8f,FontStyleRegular,UnitPixel);
}
int CapsulePainter::PreferredWidth(int logicalHeight,int rows,const std::vector<MonitorItem>& items){
    int count=std::min<int>(items.size(),rows*2);
    int usedRows=std::max(1,(count+1)/2);
    if(measuredHeight==logicalHeight && measuredRows==usedRows && widthItems.size()==count &&
       std::equal(widthItems.begin(),widthItems.end(),items.begin(),SameWidth))return measuredWidth;
    // Size in logical pixels; reserve three digits so routine percentage updates do not move the window.
    constexpr float scale=2;
    Bitmap sample(1,1,PixelFormat32bppPARGB);
    Graphics g(&sample);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    Fonts(FontPixels((logicalHeight-8)*scale/usedRows,scale));
    float content=0;
    for(int i=0;i<count;i+=2){
        float row=MeasureCell(g,items[i],labelFont.get(),valueFont.get(),unitFont.get(),scale,true).preferred;
        if(i+1<count)row+=ColumnGap*scale+MeasureCell(g,items[i+1],labelFont.get(),valueFont.get(),unitFont.get(),scale,true).preferred;
        content=std::max(content,row);
    }
    measuredWidth=std::clamp(int(std::ceil(content/scale+2*OuterPadding+2)),64,236);
    measuredHeight=logicalHeight;measuredRows=usedRows;
    widthItems.assign(items.begin(),items.begin()+count);
    return measuredWidth;
}
SIZE CapsulePainter::HoverListSize(const std::vector<MonitorItem>&items){
    if(items.empty())return SIZE{0,0};
    constexpr float scale=1;
    Bitmap sample(1,1,PixelFormat32bppPARGB);
    Graphics g(&sample);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    Fonts(13.5f*scale);
    float widest=0;
    for(const auto& item:items){
        float group=Width(g,item.name,labelFont.get());
        if(!item.value.empty())group+=(NameGap+((item.batteryIcon)?IconSpace:0))*scale+
            MeasureValue(g,item.value,valueFont.get(),unitFont.get(),scale,true).Total();
        widest=std::max(widest,group);
    }
    int width=std::clamp(int(std::ceil(widest+28)),176,360);
    int height=std::clamp(24+int(std::min<size_t>(items.size(),16))*28,52,472);
    return SIZE{width,height};
}

void CapsulePainter::Draw(Graphics&g,float width,float height,int rows,bool light,bool hover,const std::vector<MonitorItem>&items,float scale,CapsuleBackground background){
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    int count=std::min<int>(int(items.size()),rows*2);rows=std::max(1,std::min(rows,(count+1)/2));
    float padding=OuterPadding*scale,vertical=4*scale,rowHeight=(height-2*vertical)/rows;
    Fonts(FontPixels(rowHeight,scale));
    const bool custom=background.mode==BackgroundMode::Custom;
    const bool transparent=background.mode==BackgroundMode::Transparent;
    Color selected(255,GetRValue(background.color),GetGValue(background.color),GetBValue(background.color));
    double luminance=custom ? Luminance(selected) : 0;
    if(custom) light=luminance>0.179;
    Color ink=light ? Color(255,0,0,0) : Color(255,255,255,255);
    Color label=light?Color(255,111,120,132):Color(255,161,171,185);
    Color number=light?Color(255,36,46,61):Color(255,239,244,251);
    Color unit=light?Color(255,135,144,156):Color(255,145,157,174);
    GraphicsPath outline;RoundPath(outline,RectF(.6f*scale,.6f*scale,width-1.2f*scale,height-1.2f*scale),std::min(14*scale,height*.34f));
    if(transparent){
        // Alpha-zero pixels pass mouse input through a layered window. Keep empty interior
        // pixels hit-testable for the menu, double click and desktop dragging.
        SolidBrush hitArea(Color(1,0,0,0));g.FillPath(&hitArea,&outline);
        label=light?Color(255,39,48,59):Color(255,235,239,244);
        unit=label; number=ink;
    }else if(custom){
        SolidBrush fill(selected);g.FillPath(&fill,&outline);
        label=ReadableInk(label,luminance,ink,4.5);
        number=ReadableInk(number,luminance,ink,4.5);
        unit=ReadableInk(unit,luminance,ink,4.5);
    }else{
        Color top=light?Color(255,255,255,255):Color(255,48,51,56);
        Color bottom=light?Color(255,245,247,250):Color(255,36,39,44);
        if(hover){top=light?Color(255,255,255,255):Color(255,56,60,66);bottom=light?Color(255,249,251,253):Color(255,42,46,52);}
        Color edge=light?Color(230,213,219,227):Color(210,79,85,94);
        LinearGradientBrush fill(PointF(0,0),PointF(0,height),top,bottom);g.FillPath(&fill,&outline);
        Pen border(edge,.8f*scale);g.DrawPath(&border,&outline);
    }
    float haloOffset=transparent ? std::max(0.75f,0.65f*scale) : 0;
    const float inner=width-2*padding, gap=ColumnGap*scale;
    for(int row=0;row<rows;row++){
        int remaining=count-row*2;if(remaining<=0)break;
        bool single=remaining==1;
        float left=inner;
        if(!single){
            auto measure=[&](int i){return MeasureCell(g,items[i],labelFont.get(),valueFont.get(),unitFont.get(),scale,false);};
            left=LeftWidth(measure(row*2),measure(row*2+1),std::max(0.f,inner-gap));
            Pen separator(light?Color(160,218,224,231):Color(125,93,103,119),scale);
            float x=padding+left+gap/2, y=vertical+row*rowHeight;
            g.DrawLine(&separator,x,y+rowHeight*.3f,x,y+rowHeight*.7f);
        }
        for(int col=0;col<(single?1:2);col++){
            int index=row*2+col;const auto& item=items[index];
            RectF cell(col?padding+left+gap:padding,vertical+row*rowHeight,col?inner-left-gap:left,rowHeight);
            if(cell.Width<=0)continue;
            auto clip=g.Save(); g.SetClip(cell,CombineModeIntersect);
            const auto& name=item.Label();
            float nameWidth=Width(g,name,labelFont.get());
            if(item.value.empty()){
                float group=std::min(cell.Width,nameWidth);
                Text(g,name,labelFont.get(),label,RectF(cell.X,cell.Y,group,cell.Height),haloOffset,light);g.Restore(clip);continue;
            }
            auto value=MeasureValue(g,item.value,valueFont.get(),unitFont.get(),scale,false);
            bool device=item.batteryIcon;
            float glyph=device?IconSpace*scale:0;
            float group=std::min(cell.Width,glyph+nameWidth+NameGap*scale+value.Total());
            // Each cell starts at the same left inset. Keep the label, icon and
            // value together instead of centering short names in the column.
            cell.Width=group;
            float valueX=std::max(cell.X,cell.GetRight()-value.Total());
            if(device){int n=value.percent?_wtoi(value.digits.c_str()):-1;Color status=n>=0&&n<=20?(light?Color(255,189,127,43):Color(255,230,180,106)):(light?Color(255,65,139,120):Color(255,128,194,172));if(custom)status=ReadableInk(status,luminance,ink,3.0);Battery(g,RectF(cell.X,cell.Y+(rowHeight-7*scale)/2,10*scale,7*scale),n,status,scale);}
            Text(g,name,labelFont.get(),label,RectF(cell.X+glyph,cell.Y,std::max(0.f,valueX-cell.X-glyph-NameGap*scale),cell.Height),haloOffset,light);
            if(value.approximate)Text(g,L"约",unitFont.get(),unit,RectF(valueX,cell.Y+.5f*scale,value.prefixWidth+4*scale,cell.Height),haloOffset,light,false);
            Text(g,value.digits,valueFont.get(),number,RectF(valueX+value.prefixWidth,cell.Y,value.digitWidth+4*scale,cell.Height),haloOffset,light,false);
            if(value.percent)Text(g,L"%",unitFont.get(),unit,RectF(valueX+value.prefixWidth+value.digitWidth+1.5f*scale,cell.Y+scale,value.unitWidth+4*scale,cell.Height),haloOffset,light,false);
            g.Restore(clip);
        }
    }
}
void CapsulePainter::DrawHoverList(Graphics&g,float width,float height,bool light,bool hover,std::span<const MonitorItem> items,float scale,CapsuleBackground background,bool more){
    if(items.empty())return;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    const float padding=12*scale;
    const float rowHeight=(height-2*padding-(more?22*scale:0))/float(items.size());
    Fonts(FontPixels(rowHeight,scale));
    const bool custom=background.mode==BackgroundMode::Custom;
    const bool transparent=background.mode==BackgroundMode::Transparent;
    Color selected(255,GetRValue(background.color),GetGValue(background.color),GetBValue(background.color));
    double luminance=custom?Luminance(selected):0;
    if(custom)light=luminance>0.179;
    Color ink=light?Color(255,0,0,0):Color(255,255,255,255);
    Color label=light?Color(255,111,120,132):Color(255,161,171,185);
    Color number=light?Color(255,36,46,61):Color(255,239,244,251);
    Color unit=light?Color(255,135,144,156):Color(255,145,157,174);
    GraphicsPath outline;
    RoundPath(outline,RectF(.6f*scale,.6f*scale,width-1.2f*scale,height-1.2f*scale),std::min(12*scale,height*.12f));
    if(transparent){
        SolidBrush hitArea(Color(1,0,0,0));
        g.FillPath(&hitArea,&outline);
        label=light?Color(255,39,48,59):Color(255,235,239,244);
        unit=label;
        number=ink;
    }else if(custom){
        SolidBrush fill(selected);
        g.FillPath(&fill,&outline);
        label=ReadableInk(label,luminance,ink,4.5);
        number=ReadableInk(number,luminance,ink,4.5);
        unit=ReadableInk(unit,luminance,ink,4.5);
        Pen border(Color(120,255,255,255),.7f*scale);
        g.DrawPath(&border,&outline);
    }else{
        Color top=light?Color(250,255,255,255):Color(248,48,51,56);
        Color bottom=light?Color(246,245,247,250):Color(246,36,39,44);
        if(hover){top=light?Color(252,255,255,255):Color(250,56,60,66);bottom=light?Color(250,249,251,253):Color(248,42,46,52);}
        Color edge=light?Color(220,213,219,227):Color(205,79,85,94);
        LinearGradientBrush fill(PointF(0,0),PointF(0,height),top,bottom);
        g.FillPath(&fill,&outline);
        Pen border(edge,.8f*scale);
        g.DrawPath(&border,&outline);
    }
    if(more)Text(g,L"滚轮翻阅  ·  点击翻页",unitFont.get(),unit,RectF(padding,height-30*scale,width-2*padding,22*scale),0,light);
    const float haloOffset=transparent?std::max(.75f,.65f*scale):0;
    for(size_t index=0;index<items.size();++index){
        const auto& item=items[index];
        float y=padding+float(index)*rowHeight;
        if(index>0){
            Pen separator(light?Color(95,218,224,231):Color(75,93,103,119),scale*.6f);
            g.DrawLine(&separator,padding,y,width-padding,y);
        }
        RectF cell(padding,y,width-2*padding,rowHeight);
        const auto& name=item.name;
        if(item.value.empty()){
            Text(g,name,labelFont.get(),label,cell,haloOffset,light);
            continue;
        }
        auto value=MeasureValue(g,item.value,valueFont.get(),unitFont.get(),scale,false);
        const float glyph=item.batteryIcon?IconSpace*scale:0;
        const float valueX=cell.GetRight()-value.Total();
        if(item.batteryIcon){
            int n=value.percent?_wtoi(value.digits.c_str()):-1;
            Color status=n>=0&&n<=20?(light?Color(255,189,127,43):Color(255,230,180,106)):(light?Color(255,65,139,120):Color(255,128,194,172));
            if(custom)status=ReadableInk(status,luminance,ink,3.0);
            Battery(g,RectF(cell.X,cell.Y+(rowHeight-7*scale)/2,10*scale,7*scale),n,status,scale);
        }
        Text(g,name,labelFont.get(),label,RectF(cell.X+glyph,cell.Y,std::max(0.f,valueX-cell.X-glyph-NameGap*scale),cell.Height),haloOffset,light);
        if(value.approximate)Text(g,L"约",unitFont.get(),unit,RectF(valueX,cell.Y+.5f*scale,value.prefixWidth+4*scale,cell.Height),haloOffset,light,false);
        Text(g,value.digits,valueFont.get(),number,RectF(valueX+value.prefixWidth,cell.Y,value.digitWidth+4*scale,cell.Height),haloOffset,light,false);
        if(value.percent)Text(g,L"%",unitFont.get(),unit,RectF(valueX+value.prefixWidth+value.digitWidth+1.5f*scale,cell.Y+scale,value.unitWidth+4*scale,cell.Height),haloOffset,light,false);
    }
}

void CapsulePainter::Present(HWND hwnd,int rows,bool light,bool hover,const std::vector<MonitorItem>&items,CapsuleBackground background){
    RECT bounds;GetWindowRect(hwnd,&bounds);int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;if(w<=0||h<=0)return;
    if(bufferWidth!=w||bufferHeight!=h){ReleaseBuffer();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void*bits=nullptr;bufferDc=CreateCompatibleDC(nullptr);bufferBitmap=CreateDIBSection(bufferDc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bufferBitmap){ReleaseBuffer();return;}previousBitmap=SelectObject(bufferDc,bufferBitmap);
        surface=std::make_unique<Bitmap>(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));canvas=std::make_unique<Graphics>(surface.get());bufferWidth=w;bufferHeight=h;}
    UINT dpi=WindowDpi(hwnd);
    canvas->Clear(Color(0,0,0,0));Draw(*canvas,float(w),float(h),rows,light,hover,items,float(dpi)/96,background);canvas->Flush(FlushIntentionSync);
    POINT source{0,0},destination{bounds.left,bounds.top};SIZE size{w,h};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(hwnd,nullptr,&destination,&size,bufferDc,&source,0,&blend,ULW_ALPHA);
}
void CapsulePainter::PresentHoverList(HWND hwnd,bool light,bool hover,std::span<const MonitorItem> items,CapsuleBackground background,bool more){
    RECT bounds{};
    if(!GetWindowRect(hwnd,&bounds))return;
    int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
    if(w<=0||h<=0)return;
    if(hoverBufferWidth!=w||hoverBufferHeight!=h){
        ReleaseHoverBuffer();
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;
        info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;
        info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        hoverBufferDc=CreateCompatibleDC(nullptr);
        hoverBufferBitmap=CreateDIBSection(hoverBufferDc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!hoverBufferBitmap){ReleaseHoverBuffer();return;}
        hoverPreviousBitmap=SelectObject(hoverBufferDc,hoverBufferBitmap);
        hoverSurface=std::make_unique<Bitmap>(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
        hoverCanvas=std::make_unique<Graphics>(hoverSurface.get());
        hoverBufferWidth=w;
        hoverBufferHeight=h;
    }
    UINT dpi=WindowDpi(hwnd);
    hoverCanvas->Clear(Color(0,0,0,0));
    DrawHoverList(*hoverCanvas,float(w),float(h),light,hover,items,float(dpi)/96,background,more);
    hoverCanvas->Flush(FlushIntentionSync);
    POINT source{0,0},destination{bounds.left,bounds.top};
    SIZE size{w,h};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd,nullptr,&destination,&size,hoverBufferDc,&source,0,&blend,ULW_ALPHA);
}

bool CapsulePainter::Preview(const std::wstring&path){
    Bitmap image(1080,420,PixelFormat32bppPARGB);Graphics g(&image);g.Clear(Color(255,231,235,241));
    Font heading(L"Microsoft YaHei UI",17,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,62,72,90));
    g.DrawString(L"2.2 胶囊渲染预览 · 示例数据（非桌面截图）",-1,&heading,PointF(40,20),&ink);
    std::vector<MonitorItem> three{{L"CPU",L"6%"},{L"内存",L"44%"},{L"电脑",L"76%",true}};
    std::vector<MonitorItem> six{{L"CPU",L"12%"},{L"内存",L"44%"},{L"电脑",L"76%",true},{L"G304",L"约30%",true},{L"键盘",L"85%",true},{L"耳机",L"68%",true}};
    for(int row=0;row<2;row++)for(int col=0;col<2;col++){
        float x=40+col*520.f,y=95+row*165.f;bool light=col==0;bool dense=row==1;
        SolidBrush backdrop(light?Color(255,242,243,245):Color(255,24,26,30));g.FillRectangle(&backdrop,x-12,y-12,496.f,120.f);
        const auto& data=dense?six:three;int rows=dense?3:2;
        int width=2*PreferredWidth(44,rows,data);
        auto state=g.Save();g.TranslateTransform(x+(472-width)/2.f,y);Draw(g,float(width),88,rows,light,false,data,2);g.Restore(state);
    }
    return SavePng(image,path);
}
bool CapsulePainter::DpiPreview(const std::wstring&path){
    Bitmap image(1140,720,PixelFormat32bppPARGB);Graphics g(&image);g.Clear(Color(255,231,235,241));
    Font heading(L"Microsoft YaHei UI",18,FontStyleRegular,UnitPixel),caption(L"Microsoft YaHei UI",14,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,62,72,90));
    g.DrawString(L"跨屏缩放预览 · 示例数据（非桌面截图）",-1,&heading,PointF(30,18),&ink);
    
    std::vector<MonitorItem> example{{L"CPU",L"29%"},{L"内存",L"81%"},{L"上传",L"28 KB/s",false,L"↑",true},{L"下载",L"1.2 MB/s",false,L"↓",true},{L"办公室键盘",L"100%",true},{L"鼠标",L"约90%",true}};
    int row=0;for(UINT dpi:{96u,120u,144u,192u}){
        float y=70+row++*160.f;
        for(int col=0;col<2;col++){
            float x=30+col*550.f;bool light=col==0;
            auto label=std::to_wstring(MulDiv(int(dpi),100,96))+L"% · "+std::to_wstring(dpi)+L" DPI · "+(light?L"两行":L"三行");
            g.DrawString(label.c_str(),-1,&caption,PointF(x,y),&ink);
            SolidBrush backdrop(light?Color(255,242,243,245):Color(255,24,26,30));g.FillRectangle(&backdrop,x,y+26,520.f,106.f);
            auto state=g.Save();g.TranslateTransform(x+10,y+35);Draw(g,float(DpiPixels(PreferredWidth(44,light?2:3,example),dpi)),float(DpiPixels(44,dpi)),light?2:3,light,false,example,float(dpi)/96);g.Restore(state);
        }
    }
    return SavePng(image,path);
}
