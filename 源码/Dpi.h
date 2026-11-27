#pragma once
#include <windows.h>

inline UINT WindowDpi(HWND window){
    using GetDpi=UINT(WINAPI*)(HWND);
    static auto getDpi=reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    UINT dpi=getDpi?getDpi(window):0;
    if(dpi)return dpi;
    HDC dc=GetDC(window);dpi=dc?GetDeviceCaps(dc,LOGPIXELSX):96;if(dc)ReleaseDC(window,dc);
    return dpi?dpi:96;
}
inline int DpiPixels(int value,UINT dpi){return MulDiv(value,int(dpi),96);}
inline int DpiLogical(int value,HWND window){return MulDiv(value,96,int(WindowDpi(window)));}
