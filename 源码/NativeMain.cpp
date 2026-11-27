#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "Devices.h"
#include "DeviceNames.h"
#include "Capsule.h"
#include "Dpi.h"
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <imm.h>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <cmath>

namespace {
constexpr wchar_t CapsuleClass[]=L"BlueBatteryCapsuleNativeV2";
constexpr wchar_t HoverListClass[]=L"BlueBatteryHoverListNativeV2";
constexpr wchar_t MainClass[]=L"BlueBatteryDetailNativeV2";
constexpr UINT Changed=WM_APP+10,TrayMessage=WM_APP+11,RaiseCapsule=WM_APP+12;
constexpr int PanelActionsTop=116, PanelActionsBottom=143, DeviceListTop=147;
constexpr UINT Open=100,Refresh=101,All=102,Pin=103,HidePanel=104,Exit=105,Cpu=110,Memory=111,Battery=112,Rows2=113,Rows3=114,Notice=115,Startup=116,Diagnostic=117,Bluetooth=118,Collapse=119,Opacity100=120,Opacity90=121,Opacity75=122,Help=123,CapsuleTaskbar=124,CapsuleDesktop=125,CapsuleReset=126,ComputerBattery=127;
constexpr UINT BackgroundSystem=128, BackgroundTransparent=129, BackgroundCustom=130, Upload=131, Download=132;
std::wstring MemoryText(ULONGLONG bytes){wchar_t text[32];swprintf(text,32,L"%.1f",double(bytes)/1048576);return text;}
std::wstring IntervalText(int seconds){if(seconds%3600==0)return std::to_wstring(seconds/3600)+L" 小时";if(seconds%60==0)return std::to_wstring(seconds/60)+L" 分钟";return std::to_wstring(seconds)+L" 秒";}
std::wstring Folder(){wchar_t path[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);std::wstring dir=std::wstring(path)+L"\\BlueBatteryFloat";CreateDirectoryW(dir.c_str(),nullptr);return dir;}
std::wstring Executable(){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);return p;}
std::wstring Beside(const std::wstring& name){auto p=Executable();return p.substr(0,p.find_last_of(L"\\/")+1)+name;}
std::wstring ReadText(const std::wstring&path){HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return L"";DWORD n=GetFileSize(f,nullptr);if(n>1024*1024){CloseHandle(f);return L"";}std::string s(n,0);DWORD read;ReadFile(f,s.data(),n,&read,nullptr);CloseHandle(f);int count=MultiByteToWideChar(CP_UTF8,0,s.data(),read,nullptr,0);std::wstring text(count,0);MultiByteToWideChar(CP_UTF8,0,s.data(),read,text.data(),count);return text;}
struct Settings {
    CapsuleBackground background;
    bool upload=true,download=true;
    bool cpu=true,memory=true,battery=true,computerBattery=true,all=false,pin=true,notice=true,collapsed=false,desktop=false;
    int rows=2,interval=15,opacity=100,left=INT_MIN,top=INT_MIN,capsuleLeft=INT_MIN,capsuleTop=INT_MIN;
    std::wstring path=Folder()+L"\\native.ini";
    static bool ValidInterval(int n){return n==5||n==15||n==30||n==60||n==3600||n==14400;}
    void Load(){
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){
            auto xml=ReadText(Folder()+L"\\settings.xml");
            auto value=[&](const wchar_t*name)->std::wstring{std::wstring a=L"<"+std::wstring(name)+L">",b=L"</"+std::wstring(name)+L">";auto n=xml.find(a);if(n==std::wstring::npos)return L"";n+=a.size();auto end=xml.find(b,n);return end==std::wstring::npos?L"":xml.substr(n,end-n);};
            auto boolean=[&](const wchar_t*n,bool&v){auto t=value(n);if(t==L"true")v=true;else if(t==L"false")v=false;};
            boolean(L"TaskbarShowCpu",cpu);boolean(L"TaskbarShowMemory",memory);boolean(L"TaskbarShowBattery",battery);boolean(L"ComputerBattery",computerBattery);boolean(L"ShowAll",all);boolean(L"Topmost",pin);boolean(L"LowBatteryNotice",notice);boolean(L"Collapsed",collapsed);boolean(L"DesktopMode",desktop);
            rows=value(L"TaskbarRows")==L"3"?3:2;int n=_wtoi(value(L"RefreshSeconds").c_str());if(ValidInterval(n))interval=n;
            auto alpha=value(L"Opacity");if(!alpha.empty()){double a=wcstod(alpha.c_str(),nullptr);opacity=a<=0.8?75:a<=0.95?90:100;}return;
        }
        auto get=[&](const wchar_t*n,int fallback){return int(GetPrivateProfileIntW(L"Monitor",n,fallback,path.c_str()));};
        const int mode = get(L"CapsuleBackgroundMode", int(BackgroundMode::System));
        if (mode == int(BackgroundMode::Custom) || mode == int(BackgroundMode::Transparent))
            background.mode = static_cast<BackgroundMode>(mode);
        const int color = get(L"CapsuleBackgroundColor", int(background.color));
        if (color >= 0 && color <= 0xffffff) background.color = COLORREF(color);
        upload=get(L"Upload",1)!=0; download=get(L"Download",1)!=0;
        cpu=get(L"Cpu",1)!=0;memory=get(L"Memory",1)!=0;battery=get(L"Battery",1)!=0;computerBattery=get(L"ComputerBattery",1)!=0;all=get(L"All",0)!=0;pin=get(L"Pin",1)!=0;notice=get(L"Notice",1)!=0;collapsed=get(L"Collapsed",0)!=0;desktop=get(L"DesktopMode",0)!=0;
        rows=get(L"Rows",2)==3?3:2;int n=get(L"Interval",15);interval=ValidInterval(n)?n:15;opacity=get(L"Opacity",100);if(opacity!=75&&opacity!=90)opacity=100;left=get(L"Left",INT_MIN);top=get(L"Top",INT_MIN);capsuleLeft=get(L"CapsuleLeft",INT_MIN);capsuleTop=get(L"CapsuleTop",INT_MIN);
    }
    void Save()const{
        auto put=[&](const wchar_t*n,int v){auto s=std::to_wstring(v);WritePrivateProfileStringW(L"Monitor",n,s.c_str(),path.c_str());};
        put(L"CapsuleBackgroundMode",int(background.mode));
        put(L"CapsuleBackgroundColor",int(background.color));
        put(L"Upload",upload); put(L"Download",download);
        put(L"Cpu",cpu);put(L"Memory",memory);put(L"Battery",battery);put(L"ComputerBattery",computerBattery);put(L"All",all);put(L"Pin",pin);put(L"Notice",notice);put(L"Collapsed",collapsed);put(L"DesktopMode",desktop);put(L"Rows",rows);put(L"Interval",interval);put(L"Opacity",opacity);put(L"Left",left);put(L"Top",top);put(L"CapsuleLeft",capsuleLeft);put(L"CapsuleTop",capsuleTop);
    }
};
bool StartupEnabled(){wchar_t buffer[32768]{};DWORD n=sizeof buffer;if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"BlueBatteryFloat",RRF_RT_REG_SZ,nullptr,buffer,&n)!=ERROR_SUCCESS)return false;return _wcsicmp(buffer,(L"\""+Executable()+L"\" --startup").c_str())==0;}
void SetStartup(bool enabled){HKEY key=nullptr;LONG error=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(error==ERROR_SUCCESS){if(enabled){auto command=L"\""+Executable()+L"\" --startup";error=RegSetValueExW(key,L"BlueBatteryFloat",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),DWORD((command.size()+1)*2));}else{error=RegDeleteValueW(key,L"BlueBatteryFloat");if(error==ERROR_FILE_NOT_FOUND)error=ERROR_SUCCESS;}RegCloseKey(key);}if(error!=ERROR_SUCCESS)MessageBoxW(nullptr,L"无法更改开机自启动设置",L"设备监控",MB_OK|MB_ICONWARNING);}
ULONGLONG FileNow(){FILETIME t;GetSystemTimeAsFileTime(&t);return(ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
void Fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void Draw(HDC dc,const std::wstring& s,RECT r,COLORREF color,HFONT font,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS){auto old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,s.c_str(),int(s.size()),&r,flags|DT_NOPREFIX);SelectObject(dc,old);}
HFONT MakeFont(int height,bool bold=false){return CreateFontW(-height,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");}

struct App;
App* live=nullptr;
struct App {
    Settings settings;
    HWND capsule=nullptr,panel=nullptr,tooltip=nullptr,hoverList=nullptr;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    CapsulePainter capsulePainter;
    HICON icon=nullptr;HFONT small=nullptr,normal=nullptr,big=nullptr;
    int taskWidth=236,scroll=0;
    int firstHoverRow=0,visibleHoverRows=0,wheelRemainder=0;
    bool tooltipEnabled=true;
    bool lightTaskbar=false,hoverCapsule=false,hoverPopup=false,hoverListVisible=false,draggingCapsule=false,movingCapsule=false,taskbarPlaced=false,hasLastTaskbarRect=false;
    RECT lastTaskbarRect{};
    std::atomic<bool> raisePending{false};
    POINT dragStartCursor{},dragStartWindow{};
    UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    HWINEVENTHOOK foregroundHook=nullptr,reorderHook=nullptr;
    NOTIFYICONDATAW tray{};
    Resources resources;
    std::vector<Device> devices;
    std::vector<MonitorItem> items;
    std::wstring aliasStore = DeviceNames::Store;
    std::wstring tooltipText,scanStatus=L"正在读取设备…",lastTrayText;
    std::map<std::wstring,ULONGLONG> lowNotices;
    std::atomic<bool> stopping{false};std::atomic<int> scanInterval{15};
    std::mutex gate;std::condition_variable wake;bool requestScan=true;
    std::vector<Device> nextDevices;std::wstring nextStatus,diagnostic;
    std::thread worker;
    bool test=false,scanning=false;
    explicit App(bool testing=false):test(testing){live=this;settings.Load();scanInterval=settings.interval;}
    ~App(){
        stopping=true;wake.notify_all();if(worker.joinable())worker.join();
        if(foregroundHook)UnhookWinEvent(foregroundHook);if(reorderHook)UnhookWinEvent(reorderHook);
        if(tray.hWnd)Shell_NotifyIconW(NIM_DELETE,&tray);
        if(tooltip&&IsWindow(tooltip))DestroyWindow(tooltip);if(hoverList&&IsWindow(hoverList))DestroyWindow(hoverList);if(panel&&IsWindow(panel))DestroyWindow(panel);if(capsule&&IsWindow(capsule))DestroyWindow(capsule);
        for(auto f:{small,normal,big})if(f)DeleteObject(f);if(icon)DestroyIcon(icon);live=nullptr;
    }
    static LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l){return live?live->Message(h,m,w,l):DefWindowProcW(h,m,w,l);}
    static void CALLBACK ShellEvent(HWINEVENTHOOK,DWORD,HWND hwnd,LONG object,LONG child,DWORD,DWORD){if(live&&object==0&&child==0&&hwnd==FindWindowW(L"Shell_TrayWnd",nullptr)&&!live->raisePending.exchange(true))if(!PostMessageW(live->capsule,RaiseCapsule,0,0))live->raisePending=false;}
    // WS_EX_TOPMOST requests the topmost extended style, but Explorer can
    // later insert the taskbar above an existing popup.  Only repair that
    // exceptional ordering; normal refreshes continue to use SWP_NOZORDER.
    void EnsureCapsuleTopmost(){
        if(!capsule||!IsWindow(capsule))return;
        // Keep this flag set minimal.  On some Windows 11 builds,
        // SWP_NOOWNERZORDER/SWP_NOSENDCHANGING prevents the HWND_TOPMOST
        // insertion and leaves Shell_TrayWnd above the capsule.
        SetWindowPos(capsule,HWND_TOPMOST,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    bool TaskbarAboveCapsule(){
        if(!capsule||!IsWindow(capsule))return false;
        HWND taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
        if(!taskbar)return false;
        for(HWND h=GetWindow(capsule,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV))
            if(h==taskbar)return true;
        return false;
    }
    void EnsureCapsuleTopmostIfCovered(){
        // Explorer may promote the taskbar above other topmost popups when it
        // receives a click or opens the hidden-icons flyout.  Repair only that
        // exceptional ordering; do not touch Z-order during normal refreshes.
        if(TaskbarAboveCapsule())EnsureCapsuleTopmost();
    }
    void Save(){if(!test)settings.Save();}
    void Create(){
        INITCOMMONCONTROLSEX cc{sizeof cc,ICC_WIN95_CLASSES};InitCommonControlsEx(&cc);
        icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,0,0,LR_DEFAULTSIZE));
        if(!icon)icon=static_cast<HICON>(LoadImageW(nullptr,Beside(L"app.ico").c_str(),IMAGE_ICON,0,0,LR_LOADFROMFILE|LR_DEFAULTSIZE));
        if(!icon)icon=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));
        WNDCLASSEXW c{sizeof c};c.style=CS_DBLCLKS;c.lpfnWndProc=WindowProc;c.hInstance=instance;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=icon;c.hIconSm=icon;c.lpszClassName=CapsuleClass;RegisterClassExW(&c);c.lpszClassName=HoverListClass;RegisterClassExW(&c);c.lpszClassName=MainClass;RegisterClassExW(&c);
        small=MakeFont(12);normal=MakeFont(14);big=MakeFont(21,true);
        capsule=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_LAYERED,CapsuleClass,L"设备电量悬浮窗 2.2.0.16 原生版",WS_POPUP,0,0,taskWidth,44,nullptr,nullptr,instance,nullptr);
        if(!capsule)throw std::runtime_error("Cannot create taskbar window");
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,capsule,nullptr,instance,nullptr);
        TOOLINFOW info{sizeof info};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=capsule;info.uId=reinterpret_cast<UINT_PTR>(capsule);info.lpszText=const_cast<wchar_t*>(L"双击打开监控界面 · 右键配置");SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,600);
        if(!test){
            AddTray();
            SetTimer(capsule,1,1000,nullptr);
            SetTimer(capsule,2,250,nullptr);
            // These hooks only post a repair message when Explorer reports a
            // taskbar reorder.  Raise() checks the relationship again before
            // calling SetWindowPos, so the event burst does not cause a
            // repeated Z-order loop.
            foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,ShellEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
            reorderHook=SetWinEventHook(EVENT_OBJECT_REORDER,EVENT_OBJECT_REORDER,nullptr,ShellEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        }
        Tick();ShowWindow(capsule,SW_SHOWNOACTIVATE);EnsureCapsuleTopmost();Position();
    }
    void AddTray(){tray={sizeof tray};tray.hWnd=capsule;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;tray.uCallbackMessage=TrayMessage;tray.hIcon=icon;wcscpy_s(tray.szTip,L"设备电量悬浮窗 · 原生轻量版");Shell_NotifyIconW(NIM_ADD,&tray);}
    void StartWorker(){worker=std::thread([this]{
        try{Devices backend(stopping);while(!stopping){
            {std::lock_guard<std::mutex> lock(gate);requestScan=false;}auto begin=GetTickCount64();auto list=backend.Scan();auto report=backend.Diagnostic(list);
            {std::lock_guard<std::mutex> lock(gate);nextDevices=std::move(list);diagnostic=std::move(report);nextStatus=L"已检查设备 · 每 "+IntervalText(scanInterval.load());}
            if(stopping)break;PostMessageW(capsule,Changed,0,0);
            std::unique_lock<std::mutex> lock(gate);wake.wait_for(lock,std::chrono::milliseconds(std::max<LONGLONG>(1000,LONGLONG(scanInterval.load())*1000-LONGLONG(GetTickCount64()-begin))),[this]{return stopping||requestScan;});
        }}catch(...){if(!stopping){std::lock_guard<std::mutex> lock(gate);nextStatus=L"设备接口暂不可用，请刷新重试";PostMessageW(capsule,Changed,0,0);}}});}
    void Request(){if(test)return;{std::lock_guard<std::mutex> lock(gate);requestScan=true;}wake.notify_all();scanStatus=L"正在刷新…";if(panel)InvalidateRect(panel,nullptr,FALSE);}
    void CapsuleBounds(int x,int y,int width,int height){
        RECT old{};GetWindowRect(capsule,&old);
        if(old.left==x&&old.top==y&&old.right-old.left==width&&old.bottom-old.top==height)return;
        movingCapsule=true;
        // The window is created with WS_EX_TOPMOST. Preserve that band without
        // issuing another z-order change every time the taskbar is refreshed.
        SetWindowPos(capsule,nullptr,x,y,width,height,SWP_NOACTIVATE|SWP_NOZORDER);
        movingCapsule=false;InvalidateRect(capsule,nullptr,FALSE);if(hoverListVisible)PositionHoverList();
    }
    void DesktopBounds(int x,int y,bool constrain){
        // The first move can change the window's DPI. Recheck the destination
        // before returning, keeping one coordinate space for sizing and input.
        for(int pass=0;pass<3;pass++){
            UINT dpi=WindowDpi(capsule);int width=DpiPixels(taskWidth,dpi),height=DpiPixels(44,dpi);
            if(constrain){
                RECT desired{x,y,x+width,y+height};MONITORINFO mi{sizeof mi};
                if(GetMonitorInfoW(MonitorFromRect(&desired,MONITOR_DEFAULTTONEAREST),&mi)){
                    int margin=DpiPixels(4,dpi);
                    x=std::clamp(x,int(mi.rcWork.left)+margin,std::max(int(mi.rcWork.left)+margin,int(mi.rcWork.right)-width-margin));
                    y=std::clamp(y,int(mi.rcWork.top)+margin,std::max(int(mi.rcWork.top)+margin,int(mi.rcWork.bottom)-height-margin));
                }
            }
            CapsuleBounds(x,y,width,height);
            if(WindowDpi(capsule)==dpi)break;
        }
        if(constrain){RECT r{};GetWindowRect(capsule,&r);settings.capsuleLeft=r.left;settings.capsuleTop=r.top;}
    }
    void KeepTaskbarCapsuleVisible(){
        // Explorer can briefly destroy/recreate Shell_TrayWnd and TrayNotifyWnd
        // while the taskbar is being refreshed. Keep the capsule on its last
        // valid screen position instead of hiding it during that transition.
        UINT dpi=WindowDpi(capsule);
        int width=DpiPixels(taskWidth,dpi),height=DpiPixels(44,dpi);
        RECT current{};
        bool valid=GetWindowRect(capsule,&current)&&current.right>current.left&&current.bottom>current.top;
        int x=0,y=0;
        if(hasLastTaskbarRect&&lastTaskbarRect.right>lastTaskbarRect.left&&lastTaskbarRect.bottom>lastTaskbarRect.top){
            x=lastTaskbarRect.left;y=lastTaskbarRect.top;
        }else if(valid){
            x=current.left;y=current.top;
        }else{
            RECT work{};
            if(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)){
                x=work.right-width-DpiPixels(24,dpi);y=work.bottom-height-DpiPixels(4,dpi);
                x=std::max(int(work.left),x);y=std::max(int(work.top),y);
            }
        }
        CapsuleBounds(x,y,width,height);
        if(!IsWindowVisible(capsule))ShowWindow(capsule,SW_SHOWNOACTIVATE);
    }
    bool HoverListNeeded() const { return items.size()>size_t(std::max(1,settings.rows)*2); }
    bool CursorInsideHover() const {
        POINT point{};
        if(!GetCursorPos(&point))return false;
        RECT rect{};
        if(capsule&&GetWindowRect(capsule,&rect)&&PtInRect(&rect,point))return true;
        if(hoverListVisible&&hoverList&&GetWindowRect(hoverList,&rect)&&PtInRect(&rect,point))return true;
        return false;
    }
    void HideHoverList(){
        KillTimer(capsule,3);
        firstHoverRow=wheelRemainder=0;
        hoverPopup=false;
        hoverListVisible=false;
        if(hoverList&&IsWindowVisible(hoverList))ShowWindow(hoverList,SW_HIDE);
        capsulePainter.DiscardHoverSurface();
    }
    void PositionHoverList(){
        if(!hoverList||!IsWindow(hoverList)||!HoverListNeeded())return;
        RECT capsuleRect{};
        if(!GetWindowRect(capsule,&capsuleRect))return;
        MONITORINFO monitor{sizeof monitor};
        if(!GetMonitorInfoW(MonitorFromRect(&capsuleRect,MONITOR_DEFAULTTONEAREST),&monitor))return;
        UINT dpi=WindowDpi(capsule);
        SIZE logical=capsulePainter.HoverListSize(items);
        const int gap=DpiPixels(4,dpi);
        int above=std::max<int>(0,std::min(capsuleRect.top,monitor.rcWork.bottom)-gap-monitor.rcWork.top);
        int below=std::max<int>(0,monitor.rcWork.bottom-std::max(capsuleRect.bottom,monitor.rcWork.top)-gap);
        int room=std::max(above,below);
        int count=int(items.size());
        bool more=count>16 || DpiPixels(logical.cy,dpi)>room;
        int roomDip=MulDiv(room,96,int(dpi));
        visibleHoverRows=std::clamp((roomDip-24-(more?22:0))/28,1,std::min(count,16));
        firstHoverRow=std::clamp(firstHoverRow,0,count-visibleHoverRows);
        int width=std::min<int>(DpiPixels(logical.cx,dpi),monitor.rcWork.right-monitor.rcWork.left);
        int height=std::min(room,DpiPixels(24+visibleHoverRows*28+(visibleHoverRows<count?22:0),dpi));
        int x=std::clamp<int>(capsuleRect.left,monitor.rcWork.left,monitor.rcWork.right-width);
        int y=above>=height?monitor.rcWork.top+above-height:monitor.rcWork.bottom-below;
        RECT bounds{x,y,x+width,y+height},old{};
        if(!GetWindowRect(hoverList,&old)||!EqualRect(&old,&bounds))
            SetWindowPos(hoverList,nullptr,x,y,width,height,SWP_NOACTIVATE|SWP_NOZORDER);
    }
    bool HoverScrollable() const {return visibleHoverRows>0 && visibleHoverRows<int(items.size());}
    void ScrollHover(int delta,bool wrap=false){
        if(!HoverScrollable())return;
        int last=int(items.size())-visibleHoverRows;
        firstHoverRow=wrap && firstHoverRow==last ? 0 : std::clamp(firstHoverRow+delta,0,last);
        InvalidateRect(hoverList,nullptr,FALSE);
    }
    void DrawHover(){
        auto page=std::span<const MonitorItem>(items).subspan(firstHoverRow,visibleHoverRows);
        capsulePainter.PresentHoverList(hoverList,lightTaskbar,hoverPopup,page,settings.background,HoverScrollable());
    }
    void EnsureHoverList(){
        if(hoverList&&IsWindow(hoverList))return;
        hoverList=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_LAYERED,HoverListClass,L"",WS_POPUP,0,0,1,1,capsule,nullptr,instance,nullptr);
        
    }
    void ShowHoverList(){
        if(!HoverListNeeded())return;
        EnsureHoverList();
        if(!hoverList)return;
        PositionHoverList();
        hoverListVisible=true;
        hoverPopup=false;
        KillTimer(capsule,3);
        if(tooltip&&IsWindow(tooltip))SendMessageW(tooltip,TTM_POP,0,0);
        DrawHover();
        ShowWindow(hoverList,SW_SHOWNOACTIVATE);
    }
    void ScheduleHoverHide(){
        if(hoverCapsule||hoverPopup)return;
        if(hoverListVisible)SetTimer(capsule,3,260,nullptr);
    }
    void HoverListChanged(){
        if(!hoverListVisible)return;
        if(!HoverListNeeded()){HideHoverList();return;}
        PositionHoverList();
        InvalidateRect(hoverList,nullptr,FALSE);
    }

    bool TaskListRect(HWND taskbar, RECT& rect){
        HWND taskList=FindWindowExW(taskbar,nullptr,L"MSTaskListWClass",nullptr);
        return taskList&&GetWindowRect(taskList,&rect)&&rect.right>rect.left&&rect.bottom>rect.top;
    }
    void Position(){
        if(draggingCapsule||movingCapsule)return;
        if(settings.desktop){
            taskWidth=capsulePainter.PreferredWidth(44,settings.rows,items);
            int x=settings.capsuleLeft,y=settings.capsuleTop;
            if(x==INT_MIN||y==INT_MIN){RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);UINT dpi=WindowDpi(capsule);x=work.right-DpiPixels(taskWidth+24,dpi);y=work.bottom-DpiPixels(60,dpi);}
            DesktopBounds(x,y,true);
            if(!IsWindowVisible(capsule))ShowWindow(capsule,SW_SHOWNOACTIVATE);
            return;
        }
        HWND tb=FindWindowW(L"Shell_TrayWnd",nullptr);if(!tb){KeepTaskbarCapsuleVisible();return;}RECT r{};if(!GetWindowRect(tb,&r)||r.right<=r.left||r.bottom<=r.top){KeepTaskbarCapsuleVisible();return;}
        MONITORINFO mi{sizeof mi};if(!GetMonitorInfoW(MonitorFromWindow(tb,MONITOR_DEFAULTTONEAREST),&mi)){KeepTaskbarCapsuleVisible();return;}
        HWND notify=FindWindowExW(tb,nullptr,L"TrayNotifyWnd",nullptr);RECT nr{};bool has=notify!=nullptr;
        // TrayNotifyWnd is briefly hidden/recreated while the taskbar opens
        // the hidden-icons flyout.  Do not move the capsule to the arbitrary
        // fallback slot during that transition; keep its last valid position
        // so it cannot appear to disappear.
        if(has&&!GetWindowRect(notify,&nr)){if(taskbarPlaced||hasLastTaskbarRect){KeepTaskbarCapsuleVisible();return;}has=false;}
        UINT dpi=WindowDpi(tb);int margin=DpiPixels(2,dpi),gap=DpiPixels(8,dpi);
        bool horizontal=r.right-r.left>=r.bottom-r.top;int h=std::max<int>(DpiPixels(24,dpi),(horizontal?r.bottom-r.top:r.right-r.left)-margin*2);
        if(horizontal)taskWidth=capsulePainter.PreferredWidth(MulDiv(h,96,int(dpi)),settings.rows,items);
        RECT taskList{};bool hasTaskList=TaskListRect(tb,taskList);
        int width=DpiPixels(taskWidth,dpi),x=0,y=0;
        if(has&&(nr.right<=nr.left||nr.bottom<=nr.top||nr.left<r.left||nr.right>r.right||nr.top<r.top||nr.bottom>r.bottom)){
            if(taskbarPlaced||hasLastTaskbarRect){KeepTaskbarCapsuleVisible();return;}
            has=false;
        }
        if(horizontal){
            int leftEdge=r.left+margin*2;
            if(hasTaskList)leftEdge=std::max(leftEdge,int(taskList.right)+gap);
            int rightEdge=has?nr.left-gap:r.right-margin*2;
            if(rightEdge-leftEdge>=width){
                x=rightEdge-width;y=r.top+margin;
            }else{
                // A busy taskbar may have no safe horizontal gap. Keep the
                // capsule adjacent to the taskbar instead of covering a task button.
                x=std::clamp(r.right-width-margin,r.left+margin*2,std::max(r.left+margin*2,r.right-width-margin*2));
                bool bottomTaskbar=r.top>=mi.rcWork.bottom-DpiPixels(2,dpi);
                y=bottomTaskbar?r.top-h-gap:r.bottom+gap;
            }
        }else{
            x=r.left+margin;width=std::max<int>(DpiPixels(24,dpi),r.right-r.left-margin*2);h=DpiPixels(68,dpi);
            int topEdge=r.top+margin*2;
            if(hasTaskList)topEdge=std::max(topEdge,int(taskList.bottom)+gap);
            int bottomEdge=has?nr.top-gap:r.bottom-margin*2;
            if(bottomEdge-topEdge>=h)y=bottomEdge-h;
            else{
                bool rightTaskbar=r.left>=mi.rcWork.right-DpiPixels(2,dpi);
                x=rightTaskbar?r.left-width-gap:r.right+gap;y=r.top+margin;
            }
        }
        CapsuleBounds(x,y,width,h);
        if(!IsWindowVisible(capsule))ShowWindow(capsule,SW_SHOWNOACTIVATE);
        GetWindowRect(capsule,&lastTaskbarRect);hasLastTaskbarRect=true;
        taskbarPlaced=true;
    }
    void EndCapsuleDrag(){
        if(!draggingCapsule)return;
        draggingCapsule=false;RECT r{};
        if(GetWindowRect(capsule,&r)){settings.capsuleLeft=r.left;settings.capsuleTop=r.top;}
        if(GetCapture()==capsule)ReleaseCapture();
        Position();Save();
    }
    void Tick(){
        DWORD theme=0,themeSize=sizeof theme;RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&theme,&themeSize);bool light=theme!=0;if(lightTaskbar!=light){lightTaskbar=light;InvalidateRect(capsule,nullptr,FALSE);}
        resources=ReadResources();auto next=MonitorItems(resources,devices,settings.cpu,settings.memory,settings.battery,settings.computerBattery,settings.upload,settings.download);
        if(next!=items){items=std::move(next);InvalidateRect(capsule,nullptr,FALSE);}
        bool enableTooltip=!HoverListNeeded();
        if(enableTooltip!=tooltipEnabled){
            tooltipEnabled=enableTooltip;
            SendMessageW(tooltip,TTM_ACTIVATE,enableTooltip,0);
            if(!enableTooltip && hoverCapsule && !draggingCapsule)ShowHoverList();
        }
        tooltipText.clear();for(auto&s:items)tooltipText+=s.Text()+L"\n";tooltipText+=L"内存 "+FormatMemory(resources.used)+L" / "+FormatMemory(resources.total)+L"\n";
        if(resources.computerCharging)tooltipText+=L"电脑电源：正在充电\n";else if(resources.computerAc)tooltipText+=L"电脑电源：交流电\n";else if(resources.computerBatteryPresent)tooltipText+=L"电脑电源：电池供电\n";else tooltipText+=L"电脑电源：无电池\n";
        tooltipText+=L"双击打开监控界面 · 右键配置";
        TOOLINFOW info{sizeof info};info.hwnd=capsule;info.uId=reinterpret_cast<UINT_PTR>(capsule);info.lpszText=tooltipText.data();SendMessageW(tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&info));
        std::wstring computerSummary=resources.computerBattery>=0?std::to_wstring(resources.computerBattery)+L"%":resources.computerBatteryPresent?L"—":L"无电池";
        auto summary=L"CPU "+std::to_wstring(std::max(0,resources.cpu))+L"% · 内存 "+std::to_wstring(std::max(0,resources.memory))+L"% · 电脑 "+computerSummary+L" · 本应用 "+MemoryText(resources.privateWorkingSet)+L" MB";
        if(!test&&summary!=lastTrayText){lastTrayText=summary;tray.uFlags=NIF_TIP;wcsncpy_s(tray.szTip,summary.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&tray);}
        Position();HoverListChanged();if(panel&&IsWindowVisible(panel))InvalidateRect(panel,nullptr,FALSE);
    }
    void ShowPanel(){
        HideHoverList();
        if(!panel){
            UINT dpi=WindowDpi(capsule);int width=DpiPixels(390,dpi),height=DpiPixels(540,dpi);
            RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int x=settings.left==INT_MIN?work.right-width-DpiPixels(24,dpi):settings.left,y=settings.top==INT_MIN?work.top+DpiPixels(60,dpi):settings.top;
            RECT wanted{x,y,x+width,y+height};MONITORINFO mi{sizeof mi};GetMonitorInfoW(MonitorFromRect(&wanted,MONITOR_DEFAULTTONEAREST),&mi);x=std::clamp<int>(x,mi.rcWork.left,std::max(mi.rcWork.left,mi.rcWork.right-width));y=std::clamp<int>(y,mi.rcWork.top,std::max(mi.rcWork.top,mi.rcWork.bottom-DpiPixels(140,dpi)));
            panel=CreateWindowExW(WS_EX_LAYERED,MainClass,L"设备电量与系统监控 · 2.2 原生轻量版",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_THICKFRAME,x,y,width,height,nullptr,nullptr,instance,nullptr);
            UINT actual=WindowDpi(panel);
            if(actual!=dpi){width=DpiPixels(390,actual);height=DpiPixels(540,actual);if(settings.left==INT_MIN)x=work.right-width-DpiPixels(24,actual);if(settings.top==INT_MIN)y=work.top+DpiPixels(60,actual);x=std::clamp<int>(x,mi.rcWork.left,std::max(mi.rcWork.left,mi.rcWork.right-width));y=std::clamp<int>(y,mi.rcWork.top,std::max(mi.rcWork.top,mi.rcWork.bottom-DpiPixels(140,actual)));SetWindowPos(panel,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);}
            SetLayeredWindowAttributes(panel,0,BYTE(settings.opacity*255/100),LWA_ALPHA);
        }
        settings.collapsed=false;SetWindowPos(panel,settings.pin?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);ShowWindow(panel,SW_RESTORE);SetForegroundWindow(panel);InvalidateRect(panel,nullptr,FALSE);
    }
    void RememberPosition(){if(panel&&IsWindow(panel)&&!IsIconic(panel)){RECT r;GetWindowRect(panel,&r);settings.left=r.left;settings.top=r.top;}Save();}
    void EditName(HWND owner, DeviceNames::Edit edit) {
        auto result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_RENAME), owner, DeviceNames::Dialog,
                                      reinterpret_cast<LPARAM>(&edit));
        if (result == -1) MessageBoxW(owner,L"无法打开改名窗口。",L"设备改名",MB_OK|MB_ICONERROR);
        if (result != IDOK) return;
        for (auto& d : devices) if (d.key == edit.key) d.alias = edit.alias;
        Tick();
    }
    void Menu(HWND owner){
        HideHoverList();
        HMENU menu=CreatePopupMenu(),content=CreatePopupMenu(),layout=CreatePopupMenu(),interval=CreatePopupMenu(),alpha=CreatePopupMenu(),capsuleMenu=CreatePopupMenu();
        auto item=[](HMENU m,UINT id,const wchar_t*text,bool check=false){AppendMenuW(m,MF_STRING|(check?MF_CHECKED:0),id,text);};
        AppendMenuW(menu,MF_STRING|MF_DISABLED,0,L"设备电量悬浮窗 2.2.0.16 · 原生轻量版");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        item(menu,Open,L"显示监控界面");item(menu,Refresh,L"立即刷新");
        constexpr UINT firstDevice = 20000;
        HMENU rename = CreatePopupMenu();
        // The scanner may replace or reorder devices while the menu or editor is open.
        std::vector<DeviceNames::Edit> targets;
        std::map<std::wstring,int> counts;
        for (const auto& d : devices) if (!d.key.empty()) ++counts[d.DisplayName()];
        for (const auto& d : devices) {
            if (d.key.empty()) continue;
            auto label = d.DisplayName();
            if (label.empty()) label = L"未命名设备";
            if (counts[d.DisplayName()] > 1) label += L"（" + d.address + L"）";
            if (!Current(d)) label += L" · " + d.StateText();
            for (size_t pos = 0; (pos = label.find(L'&', pos)) != std::wstring::npos; pos += 2) label.insert(pos, 1, L'&');
            item(rename,firstDevice + UINT(targets.size()),label.c_str());
            targets.push_back({d.key,d.name,d.alias,aliasStore.c_str()});
        }
        if (targets.empty()) AppendMenuW(rename,MF_STRING|MF_GRAYED,0,L"暂无可改名的设备");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(rename),L"设备改名");
        item(content,Cpu,L"CPU",settings.cpu);item(content,Memory,L"内存",settings.memory);item(content,Upload,L"上传速度",settings.upload);item(content,Download,L"下载速度",settings.download);item(content,ComputerBattery,L"电脑电量",settings.computerBattery);item(content,Battery,L"蓝牙设备电量",settings.battery);AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(content),L"任务栏显示内容");
        item(layout,Rows2,L"两列两行",settings.rows==2);item(layout,Rows3,L"两列三行",settings.rows==3);AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(layout),L"任务栏布局");
        for(int n:{5,15,30,60,3600,14400}){auto label=IntervalText(n);item(interval,200+n,label.c_str(),settings.interval==n);}AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(interval),L"设备检查间隔");
        item(capsuleMenu,CapsuleTaskbar,L"固定在任务栏",!settings.desktop);item(capsuleMenu,CapsuleDesktop,L"桌面浮动（可拖动）",settings.desktop);item(capsuleMenu,CapsuleReset,L"恢复桌面位置");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(capsuleMenu),L"胶囊位置");
        HMENU background = CreatePopupMenu();
        item(background,BackgroundSystem,L"跟随系统",settings.background.mode == BackgroundMode::System);
        item(background,BackgroundTransparent,L"透明背景",settings.background.mode == BackgroundMode::Transparent);
        item(background,BackgroundCustom,L"自定义颜色…",settings.background.mode == BackgroundMode::Custom);
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(background),L"胶囊背景");
        item(menu,All,L"显示未连接设备",settings.all);item(menu,Pin,L"监控界面置顶",settings.pin);item(menu,Notice,L"低于或等于 20% 时提醒",settings.notice);item(menu,Startup,L"开机自启动",StartupEnabled());
        item(alpha,Opacity100,L"100%",settings.opacity==100);item(alpha,Opacity90,L"90%",settings.opacity==90);item(alpha,Opacity75,L"75%",settings.opacity==75);AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(alpha),L"监控界面不透明度");
        item(menu,Collapse,L"折叠监控界面",settings.collapsed);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);item(menu,Diagnostic,L"导出兼容性诊断…");item(menu,Bluetooth,L"Windows 蓝牙设置");item(menu,Help,L"使用说明");item(menu,HidePanel,L"收起监控界面");item(menu,Exit,L"退出应用");
        POINT pos;GetCursorPos(&pos);SetForegroundWindow(owner);UINT choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pos.x,pos.y,0,owner,nullptr);DestroyMenu(menu);PostMessageW(owner,WM_NULL,0,0);if(choice >= firstDevice && choice-firstDevice < targets.size()) EditName(owner,targets[choice-firstDevice]);
        else if(choice) Command(choice,owner);
    }
    void Export(){
        wchar_t path[32768]=L"设备兼容性诊断.txt";OPENFILENAMEW o{sizeof o};o.hwndOwner=panel&&IsWindowVisible(panel)?panel:capsule;o.lpstrFilter=L"文本报告 (*.txt)\0*.txt\0\0";o.lpstrFile=path;o.nMaxFile=32768;o.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;o.lpstrDefExt=L"txt";
        if(GetSaveFileNameW(&o)){std::wstring report;{std::lock_guard<std::mutex>lock(gate);report=diagnostic;}WriteUtf8(path,report);}
    }
    void Command(UINT id, HWND owner=nullptr){
        switch(id){
        case Open:ShowPanel();return;case Refresh:Request();return;
        case HidePanel:RememberPosition();if(panel)ShowWindow(panel,SW_HIDE);return;
        case Exit:RememberPosition();DestroyWindow(capsule);return;
        case CapsuleTaskbar:EndCapsuleDrag();settings.desktop=false;Save();Tick();return;
        case CapsuleDesktop:settings.desktop=true;Save();Tick();return;
        case CapsuleReset:settings.capsuleLeft=INT_MIN;settings.capsuleTop=INT_MIN;Save();Tick();return;
        case BackgroundSystem: settings.background.mode=BackgroundMode::System; break;
        case BackgroundTransparent: settings.background.mode=BackgroundMode::Transparent; break;
        case BackgroundCustom: {
            static COLORREF swatches[16]{};
            CHOOSECOLORW picker{sizeof picker};
            picker.hwndOwner=owner ? owner : capsule;
            picker.rgbResult=settings.background.color;
            picker.lpCustColors=swatches;
            picker.Flags=CC_RGBINIT | CC_FULLOPEN;
            if (!ChooseColorW(&picker)) return;
            settings.background={BackgroundMode::Custom,picker.rgbResult};
            break;
        }
        case Upload:settings.upload=!settings.upload;break;
        case Download:settings.download=!settings.download;break;
        case Cpu:settings.cpu=!settings.cpu;break;case Memory:settings.memory=!settings.memory;break;case ComputerBattery:settings.computerBattery=!settings.computerBattery;break;case Battery:settings.battery=!settings.battery;break;
        case Rows2:settings.rows=2;break;case Rows3:settings.rows=3;break;
        case All:settings.all=!settings.all;scroll=0;break;
        case Pin:settings.pin=!settings.pin;if(panel)SetWindowPos(panel,settings.pin?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);break;
        case Notice:settings.notice=!settings.notice;break;
        case Startup:SetStartup(!StartupEnabled());return;
        case Diagnostic:Export();return;
        case Bluetooth:ShellExecuteW(nullptr,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);return;
        case Help:ShellExecuteW(nullptr,L"open",Beside(L"使用说明.txt").c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;
        case Collapse:ShowPanel();settings.collapsed=true;break;
        case Opacity100:case Opacity90:case Opacity75:settings.opacity=id==Opacity75?75:id==Opacity90?90:100;if(panel)SetLayeredWindowAttributes(panel,0,BYTE(settings.opacity*255/100),LWA_ALPHA);break;
        default:if(id>=205&&Settings::ValidInterval(id-200)){settings.interval=id-200;scanInterval=settings.interval;Request();}break;
        }
        Save();Tick();InvalidateRect(capsule,nullptr,FALSE);if(panel)InvalidateRect(panel,nullptr,FALSE);
    }
    void NotifyLow(){if(!settings.notice||test)return;ULONGLONG now=FileNow();for(const auto&d:devices){if(d.battery>25)lowNotices.erase(d.key);if(d.connected!=1||d.battery<0||d.battery>20||!d.batteryTime||d.batteryTime>now||now-d.batteryTime>3000000000ULL)continue;
        auto it=lowNotices.find(d.key);if(it!=lowNotices.end()&&GetTickCount64()-it->second<7200000)continue;lowNotices[d.key]=GetTickCount64();tray.uFlags=NIF_INFO;wcscpy_s(tray.szInfoTitle,L"无线设备电量偏低");auto text=d.DisplayName()+L" 剩余 "+d.BatteryText();wcsncpy_s(tray.szInfo,text.c_str(),_TRUNCATE);tray.dwInfoFlags=NIIF_WARNING;Shell_NotifyIconW(NIM_MODIFY,&tray);}}
    std::vector<const Device*> VisibleDevices(){std::vector<const Device*> result;for(auto&d:devices)if(settings.all||Current(d))result.push_back(&d);return result;}
    void Paint(HWND h){
        if(h==capsule){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);capsulePainter.Present(h,settings.rows,lightTaskbar,hoverCapsule,items,settings.background);return;}
        if(h==hoverList){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);if(hoverListVisible)DrawHover();return;}
        PAINTSTRUCT paint;HDC target=BeginPaint(h,&paint);RECT pixels;GetClientRect(h,&pixels);if(pixels.right<=0||pixels.bottom<=0){EndPaint(h,&paint);return;}
        UINT dpi=WindowDpi(h);RECT r{0,0,std::max(1,MulDiv(pixels.right,96,int(dpi))),std::max(1,MulDiv(pixels.bottom,96,int(dpi)))};
        HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,pixels.right,pixels.bottom);auto oldBitmap=SelectObject(dc,bitmap);
        int mapping=SaveDC(dc);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,r.right,r.bottom,nullptr);SetViewportExtEx(dc,pixels.right,pixels.bottom,nullptr);
            Fill(dc,r,RGB(19,29,45));Draw(dc,L"设备电量与系统资源",{16,8,r.right-14,42},RGB(237,244,255),big);
            std::wstring computerText=resources.computerBattery>=0?std::to_wstring(resources.computerBattery)+L"%":resources.computerBatteryPresent?L"—":L"无电池";
            Draw(dc,L"CPU "+(resources.cpu<0?L"采样中":std::to_wstring(resources.cpu)+L"%")+L"   ·   内存 "+std::to_wstring(resources.memory)+L"%   ·   电脑 "+computerText,{16,43,r.right-14,68},RGB(148,163,184),small);
            Draw(dc,FormatMemory(resources.used)+L" / "+FormatMemory(resources.total)+L"   ·   原生轻量版",{16,67,r.right-14,89},RGB(148,163,184),small);
            Draw(dc,L"上传  "+FormatRate(resources.network.upload)+L"     下载  "+FormatRate(resources.network.download),
                 {16,90,r.right-14,113},RGB(175,201,219),normal);
            Draw(dc,settings.all?L"[当前]     [全部 ✓]     [刷新]     [收起]":L"[当前 ✓]     [全部]     [刷新]     [收起]",{16,PanelActionsTop,r.right-14,PanelActionsBottom},RGB(124,182,255),normal);
            RECT viewport{0,DeviceListTop,r.right,std::max<LONG>(DeviceListTop,r.bottom-32)};int saved=SaveDC(dc);IntersectClipRect(dc,viewport.left,viewport.top,viewport.right,viewport.bottom);
            if(!settings.collapsed){auto visible=VisibleDevices();int maxScroll=std::max<int>(0,int(visible.size())*116-(viewport.bottom-viewport.top));scroll=std::clamp(scroll,0,maxScroll);
                if(visible.empty())Draw(dc,L"当前没有已连接设备，可切换“全部”查看",{16,DeviceListTop+17,r.right-14,DeviceListTop+77},RGB(148,163,184),normal,DT_WORDBREAK);
                int y=DeviceListTop-scroll;for(auto*d:visible){RECT card{12,y+3,r.right-12,y+110};Fill(dc,card,d->connected==1?RGB(30,45,67):RGB(25,37,56));
                    Draw(dc,d->DisplayName(),{24,y+9,r.right-100,y+37},RGB(237,244,255),normal);
                    Draw(dc,d->BatteryText(),{r.right-98,y+7,r.right-24,y+39},d->connected!=1?RGB(148,163,184):d->battery<=20?RGB(255,176,120):RGB(97,219,178),big,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
                    Draw(dc,d->StateText()+L" · "+d->transport,{24,y+38,r.right-24,y+58},RGB(148,163,184),small);
                    if(d->battery>=0){RECT track{24,y+65,r.right-24,y+69};Fill(dc,track,RGB(53,67,90));track.right=track.left+(track.right-track.left)*d->battery/100;Fill(dc,track,d->connected==1?RGB(97,219,178):RGB(148,163,184));}
                    Draw(dc,d->Note(),{24,y+78,r.right-24,y+101},RGB(148,163,184),small);y+=116;
                }
            }RestoreDC(dc,saved);Fill(dc,{0,std::max<LONG>(DeviceListTop,r.bottom-32),r.right,r.bottom},RGB(19,29,45));Draw(dc,scanStatus,{16,r.bottom-30,r.right-14,r.bottom-4},RGB(148,163,184),small);
        RestoreDC(dc,mapping);BitBlt(target,0,0,pixels.right,pixels.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(h,&paint);
    }
    void DetailAt(int y){auto v=VisibleDevices();int index=(y-DeviceListTop+scroll)/116;if(y<DeviceListTop||index<0||index>=int(v.size()))return;auto&d=*v[index];std::wstring text=d.DisplayName()+(d.alias.empty()?L"":L"\n原名："+d.name)+L"\n连接："+d.StateText()+L"\n电量："+d.BatteryText()+L"\n来源："+d.Note()+L"\n传输："+d.transport+L"\n标识："+d.address+L"\n"+d.detail;MessageBoxW(panel,text.c_str(),L"设备详情",MB_OK|MB_ICONINFORMATION);}
    LRESULT Message(HWND h,UINT msg,WPARAM w,LPARAM l){
        if(msg==taskbarCreated){if(h==capsule){AddTray();EnsureCapsuleTopmost();Position();}return 0;}
        switch(msg){
        case WM_PAINT:Paint(h);return 0;case WM_ERASEBKGND:return 1;
        case WM_MOUSEACTIVATE:if(h==capsule||h==hoverList)return MA_NOACTIVATE;break;
        case WM_LBUTTONDBLCLK:if(h==capsule)ShowPanel();else if(h==panel)DetailAt(DpiLogical(GET_Y_LPARAM(l),h));return 0;
        case WM_LBUTTONDOWN:if(h==hoverList)return 0;if(h==capsule&&settings.desktop){HideHoverList();POINT cursor{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(h,&cursor);RECT r{};GetWindowRect(h,&r);dragStartCursor=cursor;dragStartWindow={r.left,r.top};draggingCapsule=true;SetCapture(h);return 0;}break;
        case WM_LBUTTONUP:if(h==hoverList&&HoverScrollable()){
            RECT r{};GetClientRect(h,&r);
            if(GET_Y_LPARAM(l)>=r.bottom-DpiPixels(32,WindowDpi(h)))ScrollHover(visibleHoverRows,true);
            return 0;
        }if(h==capsule&&draggingCapsule){EndCapsuleDrag();return 0;}if(h==panel&&DpiLogical(GET_Y_LPARAM(l),h)>=PanelActionsTop&&DpiLogical(GET_Y_LPARAM(l),h)<=PanelActionsBottom){int x=DpiLogical(GET_X_LPARAM(l),h);if(x<90){settings.all=false;Save();scroll=0;}else if(x<180){settings.all=true;Save();scroll=0;}else if(x<270)Request();else{Command(HidePanel);return 0;}InvalidateRect(panel,nullptr,FALSE);}return 0;
        case WM_CAPTURECHANGED:case WM_CANCELMODE:if(h==capsule)EndCapsuleDrag();break;
        case WM_RBUTTONUP:if(h==hoverList)Menu(capsule);else Menu(h);return 0;
        case WM_MOUSEWHEEL:if(h==hoverList){
            wheelRemainder+=GET_WHEEL_DELTA_WPARAM(w);
            ScrollHover(-3*(wheelRemainder/WHEEL_DELTA));wheelRemainder%=WHEEL_DELTA;return 0;
        }if(h==panel){scroll=std::max(0,scroll-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*64);InvalidateRect(h,nullptr,FALSE);}return 0;
        case WM_SIZE:InvalidateRect(h,nullptr,FALSE);return 0;
        case WM_MOUSEMOVE:if(h==hoverList){hoverPopup=true;KillTimer(capsule,3);TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,hoverList,0};TrackMouseEvent(&track);return 0;}if(h==capsule&&draggingCapsule){POINT cursor{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(h,&cursor);int x=dragStartWindow.x+(cursor.x-dragStartCursor.x),y=dragStartWindow.y+(cursor.y-dragStartCursor.y);DesktopBounds(x,y,false);return 0;}if(h==capsule){if(!hoverCapsule){hoverCapsule=true;TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,capsule,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}if(!hoverListVisible)ShowHoverList();}return 0;
        case WM_DPICHANGED:{
            const auto* suggested=reinterpret_cast<const RECT*>(l);
            if(h==capsule){
                if(!movingCapsule){
                    if(settings.desktop){RECT r{};GetWindowRect(h,&r);if(suggested&&!draggingCapsule)r=*suggested;DesktopBounds(r.left,r.top,!draggingCapsule);}
                    else Position();
                }
            }else if(h==panel&&suggested)SetWindowPos(h,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOACTIVATE|SWP_NOZORDER);
            else if(h==hoverList)PositionHoverList();
            InvalidateRect(h,nullptr,FALSE);return 0;
        }
        case WM_DISPLAYCHANGE:case WM_SETTINGCHANGE:if(h==capsule){Position();HoverListChanged();}else if(h==hoverList)PositionHoverList();InvalidateRect(h,nullptr,FALSE);return 0;
        case WM_MOUSELEAVE:if(h==capsule){hoverCapsule=false;ScheduleHoverHide();InvalidateRect(h,nullptr,FALSE);}else if(h==hoverList){hoverPopup=false;ScheduleHoverHide();}return 0;
        case RaiseCapsule:if(h==capsule){EnsureCapsuleTopmostIfCovered();raisePending=false;}return 0;
        case WM_COMMAND:Command(LOWORD(w));return 0;
        case WM_TIMER:if(h==capsule){if(w==2)EnsureCapsuleTopmostIfCovered();else if(w==3){if(!CursorInsideHover())HideHoverList();else KillTimer(capsule,3);}else Tick();}return 0;
        case WM_DEVICECHANGE:if(h==capsule)Request();return 0;
        case Changed:{std::lock_guard<std::mutex> lock(gate);devices=nextDevices;scanStatus=nextStatus;}for(auto& d:devices) d.alias=DeviceNames::Read(d.key,aliasStore.c_str());NotifyLow();Tick();return 0;
        case TrayMessage:if(l==WM_LBUTTONDBLCLK)ShowPanel();else if(l==WM_RBUTTONUP)Menu(capsule);return 0;
        case WM_CLOSE:if(h==panel){RememberPosition();ShowWindow(panel,SW_HIDE);}else DestroyWindow(h);return 0;
        case WM_DESTROY:if(h==hoverList){hoverList=nullptr;hoverListVisible=false;hoverPopup=false;return 0;}if(h==capsule){stopping=true;wake.notify_all();PostQuitMessage(0);}return 0;
        }return DefWindowProcW(h,msg,w,l);
    }
};
int SelfTest(const std::wstring&file){
    std::wstring failure;int checks=0;auto check=[&](bool good,const wchar_t* message){checks++;if(!good)failure+=std::wstring(message)+L"\n";};
    check(Settings::ValidInterval(3600)&&Settings::ValidInterval(14400)&&!Settings::ValidInterval(3601),L"Long refresh intervals");check(IntervalText(3600)==L"1 小时"&&IntervalText(14400)==L"4 小时",L"Long interval labels");
    check(NormalizeBattery(0,20,19)==95,L"K380 range conversion");check(NormalizeBattery(0,20,0)==0,L"Real zero battery");check(NormalizeBattery(0,20,21)==-1,L"Out of range rejected");check(NormalizeBattery(-1,100,50)==-1,L"Negative range rejected");check(NormalizeBattery(0,0,0)==-1,L"Empty range rejected");
    BYTE bb[]={0xbb,0xb0,0x81,0x20,3,1,90};check(RapooBattery(bb,7)==90,L"Rapoo status");bb[6]=101;check(RapooBattery(bb,7)==-1,L"Rapoo overflow");bb[6]=90;bb[0]=0xbc;check(RapooBattery(bb,7)==-1,L"Rapoo transition rejected");check(RapooBattery(bb,6)==-1,L"Rapoo short rejected");
    BYTE gaming[20]={7,0,1,0x20,3,0x20,3,1,80};check(RapooBattery(gaming,20)==80,L"Rapoo gaming status");gaming[3]=0;gaming[4]=0;check(RapooBattery(gaming,20)==-1,L"Rapoo invalid status metadata");gaming[0]=8;check(RapooBattery(gaming,20)==-1,L"Unknown report rejected");
    BYTE logi[]={30,10,0};int n=-1;check(LogitechBattery(logi,3,n)&&n==30,L"G304 decode");check(LogitechModelName(L"G304 X")==L"罗技 G304X",L"G304 X model alias");check(LogitechModelName(L"G304X")==L"罗技 G304X",L"G304X compact model alias");check(LogitechModelName(L"G305-X")==L"罗技 G305X",L"G305 X model alias");logi[0]=0;check(!LogitechBattery(logi,3,n),L"G304 zero is unknown");check(!LogitechBattery(logi,1,n),L"G304 short rejected");
    Device d;d.connected=1;d.name=L"test";d.battery=0;d.source=L"test";d.batteryTime=FileNow();check(d.BatteryText()==L"0%",L"Zero display");check(d.Note().find(L"历史")==std::wstring::npos,L"Current battery note");d.connected=0;check(d.Note().find(L"历史")!=std::wstring::npos,L"Offline cache historical");
    Resources r;r.cpu=3;r.memory=40;Device a,b;a.connected=1;a.battery=90;a.name=L"high";b.connected=1;b.battery=10;b.name=L"low";auto items=MonitorItems(r,{a,b,d},true,true,true,false);check(items.size()==4&&items[2].name==L"low",L"Lowest battery first / offline filtered");check(MonitorItems(r,{a,b,d},false,false,true,false).size()==2,L"Disabled fields packed");check(MonitorItems(r,{},false,false,false,false)[0].Text()==L"监控已关闭",L"All disabled");check(MonitorItems(r,{},true,true,true,false).back().Text()==L"电量: —",L"Unknown not zero");
    r.computerBattery=76;r.computerBatteryPresent=true;r.computerCharging=true;auto computerItems=MonitorItems(r,{},true,true,false,true);check(computerItems.size()==3&&computerItems[2].Text()==L"电脑: 76%",L"Computer battery item");check(MonitorItems(r,{},false,false,false,true).size()==1,L"Computer battery switch");r.computerBattery=-1;r.computerBatteryPresent=false;r.computerCharging=false;check(MonitorItems(r,{},false,false,false,true)[0].Text()==L"电脑: 无电池",L"No computer battery state");
    check(DpiPixels(44,96)==44&&DpiPixels(44,120)==55&&DpiPixels(44,144)==66&&DpiPixels(44,192)==88,L"Capsule heights at 100 / 125 / 150 / 200 percent");
    check(DpiPixels(210,120)==263&&DpiPixels(210,144)==315&&DpiPixels(210,192)==420,L"Capsule width follows the same DPI as text");
    const std::vector<InterfaceBytes> prior{{1,1000,8000},{2,2000,1000},{3,9000,9000}};
    auto traffic=NetworkDelta(prior,{{2,3000,2000},{4,100000,90000},{1,3000,14000}},2000);
    check(traffic.upload==1500 && traffic.download==3500,L"Network directions, elapsed time and adapter turnover");
    auto quiet=NetworkDelta(prior,prior,1000);
    check(quiet.upload==0 && quiet.download==0,L"Idle network speed is zero");
    check(NetworkDelta({},prior,1000).upload<0 && NetworkDelta(prior,{},1000).download<0 &&
          NetworkDelta(prior,{{1,200,300}},1000).download<0 && NetworkDelta(prior,prior,15000).upload<0,
          L"No spikes on first sample, disconnect, counter reset or resume");
    check(FormatRate(-1)==L"—" && FormatRate(0)==L"0 B/s" && FormatRate(999.6)==L"1.0 KB/s" &&
          FormatRate(1240000)==L"1.2 MB/s",L"Network automatically scales units");
    r.network={21000,42000};auto up=MonitorItems(r,{},false,false,false,false,true,false);
    auto down=MonitorItems(r,{},false,false,false,false,false,true);
    check(up.size()==1 && up[0].Label()==L"↑" && up[0].Text()==L"上传: 21 KB/s" &&
          down.size()==1 && down[0].Label()==L"↓" && down[0].Text()==L"下载: 42 KB/s",L"Network metrics switch independently");
    Settings backgroundSaved; backgroundSaved.path=file+L".background.ini";
    backgroundSaved.upload=false; backgroundSaved.download=true;
    backgroundSaved.background={BackgroundMode::Custom,RGB(40,110,210)}; backgroundSaved.Save();
    Settings backgroundLoaded; backgroundLoaded.path=backgroundSaved.path; backgroundLoaded.Load();
    check(!backgroundLoaded.upload && backgroundLoaded.download,L"Network choices survive restart");
    check(backgroundLoaded.background.mode==BackgroundMode::Custom && backgroundLoaded.background.color==RGB(40,110,210),L"Custom capsule color persists");
    backgroundSaved.background.mode=BackgroundMode::Transparent; backgroundSaved.Save(); backgroundLoaded.Load();
    check(backgroundLoaded.background.mode==BackgroundMode::Transparent,L"Transparent capsule mode persists");
    DeleteFileW(backgroundSaved.path.c_str());
    CapsulePainter backgroundPainter;
    std::vector<MonitorItem> backgroundItems{{L"CPU",L"29%"},{L"内存",L"78%"}};
    Gdiplus::Bitmap transparentBitmap(128,44,PixelFormat32bppARGB); Gdiplus::Graphics transparentGraphics(&transparentBitmap);
    backgroundPainter.Draw(transparentGraphics,128,44,2,false,false,backgroundItems,1,CapsuleBackground{BackgroundMode::Transparent,RGB(40,110,210)});
    Gdiplus::Color transparentPixel; transparentBitmap.GetPixel(4,4,&transparentPixel);
    Gdiplus::Bitmap customBitmap(128,44,PixelFormat32bppARGB); Gdiplus::Graphics customGraphics(&customBitmap);
    backgroundPainter.Draw(customGraphics,128,44,2,false,false,backgroundItems,1,CapsuleBackground{BackgroundMode::Custom,RGB(40,110,210)});
    Gdiplus::Color customPixel; customBitmap.GetPixel(64,4,&customPixel);
    check(transparentPixel.GetA()<=2,L"Transparent capsule clears the background");
    check(customPixel.GetA()>=240,L"Custom capsule paints an opaque background");
    CapsulePainter painter;
    std::vector<MonitorItem> sample{{L"CPU",L"9%"},{L"内存",L"81%"},{L"助听器",L"70%",true},{L"黑鼠",L"约90%",true},{L"键盘侠",L"100%",true}};
    std::vector<MonitorItem> pair{up[0],down[0]};
    int pairWidth=painter.PreferredWidth(44,2,pair);
    pair[0].value=L"0 B/s";pair[1].value=L"999 MB/s";
    check(painter.PreferredWidth(44,2,pair)==pairWidth,L"Changing network units preserves outer width");
    std::vector<MonitorItem> mixed{{L"办公室键盘",L"70%",true},{L"CPU",L"5%"}};
    auto narrowPair=painter.PreferredWidth(44,2,mixed);
    mixed[1]=mixed[0];check(narrowPair<painter.PreferredWidth(44,2,mixed),L"A short neighbour uses less space than a second long name");
    int compact=painter.PreferredWidth(44,3,sample);
    check(compact<210,L"Three-row capsule fits short aliases");
    sample[0].value=L"100%";sample[3].value=L"约1%";
    check(compact==painter.PreferredWidth(44,3,sample),L"Percentage updates keep the same width");
    int twoRows=painter.PreferredWidth(44,2,sample);
    sample[4].name=std::wstring(40,L'键');
    check(painter.PreferredWidth(44,2,sample)==twoRows,L"Hidden fifth item cannot widen a two-row capsule");
    check(painter.PreferredWidth(44,3,sample)==236,L"Long visible aliases respect the width cap");
    std::vector<MonitorItem> overflow{{L"CPU",L"9%"},{L"内存",L"81%"},{L"电脑",L"76%",true},{L"G304X",L"约30%",true},{L"键盘",L"85%",true}};
    SIZE hoverSize=painter.HoverListSize(overflow);
    check(hoverSize.cx>=176&&hoverSize.cy>=12+int(overflow.size())*28,L"Hover list sizes every overflow item");
    if(!failure.empty()){WriteUtf8(file,L"FAIL\n"+failure);return 1;}WriteUtf8(file,L"PASS: "+std::to_wstring(checks)+L" native protocol / layout / historical-state checks\n");return 0;
}
BOOL CALLBACK CollectWorkAreas(HMONITOR monitor,HDC,LPRECT,LPARAM data){
    MONITORINFO mi{sizeof mi};if(GetMonitorInfoW(monitor,&mi))reinterpret_cast<std::vector<RECT>*>(data)->push_back(mi.rcWork);return TRUE;
}
// Exercises the actual dialog and scanner result message using an isolated registry key.
struct RenameTestStep {
    const wchar_t* input;
    int button;
    bool editReady = false;
};
RenameTestStep* renameStep = nullptr;
void CALLBACK OperateRenameDialog(HWND,UINT,UINT_PTR timer,DWORD) {
    HWND dialog = GetActiveWindow();
    HWND field = GetDlgItem(dialog,IDC_DEVICE_ALIAS);
    if (!field || !renameStep) return;
    KillTimer(nullptr,timer);
    HIMC context = ImmGetContext(field);
    renameStep->editReady = context && GetFocus() == field;
    if (context) ImmReleaseContext(field,context);
    // Deliver a reordered scan while the editor is still using the original selection.
    std::reverse(live->nextDevices.begin(),live->nextDevices.end());
    SendMessageW(live->capsule,Changed,0,0);
    SetWindowTextW(field,renameStep->input);
    SendMessageW(GetDlgItem(dialog,renameStep->button),BM_CLICK,0,0);
}
bool TestDeviceNames(App& app,std::wstring& report) {
    app.aliasStore = L"Software\\BlueBatteryFloat\\NameTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    Device a,b;
    a.key=L"test:A"; a.name=L"罗技 G304X"; a.connected=1; a.battery=25;
    b=a; b.key=L"test:B"; b.battery=70;
    app.nextDevices={a,b};
    app.settings.battery=true;
    auto scan = [&]{ SendMessageW(app.capsule,Changed,0,0); };
    auto edit = [&](const wchar_t* value,int button) {
        RenameTestStep step{value,button};
        renameStep=&step;
        auto timer=SetTimer(nullptr,0,20,OperateRenameDialog);
        if (!timer) {renameStep=nullptr; return false;}
        app.EditName(app.capsule,{a.key,a.name,DeviceNames::Read(a.key,app.aliasStore.c_str()),app.aliasStore.c_str()});
        KillTimer(nullptr,timer);renameStep=nullptr;
        return step.editReady;
    };
    auto check = [&](const wchar_t* name,bool valid){ report+=(valid?L"PASS: ":L"FAIL: ")+std::wstring(name)+L"\n";return valid;};
    bool ok=true;
    auto visible = [&](const wchar_t* expected) {
        return std::any_of(app.items.begin(),app.items.end(),[&](const MonitorItem& item){
            return item.Label()==expected && item.value==L"25%" && item.batteryIcon;
        });
    };
    scan();
    ok &= check(L"Default short model name and battery icon",visible(L"G304X"));
    ok &= check(L"Chinese input context and focused editor",edit(L"  罗技 书桌: A&B  ",IDOK));
    ok &= check(L"Immediate name / battery / tooltip after save",visible(L"罗技 书桌: A&B") && app.tooltipText.find(L"罗技 书桌: A&B: 25%")!=std::wstring::npos);
    scan();
    ok &= check(L"Alias survives a fresh scan and a separate registry read",DeviceNames::Read(a.key,app.aliasStore.c_str())==L"罗技 书桌: A&B" && visible(L"罗技 书桌: A&B"));
    ok &= check(L"Same-model devices keep independent aliases and original model",app.devices[0].DisplayName()==b.name && app.devices[1].name==a.name);
    ok &= check(L"Cancel leaves the alias unchanged",edit(L"取消的名字",IDCANCEL) && visible(L"罗技 书桌: A&B"));
    ok &= check(L"A device named RAM keeps its own label and battery icon",edit(L"RAM",IDOK) && visible(L"RAM"));
    ok &= check(L"Restore original name",edit(L"忽略",IDC_RESET_NAME) && visible(L"G304X") && DeviceNames::Read(a.key,app.aliasStore.c_str()).empty());
    ok &= check(L"Whitespace-only alias restores original",edit(L"鼠标",IDOK) && edit(L"　 ",IDOK) && visible(L"G304X"));
    RegDeleteKeyW(HKEY_CURRENT_USER,app.aliasStore.c_str());
    return ok;
}

int UiTest(const std::wstring&path){
    App app(true);app.Create();app.settings.rows=2;app.Tick();RECT r{};GetClientRect(app.capsule,&r);std::wstring monitorReport;
    auto verify=[&](bool good,const wchar_t* condition){if(!good)monitorReport+=std::wstring(L"FAIL: ")+condition+L"\n";return good;};
    bool ok=verify(r.bottom>=24,L"initial capsule height");
    app.items={{L"CPU",L"9%"},{L"内存",L"81%"},{L"电脑",L"76%",true},{L"G304X",L"约30%",true},{L"键盘",L"85%",true}};
    app.hoverCapsule=true;app.ShowHoverList();RECT hoverRect{};GetWindowRect(app.hoverList,&hoverRect);bool hoverShown=app.HoverListNeeded()&&app.hoverList&&IsWindowVisible(app.hoverList)&&hoverRect.right-hoverRect.left>=176&&hoverRect.bottom-hoverRect.top>=12+5*28;app.HideHoverList();app.items.resize(4);bool hoverThreshold=!app.HoverListNeeded()&&!IsWindowVisible(app.hoverList);
    app.settings.rows=3;app.items.resize(6);hoverThreshold&=!app.HoverListNeeded();app.items.resize(7);
    SendMessageW(app.capsule,WM_MOUSEMOVE,0,0);hoverThreshold&=app.hoverListVisible&&app.visibleHoverRows==7;
    SendMessageW(app.capsule,WM_MOUSELEAVE,0,0);SendMessageW(app.hoverList,WM_MOUSEMOVE,0,0);
    hoverThreshold&=app.hoverPopup&&IsWindowVisible(app.hoverList)&&SendMessageW(app.hoverList,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE;
    app.items.resize(40);app.HoverListChanged();for(int i=0;i<20;++i)SendMessageW(app.hoverList,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);
    hoverThreshold&=app.firstHoverRow+app.visibleHoverRows==40;app.items.resize(6);app.HoverListChanged();
    hoverThreshold&=!app.hoverListVisible;app.hoverCapsule=false;app.settings.rows=2;ok &= verify(hoverShown&&hoverThreshold,L"overflow list behaviour");
    app.settings.desktop=true;app.settings.capsuleLeft=120;app.settings.capsuleTop=140;app.Position();RECT desktopRect{};GetWindowRect(app.capsule,&desktopRect);ok &= verify(desktopRect.left==120&&desktopRect.top==140&&((GetWindowLongW(app.capsule,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0),L"floating capsule position and topmost style");
    SendMessageW(app.capsule,WM_LBUTTONDOWN,MK_LBUTTON,0);SendMessageW(app.capsule,WM_LBUTTONUP,0,0);ok &= verify(app.settings.capsuleLeft==desktopRect.left&&app.settings.capsuleTop==desktopRect.top,L"saved drag origin");
    std::vector<RECT> workAreas;EnumDisplayMonitors(nullptr,nullptr,CollectWorkAreas,reinterpret_cast<LPARAM>(&workAreas));
    monitorReport+=((hoverShown&&hoverThreshold)?L"PASS":L"FAIL");monitorReport+=L": hover list threshold / window sizing / hide behavior\n";ok &= verify(!workAreas.empty(),L"monitor enumeration");
    for(const auto&work:workAreas){
        int x=int(work.left)+std::max(4,std::min(48,(int(work.right-work.left)-app.taskWidth)/2)),y=int(work.top)+std::max(4,std::min(48,(int(work.bottom-work.top)-44)/2));
        SendMessageW(app.capsule,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));
        POINT target{x+10,y+10};ScreenToClient(app.capsule,&target);
        SendMessageW(app.capsule,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(target.x,target.y));
        for(int i=0;i<3;i++)SendMessageW(app.capsule,WM_TIMER,1,0);
        RECT moved{};GetWindowRect(app.capsule,&moved);bool held=moved.left==x&&moved.top==y;
        SendMessageW(app.capsule,WM_LBUTTONUP,0,0);
        for(int i=0;i<3;i++)SendMessageW(app.capsule,WM_TIMER,1,0);
        SendMessageW(app.capsule,WM_DISPLAYCHANGE,32,0);SendMessageW(app.capsule,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);
        GetWindowRect(app.capsule,&moved);bool retained=moved.left==x&&moved.top==y&&app.settings.capsuleLeft==x&&app.settings.capsuleTop==y&&!app.draggingCapsule&&GetCapture()!=app.capsule;
        UINT dpi=WindowDpi(app.capsule);bool scaled=moved.right-moved.left==DpiPixels(app.taskWidth,dpi)&&moved.bottom-moved.top==DpiPixels(44,dpi);
        Settings saved=app.settings;saved.path=path+L".settings.ini";saved.Save();Settings loaded;loaded.path=saved.path;loaded.Load();DeleteFileW(saved.path.c_str());
        bool persisted=loaded.desktop&&loaded.capsuleLeft==x&&loaded.capsuleTop==y;
        ok &= verify(held&&retained&&persisted&&scaled,L"drag, DPI and position persistence");
        monitorReport+=(held&&retained&&persisted&&scaled?L"PASS":L"FAIL")+std::wstring(L": monitor work area [")+std::to_wstring(work.left)+L","+std::to_wstring(work.top)+L","+std::to_wstring(work.right)+L","+std::to_wstring(work.bottom)+L"], destination ("+std::to_wstring(x)+L","+std::to_wstring(y)+L"), DPI "+std::to_wstring(dpi)+L", capsule "+std::to_wstring(moved.right-moved.left)+L"x"+std::to_wstring(moved.bottom-moved.top)+L", drag / timer / release / persistence / display change / DPI dimensions\n";
    }
    SendMessageW(app.capsule,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));ReleaseCapture();ok &= verify(!app.draggingCapsule,L"released mouse capture");
    app.Command(CapsuleTaskbar);ok &= verify(!app.settings.desktop,L"return to taskbar");
    RECT taskbarCapsule{};GetWindowRect(app.capsule,&taskbarCapsule);HWND taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);if(taskbar){UINT dpi=WindowDpi(taskbar);RECT tb{};GetWindowRect(taskbar,&tb);if(tb.right-tb.left>=tb.bottom-tb.top){ok &= verify(taskbarCapsule.right-taskbarCapsule.left==DpiPixels(app.taskWidth,dpi)&&taskbarCapsule.bottom-taskbarCapsule.top==std::max(DpiPixels(24,dpi),int(tb.bottom-tb.top)-DpiPixels(2,dpi)*2),L"taskbar capsule size");RECT list{};if(app.TaskListRect(taskbar,list)&&taskbarCapsule.top>=tb.top&&taskbarCapsule.bottom<=tb.bottom)ok &= verify(taskbarCapsule.left>=list.right+DpiPixels(8,dpi),L"task button clearance");}}
    SendMessageW(app.capsule,WM_LBUTTONDBLCLK,MK_LBUTTON,0);ok &= verify(app.panel&&IsWindowVisible(app.panel),L"double-click opens panel");
    for(const auto&work:workAreas){
        SetWindowPos(app.panel,nullptr,work.left+48,work.top+48,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        UINT dpi=WindowDpi(app.panel);app.settings.all=false;
        SendMessageW(app.panel,WM_LBUTTONUP,0,MAKELPARAM(DpiPixels(120,dpi),DpiPixels((PanelActionsTop+PanelActionsBottom)/2,dpi)));bool hit=app.settings.all;
        SendMessageW(app.panel,WM_LBUTTONUP,0,MAKELPARAM(DpiPixels(45,dpi),DpiPixels((PanelActionsTop+PanelActionsBottom)/2,dpi)));hit=hit&&!app.settings.all;
        InvalidateRect(app.panel,nullptr,FALSE);UpdateWindow(app.panel);ok &= verify(hit,L"panel hit targets");
        monitorReport+=(hit?L"PASS":L"FAIL")+std::wstring(L": monitor panel painting / button hit coordinates at DPI ")+std::to_wstring(dpi)+L"\n";
    }
    SendMessageW(app.panel,WM_CLOSE,0,0);ok &= verify(!IsWindowVisible(app.panel),L"panel close");
    SendMessageW(app.capsule,WM_LBUTTONDBLCLK,MK_LBUTTON,0);ok &= verify(IsWindowVisible(app.panel),L"double-click restores panel");
    app.Command(Rows3);ok &= verify(app.settings.rows==3,L"three-row selection");app.Command(Cpu);ok &= verify(app.items.size()>=1,L"monitor content selection");
    SendMessageW(app.capsule,WM_PAINT,0,0);SendMessageW(app.panel,WM_PAINT,0,0);
    ok &= verify(SendMessageW(app.capsule,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,L"no focus activation");
    DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);for(int i=0;i<100;i++){app.Tick();InvalidateRect(app.capsule,nullptr,FALSE);UpdateWindow(app.capsule);InvalidateRect(app.panel,nullptr,FALSE);UpdateWindow(app.panel);}DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);ok &= verify(after<=handles+3&&GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2&&GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+2,L"redraw handle usage");
    ok &= TestDeviceNames(app,monitorReport);
    WriteUtf8(path,(ok?L"PASS: native windows, per-monitor DPI sizing, multi-monitor desktop drag, coordinate persistence, capture release, display-change handling, monitor-panel DPI input, double-click show/restore, close-to-tray, layout/content changes, no-activate handling, 100 redraws without handle growth\n":L"FAIL: UI regression\n")+monitorReport);return ok?0:1;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int argc=0;LPWSTR*argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::vector<std::wstring> args;for(int i=1;i<argc;i++)args.push_back(argv[i]);LocalFree(argv);
    try{
        if(args.size()>=2&&args[0]==L"--render-capsule"){CapsulePainter painter;return painter.Preview(args[1])?0:1;}
        if(args.size()>=2&&args[0]==L"--render-dpi-preview"){CapsulePainter painter;return painter.DpiPreview(args[1])?0:1;}
        if(args.size()>=2&&args[0]==L"--self-test")return SelfTest(args[1]);
        if(args.size()>=2&&args[0]==L"--ui-self-test")return UiTest(args[1]);
        if(args.size()>=2&&(args[0]==L"--diagnose"||args[0]==L"--compatibility-report")){std::atomic<bool>stop{false};Devices backend(stop);std::vector<Device> list;int cycles=args.size()>=3?std::clamp(_wtoi(args[2].c_str()),1,10):1;for(int n=0;n<cycles;n++){list=backend.Scan();if(n+1<cycles)Sleep(1000);}WriteUtf8(args[1],backend.Diagnostic(list));return 0;}
        HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\BlueBatteryFloat_Desktop_v1");if(GetLastError()==ERROR_ALREADY_EXISTS){HWND main=FindWindowW(MainClass,nullptr),capsule=FindWindowW(CapsuleClass,nullptr);if(capsule)PostMessageW(capsule,WM_COMMAND,Open,0);else{HANDLE old=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\BlueBatteryFloat_Show_v1");if(old){SetEvent(old);CloseHandle(old);}}if(mutex)CloseHandle(mutex);return 0;}
        {App app;app.Create();app.StartWorker();if(std::find(args.begin(),args.end(),L"--show")!=args.end())app.ShowPanel();MSG message;while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
        if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);}return 0;
    }catch(...){MessageBoxW(nullptr,L"启动失败，请确认文件完整并稍后重试。",L"设备电量监控",MB_OK|MB_ICONERROR);return 1;}
}









