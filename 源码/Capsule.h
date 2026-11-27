#pragma once
#include <windows.h>
#include "MonitorItem.h"
#include <gdiplus.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

enum class BackgroundMode { System, Transparent, Custom };
struct CapsuleBackground {
    BackgroundMode mode = BackgroundMode::System;
    COLORREF color = RGB(46, 61, 78);
};

// One software renderer is shared by the live capsule and exported previews.
// No desktop screenshots, graphics drivers, or browser runtime are involved.
class CapsulePainter {
    ULONG_PTR token=0;
    float fontSize=0;
    std::unique_ptr<Gdiplus::Font> labelFont,valueFont,unitFont;
    HDC bufferDc=nullptr;HBITMAP bufferBitmap=nullptr;HGDIOBJ previousBitmap=nullptr;
    int bufferWidth=0,bufferHeight=0;
    std::unique_ptr<Gdiplus::Bitmap> surface;
    std::unique_ptr<Gdiplus::Graphics> canvas;
    HDC hoverBufferDc=nullptr;HBITMAP hoverBufferBitmap=nullptr;HGDIOBJ hoverPreviousBitmap=nullptr;
    int hoverBufferWidth=0,hoverBufferHeight=0;
    std::unique_ptr<Gdiplus::Bitmap> hoverSurface;
    std::unique_ptr<Gdiplus::Graphics> hoverCanvas;
    void ReleaseHoverBuffer();
    std::vector<MonitorItem> widthItems;
    int measuredRows=0, measuredHeight=0, measuredWidth=0;
    void ReleaseBuffer();
    void Fonts(float size);
    void DrawHoverList(Gdiplus::Graphics&,float width,float height,bool light,bool hover,
                       std::span<const MonitorItem> items,float scale,CapsuleBackground background,bool more);
public:
    CapsulePainter();
    ~CapsulePainter();
    CapsulePainter(const CapsulePainter&)=delete;
    int PreferredWidth(int logicalHeight,int rows,const std::vector<MonitorItem>& items);
    SIZE HoverListSize(const std::vector<MonitorItem>& items);
    void Draw(Gdiplus::Graphics&,float width,float height,int rows,bool light,bool hover,
              const std::vector<MonitorItem>& items,float scale=1,CapsuleBackground background={});
    void Present(HWND,int rows,bool light,bool hover,const std::vector<MonitorItem>&,CapsuleBackground background);
    void PresentHoverList(HWND,bool light,bool hover,std::span<const MonitorItem>,CapsuleBackground background,bool more);
    void DiscardHoverSurface(){ReleaseHoverBuffer();}
    bool Preview(const std::wstring& path);
    bool DpiPreview(const std::wstring& path);
};
