#define NOMINMAX
#include "Devices.h"
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <psapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <map>
#include <set>
#include <array>
#include <optional>
#include <cstring>

using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;
using winrt::Windows::Foundation::AsyncStatus;

namespace {
ULONGLONG Now() { FILETIME t; GetSystemTimeAsFileTime(&t); return (ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):h(value){}
    ~Handle(){if(h!=INVALID_HANDLE_VALUE&&h) CloseHandle(h);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
std::wstring Upper(std::wstring text){for(auto&c:text)c=towupper(c);return text;}
std::wstring GuidText(const GUID& guid){wchar_t s[40];StringFromGUID2(guid,s,40);return s;}
const DEVPROPKEY BatteryKey={{0x104ea319,0x6ee2,0x4701,{0xbd,0x47,0x8d,0xdb,0xf4,0x25,0xbb,0xe5}},2};
const DEVPROPKEY BatteryTimeKey={{0x104ea319,0x6ee2,0x4701,{0xbd,0x47,0x8d,0xdb,0xf4,0x25,0xbb,0xe5}},7};
std::vector<BYTE> Property(HDEVINFO set,SP_DEVINFO_DATA& d,const DEVPROPKEY& key){
    BYTE buffer[2048]; DWORD needed=0; DEVPROPTYPE type=0;
    if(!SetupDiGetDevicePropertyW(set,&d,&key,&type,buffer,sizeof buffer,&needed,0)||needed>sizeof buffer)return {};
    return {buffer,buffer+needed};
}
std::wstring TextProperty(HDEVINFO set,SP_DEVINFO_DATA& d,const DEVPROPKEY& key){
    auto v=Property(set,d,key); if(v.size()<2)return L"";
    const auto* p=reinterpret_cast<const wchar_t*>(v.data()); size_t n=v.size()/2;
    size_t count=0;while(count<n&&p[count])count++;return std::wstring(p,count);
}
struct Root { Device device; bool ble=false; ULONGLONG address=0; };
std::vector<Root> ReadPnp(){
    HDEVINFO set=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_ALLCLASSES);
    if(set==INVALID_HANDLE_VALUE)return {};
    std::vector<Root> roots; std::map<std::wstring,std::pair<int,ULONGLONG>> values;
    for(DWORD i=0;;i++){
        SP_DEVINFO_DATA data{sizeof data}; if(!SetupDiEnumDeviceInfo(set,i,&data))break;
        wchar_t rawId[1024]{}; if(!SetupDiGetDeviceInstanceIdW(set,&data,rawId,1024,nullptr))continue;
        std::wstring id=Upper(rawId); if(id.rfind(L"BTH",0)!=0&&id.rfind(L"HID",0)!=0)continue;
        auto container=Property(set,data,DEVPKEY_Device_ContainerId);
        std::wstring key;if(container.size()==sizeof(GUID)){GUID guid;memcpy(&guid,container.data(),sizeof guid);key=GuidText(guid);}
        auto battery=Property(set,data,BatteryKey);
        DWORD percent=101;if(battery.size()==1)percent=battery[0];else if(battery.size()==4)memcpy(&percent,battery.data(),4);
        if(!key.empty()&&percent<=100){
            ULONGLONG time=0;auto v=Property(set,data,BatteryTimeKey);if(v.size()==8)memcpy(&time,v.data(),8);
            auto it=values.find(key);if(it==values.end()||it->second.second<time)values[key]={int(percent),time};
        }
        size_t prefix=0;bool ble=false;
        if(id.rfind(L"BTHLE\\DEV_",0)==0){prefix=10;ble=true;}
        else if(id.rfind(L"BTHENUM\\DEV_",0)==0)prefix=12;
        else continue;
        if(id.size()<prefix+13||id[prefix+12]!=L'\\')continue;
        std::wstring address=id.substr(prefix,12); wchar_t* end=nullptr;
        ULONGLONG number=wcstoull(address.c_str(),&end,16);if(!end||*end)continue;
        std::wstring name=TextProperty(set,data,DEVPKEY_Device_FriendlyName);
        if(name.empty())name=TextProperty(set,data,DEVPKEY_Device_DeviceDesc);
        Root root;root.ble=ble;root.address=number;root.device.key=key.empty()?address:key;root.device.name=name;
        root.device.address=address;root.device.transport=ble?L"BLE":L"经典蓝牙";roots.push_back(std::move(root));
    }
    SetupDiDestroyDeviceInfoList(set);
    for(auto&root:roots){auto v=values.find(root.device.key);if(v!=values.end()){root.device.battery=v->second.first;root.device.batteryTime=v->second.second;root.device.source=L"系统上报";}}
    return roots;
}

template<class Op> auto Complete(Op op,std::atomic<bool>& stop,DWORD timeout=1800)->decltype(op.GetResults()){
    auto start=GetTickCount64();
    try{
        while(op.Status()==AsyncStatus::Started){if(stop||GetTickCount64()-start>timeout){op.Cancel();throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));}Sleep(20);}
        auto result=op.GetResults();op.Close();return result;
    }catch(...){try{op.Close();}catch(...){}throw;}
}
struct BatteryState { std::atomic<int> value{-1};std::atomic<ULONGLONG> time{0}; };
struct Session {
    BluetoothLEDevice ble{nullptr};BluetoothDevice classic{nullptr};
    GattDeviceService service{nullptr};GattCharacteristic characteristic{nullptr};
    ULONGLONG lastOpen=0,lastRead=0;std::shared_ptr<BatteryState> state=std::make_shared<BatteryState>();
    winrt::event_token batteryToken{};bool subscribed=false;
    ~Session(){try{if(subscribed)characteristic.ValueChanged(batteryToken);if(service)service.Close();if(ble)ble.Close();if(classic)classic.Close();}catch(...){} }
};
struct HidInfo {
    std::wstring path,container,product,bus;
    USHORT vid=0,pid=0;HIDP_CAPS caps{};
    std::vector<std::pair<HIDP_REPORT_TYPE,HIDP_VALUE_CAPS>> fields;
};
std::wstring Bus(DWORD instance){
    for(int n=0;n<12;n++){wchar_t id[1024]{};CM_Get_Device_IDW(instance,id,1024,0);auto name=Upper(id);
        if(name.rfind(L"BTH",0)==0)return L"蓝牙";if(name.rfind(L"USB\\VID_",0)==0)return L"USB";
        DEVINST parent;if(CM_Get_Parent(&parent,instance,0)!=CR_SUCCESS)break;instance=parent;
    }return L"未知";
}
std::vector<HidInfo> ReadHids(){
    GUID guid;HidD_GetHidGuid(&guid);std::vector<HidInfo> result;
    HDEVINFO set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(set==INVALID_HANDLE_VALUE)return result;
    for(DWORD i=0;;i++){
        SP_DEVICE_INTERFACE_DATA in{sizeof in};if(!SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&in))break;
        DWORD required=0;SetupDiGetDeviceInterfaceDetailW(set,&in,nullptr,0,&required,nullptr);if(required>65536||required<8)continue;
        std::vector<BYTE> storage(required);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA device{sizeof device};if(!SetupDiGetDeviceInterfaceDetailW(set,&in,detail,required,nullptr,&device))continue;
        Handle h(CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));if(h.h==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES attr{sizeof attr};PHIDP_PREPARSED_DATA data=nullptr;
        if(!HidD_GetAttributes(h.h,&attr)||!HidD_GetPreparsedData(h.h,&data))continue;
        HidInfo info;info.path=detail->DevicePath;info.vid=attr.VendorID;info.pid=attr.ProductID;
        if(HidP_GetCaps(data,&info.caps)!=HIDP_STATUS_SUCCESS){HidD_FreePreparsedData(data);continue;}
        wchar_t name[256]{};if(HidD_GetProductString(h.h,name,sizeof name))info.product=name;
        auto key=Property(set,device,DEVPKEY_Device_ContainerId);if(key.size()==16){GUID c;memcpy(&c,key.data(),16);info.container=GuidText(c);}
        info.bus=Bus(device.DevInst);
        for(auto type:{HidP_Input,HidP_Feature}){
            USHORT count=type==HidP_Input?info.caps.NumberInputValueCaps:info.caps.NumberFeatureValueCaps;
            if(count>1024||count==0)continue;std::vector<HIDP_VALUE_CAPS> fields(count);
            if(HidP_GetValueCaps(type,fields.data(),&count,data)!=HIDP_STATUS_SUCCESS)continue;
            for(USHORT n=0;n<count;n++){
                auto f=fields[n];USHORT lo=f.IsRange?f.Range.UsageMin:f.NotRange.Usage,hi=f.IsRange?f.Range.UsageMax:lo;
                for(USHORT usage:{USHORT(0x20),USHORT(0x64)})if(((f.UsagePage==6&&usage==0x20)||(f.UsagePage==0x85&&usage==0x64))&&usage>=lo&&usage<=hi){f.NotRange.Usage=usage;f.IsRange=FALSE;info.fields.push_back({type,f});}
            }
        }
        HidD_FreePreparsedData(data);result.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(set);return result;
}
bool RapooChannel(const HidInfo& h){return h.vid==0x24ae&&h.pid==0x1235&&h.bus==L"USB"&&h.caps.UsagePage==0xff00&&h.caps.Usage==2&&h.caps.InputReportByteLength>=7&&h.caps.InputReportByteLength<=64;}
int DecodeHid(const HidInfo& info,PHIDP_PREPARSED_DATA data,const BYTE* report,ULONG length){
    if(!length)return -1;
    for(const auto&entry:info.fields){const auto&f=entry.second;if(entry.first!=HidP_Input||f.ReportID!=report[0])continue;
        ULONG value=0;if(HidP_GetUsageValue(HidP_Input,f.UsagePage,f.LinkCollection,f.NotRange.Usage,&value,data,reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)),length)==HIDP_STATUS_SUCCESS){int n=NormalizeBattery(f.LogicalMin,f.LogicalMax,value);if(n>=0)return n;}}
    return -1;
}
struct Watch {
    HidInfo info;Handle file,event;PHIDP_PREPARSED_DATA parsed=nullptr;OVERLAPPED overlapped{};
    std::array<BYTE,4096> buffer{};bool pending=false;int battery=-1;ULONGLONG time=0,received=0;
    explicit Watch(const HidInfo& h):info(h),file(CreateFileW(h.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr)),event(CreateEventW(nullptr,TRUE,FALSE,nullptr)){
        if(file.h!=INVALID_HANDLE_VALUE){HidD_GetPreparsedData(file.h,&parsed);overlapped.hEvent=event.h;}
    }
    ~Watch(){if(pending){CancelIoEx(file.h,&overlapped);DWORD done=0;GetOverlappedResult(file.h,&overlapped,&done,TRUE);}if(parsed)HidD_FreePreparsedData(parsed);}
    void Poll(){
        if(file.h==INVALID_HANDLE_VALUE||!event.h)return;
        for(int i=0;i<8;i++){
            DWORD done=0;
            if(pending){if(WaitForSingleObject(event.h,0)!=WAIT_OBJECT_0)return;pending=false;if(!GetOverlappedResult(file.h,&overlapped,&done,FALSE))return;}
            else{ResetEvent(event.h);if(!ReadFile(file.h,buffer.data(),std::min<size_t>(buffer.size(),info.caps.InputReportByteLength),&done,&overlapped)){
                if(GetLastError()==ERROR_IO_PENDING)pending=true;return;}}
            if(!done)return;received++;int n=RapooChannel(info)?RapooBattery(buffer.data(),done):DecodeHid(info,parsed,buffer.data(),done);
            if(n>=0){battery=n;time=Now();}
        }
    }
};
bool Transfer(HANDLE file,bool write,BYTE* data,DWORD length,DWORD timeout){
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=event.h;DWORD done=0;
    BOOL ok=write?WriteFile(file,data,length,&done,&ov):ReadFile(file,data,length,&done,&ov);
    if(!ok){if(GetLastError()!=ERROR_IO_PENDING)return false;
        if(WaitForSingleObject(event.h,timeout)!=WAIT_OBJECT_0){CancelIoEx(file,&ov);GetOverlappedResult(file,&ov,&done,TRUE);return false;}
        if(!GetOverlappedResult(file,&ov,&done,FALSE))return false;
    }return done==length;
}
// Only the two documented battery-read registers on the associated K380
// vendor interface are allowed; no keyboard configuration commands are sent.
std::vector<BYTE> K380Register(HANDLE file,const HidInfo&h,BYTE slot,BYTE reg){
    std::vector<BYTE> request(h.caps.OutputReportByteLength);request[0]=request.size()>=20?0x11:0x10;request[1]=slot;request[2]=0x81;request[3]=reg;
    if(!Transfer(file,true,request.data(),DWORD(request.size()),700))return {};
    auto begin=GetTickCount64();while(GetTickCount64()-begin<900){std::vector<BYTE> reply(h.caps.InputReportByteLength);
        if(!Transfer(file,false,reply.data(),DWORD(reply.size()),900-DWORD(GetTickCount64()-begin)))return {};
        if(reply.size()<7||(reply[0]!=0x10&&reply[0]!=0x11)||(reply[1]!=slot&&!(slot==0xff&&reply[1]==0)&&!(slot==0&&reply[1]==0xff)))continue;
        if(reply[2]==0x81&&reply[3]==reg)return {reply.begin()+4,reply.end()};
        if((reply[2]==0xff||reply[2]==0x8f)&&reply[3]==0x81&&reply[4]==reg)return {};
    }return {};
}
void ReadK380(Device&d,const std::vector<HidInfo>&hids){
    if(Upper(d.name).find(L"K380")==std::wstring::npos||d.connected!=1||d.battery>=0||!d.level.empty())return;
    // A declared standard battery field takes priority, including while waiting
    // for its first report, so that two readers do not compete on that channel.
    for(auto&h:hids)if(h.container==d.key&&!h.fields.empty())return;
    int count=0;for(auto&h:hids){if(h.container!=d.key||h.vid!=0x046d||h.caps.UsagePage!=0xff00||h.caps.InputReportByteLength<7||h.caps.InputReportByteLength>128||h.caps.OutputReportByteLength<7||h.caps.OutputReportByteLength>128)continue;
        if(count++>=2)break;Handle file(CreateFileW(h.path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));if(file.h==INVALID_HANDLE_VALUE)continue;
        for(BYTE slot:{BYTE(0),BYTE(0xff)}){auto v=K380Register(file.h,h,slot,0x0d);if(v.size()>=3&&v[0]<=100){d.battery=v[0];d.source=L"Logitech HID++ 电量";d.approximate=true;d.batteryTime=Now();return;}
            v=K380Register(file.h,h,slot,7);if(v.size()<2)continue;d.level=v[0]==7?L"充足":v[0]==5?L"良好":v[0]==3?L"偏低":v[0]==1?L"极低":L"";if(!d.level.empty()){d.source=L"Logitech HID++ 档位";d.batteryTime=Now();return;}}
    }
}
struct Receiver {
    Handle file;std::wstring name;int battery=-1,next=-1;BYTE feature=0,software=8;ULONGLONG time=0;
    explicit Receiver(const std::wstring&path):file(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr)){}
    std::vector<BYTE> Query(BYTE feat,BYTE function,std::initializer_list<BYTE> params){
        BYTE report[20]{};BYTE fid=(function<<4)|software;software=software>=15?8:software+1;
        report[0]=0x11;report[1]=1;report[2]=feat;report[3]=fid;std::copy(params.begin(),params.end(),report+4);
        if(!Transfer(file.h,true,report,20,700))return {};
        auto start=GetTickCount64();while(GetTickCount64()-start<900){BYTE reply[20]{};if(!Transfer(file.h,false,reply,20,900-DWORD(GetTickCount64()-start)))return {};
            if(reply[0]!=0x11||reply[1]!=1)continue;if(reply[2]==feat&&reply[3]==fid)return {reply+4,reply+20};
            if((reply[2]==0xff||reply[2]==0x8f)&&reply[3]==feat&&reply[4]==fid)return {};
        }return {};
    }
    BYTE Feature(USHORT id){auto v=Query(0,0,{BYTE(id>>8),BYTE(id),0});return v.empty()?0:v[0];}
    std::wstring ModelName() const { return LogitechModelName(name); }
    Device Poll(const std::wstring&key){
        Device d;d.key=key;d.name=name.empty()?L"罗技 LIGHTSPEED 设备":name;d.address=L"USB 046D:C53F · 槽 1";d.transport=L"2.4G 接收器";d.receiver=true;d.approximate=true;d.source=L"接收器上报";
        auto ping=Query(0,1,{0,0,0x6b});bool awake=ping.size()>=3&&ping[2]==0x6b&&ping[0]>=2;
        d.connected=awake?1:-1;if(!awake)d.detail=L"休眠或暂未响应";
        if(awake&&name.empty()){
            BYTE nf=Feature(5);if(nf){auto len=Query(nf,0,{});if(!len.empty()&&len[0]>0&&len[0]<=128){std::string text;
                while(text.size()<len[0]){auto part=Query(nf,1,{BYTE(text.size())});if(part.empty())break;size_t n=std::min<size_t>(part.size(),len[0]-text.size());text.append(reinterpret_cast<char*>(part.data()),n);}
                if(text.size()==len[0]){int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);name.resize(n);MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),name.data(),n);while(!name.empty()&&!name.back())name.pop_back();}
            }}
        }
        auto model=ModelName();
        // G304 X uses the same C53F LIGHTSPEED receiver as G304/G305, but
        // some firmware revisions do not return a readable product name.
        // Query the standard battery feature even when the name is missing so
        // the device can still be identified from its HID++ capability.
        if(awake){
            if(!feature)feature=Feature(0x1000);
            if(feature){auto v=Query(feature,0,{});int n=-1;if(LogitechBattery(v.data(),v.size(),n)){battery=n;time=Now();next=v.size()>1&&v[1]>=1&&v[1]<=100?v[1]:-1;}}
        }
        if(!model.empty())d.name=model;else if(battery>=0)d.name=L"罗技 LIGHTSPEED 鼠标";
        d.battery=battery;d.batteryTime=time;d.detail+=L"\n"+name+(next>0?L" · 下一报告档位 "+std::to_wstring(next)+L"%":L"");return d;
    }
};
}

std::wstring Device::BatteryText()const{return battery>=0?(approximate?L"约":L"")+std::to_wstring(battery)+L"%":level.empty()?L"—":level;}
std::wstring Device::StateText()const{return connected==1?L"已连接":connected==0?L"未连接":receiver?L"USB 已接入 · 在线状态未知":L"连接状态未知";}
std::wstring Device::Note()const{
    if(battery<0&&level.empty())return L"暂未读取到电量";
    std::wstring s=connected==1?source:L"历史电量";
    if(!batteryTime)return s+L" · 时间未知";auto now=Now();if(batteryTime>now)return s+L" · 时间异常";
    auto age=(now-batteryTime)/10000000;if(age<60)return s+L" · 刚刚";
    if(age<3600)return s+L" · "+std::to_wstring(age/60)+L" 分钟前";return s+L" · "+std::to_wstring(age/3600)+L" 小时前";
}
bool Current(const Device&d){return d.connected==1||d.receiver;}
int NormalizeBattery(int lo,int hi,ULONG value){if(lo<0||hi<=lo||value<ULONG(lo)||value>ULONG(hi))return -1;return int((double(value-lo)/(hi-lo))*100+0.5);}
int RapooBattery(const BYTE*r,size_t n){
    if(n==7&&r[0]==0xbb&&r[1]==0xb0&&(r[2]==1||r[2]==0x81)&&(r[5]==1||r[5]==2)&&r[6]<=100)return r[6];
    if(n>=19&&r[0]==7&&(r[1]&15)<=3&&r[2]<=15&&r[7]<=4&&r[8]<=100&&r[12]<=15){int dx=r[3]|r[4]<<8,dy=r[5]|r[6]<<8;if(dx>=100&&dx<=32000&&dy>=100&&dy<=32000)return r[8];}return -1;
}
bool LogitechBattery(const BYTE*r,size_t n,int& value){if(n<3||r[0]==0||r[0]>100)return false;value=r[0];return true;}
std::wstring LogitechModelName(const std::wstring&name){
    auto key=Upper(name);
    key.erase(std::remove_if(key.begin(),key.end(),[](wchar_t c){return c==L' '||c==L'\t'||c==L'-'||c==L'_';}),key.end());
    if(key.find(L"G304X")!=std::wstring::npos)return L"罗技 G304X";
    if(key.find(L"G305X")!=std::wstring::npos)return L"罗技 G305X";
    if(key.find(L"G304")!=std::wstring::npos)return L"罗技 G304";
    if(key.find(L"G305")!=std::wstring::npos)return L"罗技 G305";
    return L"";
}
std::wstring FormatMemory(ULONGLONG bytes){wchar_t text[64];swprintf(text,64,L"%.1f GB",double(bytes)/1073741824.0);return text;}
Resources ReadResources(){
    Resources r;static ULONGLONG previousIdle=0,previousTotal=0;FILETIME i,k,u;
    auto value=[](FILETIME t){return(ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    if(GetSystemTimes(&i,&k,&u)){ULONGLONG idle=value(i),total=value(k)+value(u);if(previousTotal&&total>previousTotal&&idle>=previousIdle)r.cpu=std::clamp(int(100.0*(1.0-double(idle-previousIdle)/(total-previousTotal))+0.5),0,100);previousTotal=total;previousIdle=idle;}
    MEMORYSTATUSEX m{sizeof m};if(GlobalMemoryStatusEx(&m)){r.total=m.ullTotalPhys;r.used=m.ullTotalPhys-m.ullAvailPhys;r.memory=int(100.0*r.used/r.total+0.5);}
    SYSTEM_POWER_STATUS power{};
    if(GetSystemPowerStatus(&power)){
        r.computerAc=power.ACLineStatus==1;
        r.computerBatteryPresent=power.BatteryFlag!=128;
        r.computerCharging=r.computerBatteryPresent&&power.BatteryFlag!=255&&(power.BatteryFlag&8)!=0;
        if(r.computerBatteryPresent&&power.BatteryLifePercent<=100)r.computerBattery=power.BatteryLifePercent;
    }
    PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof p;if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof p)){r.workingSet=p.WorkingSetSize;r.commit=p.PrivateUsage;}
    // Match Task Manager's process Memory column without changing residency.
    // Reuse the small query buffer and sample once per five seconds.
    static ULONGLONG last=0,privateBytes=0;static std::vector<ULONG_PTR> pages;
    auto now=GetTickCount64();if(!last||now-last>=5000){last=now;size_t capacity=size_t(r.workingSet/4096*2+8192);if(capacity>pages.size())pages.resize(capacity);
        if(QueryWorkingSet(GetCurrentProcess(),pages.data(),DWORD(pages.size()*sizeof(ULONG_PTR)))){auto info=reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(pages.data());privateBytes=0;for(ULONG_PTR n=0;n<info->NumberOfEntries;n++)if(!info->WorkingSetInfo[n].Shared)privateBytes+=4096;}}
    r.privateWorkingSet=privateBytes;return r;
}
std::vector<MonitorItem> MonitorItems(const Resources&r,const std::vector<Device>&devices,bool cpu,bool memory,bool battery,bool computerBattery){
    std::vector<MonitorItem> result;
    auto percent = [](int value) -> std::wstring { return value < 0 ? L"—" : std::to_wstring(value) + L"%"; };
    if(cpu) result.push_back({L"CPU", percent(r.cpu)});
    if(memory) result.push_back({L"内存", percent(r.memory)});
    if(computerBattery) result.push_back({L"电脑", r.computerBatteryPresent || r.computerBattery >= 0 ? percent(r.computerBattery) : L"无电池", r.computerBattery >= 0});
    if(battery){
        std::vector<const Device*> current;
        for(auto& d:devices) if(d.connected==1 && (d.battery>=0 || !d.level.empty())) current.push_back(&d);
        std::stable_sort(current.begin(),current.end(),[](auto*a,auto*b){return (a->battery<0?101:a->battery)<(b->battery<0?101:b->battery);});
        for(auto d:current){
            auto shortName = d->alias.empty() && d->name.starts_with(L"罗技 ") ? d->name.substr(3) : L"";
            result.push_back({d->DisplayName(),d->BatteryText(),true,std::move(shortName)});
        }
        if(current.empty()) result.push_back({L"电量",L"—"});
    }
    if(result.empty()) result.push_back({L"监控已关闭"});
    return result;
}
void WriteUtf8(const std::wstring&path,const std::wstring&text){
    int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),s.data(),n,nullptr,nullptr);
    Handle h(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(h.h==INVALID_HANDLE_VALUE)return;DWORD done;const BYTE bom[]={0xef,0xbb,0xbf};WriteFile(h.h,bom,3,&done,nullptr);WriteFile(h.h,s.data(),DWORD(s.size()),&done,nullptr);
}

struct Devices::Impl {
    std::atomic<bool>& stop;
    std::map<std::wstring,std::unique_ptr<Session>> sessions;
    std::map<std::wstring,std::unique_ptr<Watch>> watches;
    std::map<std::wstring,std::unique_ptr<Receiver>> receivers;
    std::map<std::wstring,Device> history;
    std::vector<HidInfo> hids;
    explicit Impl(std::atomic<bool>&s):stop(s){winrt::init_apartment(winrt::apartment_type::multi_threaded);}
    ~Impl(){sessions.clear();watches.clear();receivers.clear();winrt::clear_factory_cache();winrt::uninit_apartment();}
    void ReadBluetooth(Root& root){
        auto key=(root.ble?L"LE:":L"BT:")+root.device.address;auto&ptr=sessions[key];if(!ptr)ptr=std::make_unique<Session>();auto&s=*ptr;
        try{
            auto now=GetTickCount64();if(!s.ble&&!s.classic&&(!s.lastOpen||now-s.lastOpen>60000)){s.lastOpen=now;if(root.ble)s.ble=Complete(BluetoothLEDevice::FromBluetoothAddressAsync(root.address),stop);else s.classic=Complete(BluetoothDevice::FromBluetoothAddressAsync(root.address),stop);}
            if(s.ble)root.device.connected=s.ble.ConnectionStatus()==BluetoothConnectionStatus::Connected?1:0;
            if(s.classic)root.device.connected=s.classic.ConnectionStatus()==BluetoothConnectionStatus::Connected?1:0;
            if(s.ble&&root.device.connected==1&&(!s.lastRead||now-s.lastRead>=60000)){
                s.lastRead=now;
                if(!s.characteristic){auto services=Complete(s.ble.GetGattServicesForUuidAsync(GattServiceUuids::Battery(),BluetoothCacheMode::Uncached),stop,2200);
                    if(services.Status()==GattCommunicationStatus::Success&&services.Services().Size()>0){
                        s.service=services.Services().GetAt(0);for(uint32_t n=1;n<services.Services().Size();n++)services.Services().GetAt(n).Close();
                        auto chars=Complete(s.service.GetCharacteristicsForUuidAsync(GattCharacteristicUuids::BatteryLevel(),BluetoothCacheMode::Uncached),stop);
                        if(chars.Status()==GattCommunicationStatus::Success&&chars.Characteristics().Size()>0)s.characteristic=chars.Characteristics().GetAt(0);
                        else{s.service.Close();s.service=nullptr;}
                    }}
                if(s.characteristic){auto read=Complete(s.characteristic.ReadValueAsync(BluetoothCacheMode::Uncached),stop);
                    if(read.Status()==GattCommunicationStatus::Success&&read.Value().Length()>0){auto reader=DataReader::FromBuffer(read.Value());int v=reader.ReadByte();reader.Close();if(v<=100){s.state->value=v;s.state->time=Now();}}
                    if(!s.subscribed){auto props=s.characteristic.CharacteristicProperties();auto mode=(props&GattCharacteristicProperties::Notify)!=GattCharacteristicProperties::None?GattClientCharacteristicConfigurationDescriptorValue::Notify:(props&GattCharacteristicProperties::Indicate)!=GattCharacteristicProperties::None?GattClientCharacteristicConfigurationDescriptorValue::Indicate:GattClientCharacteristicConfigurationDescriptorValue::None;
                        if(mode!=GattClientCharacteristicConfigurationDescriptorValue::None){auto target=s.state;
                            s.batteryToken=s.characteristic.ValueChanged([target](const GattCharacteristic&,const GattValueChangedEventArgs&args){try{auto r=DataReader::FromBuffer(args.CharacteristicValue());if(r.UnconsumedBufferLength()){int n=r.ReadByte();if(n<=100){target->value=n;target->time=Now();}}r.Close();}catch(...){} });
                            s.subscribed=true;
                            try{auto status=Complete(s.characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(mode),stop);if(status!=GattCommunicationStatus::Success){s.characteristic.ValueChanged(s.batteryToken);s.subscribed=false;}}
                            catch(...){try{s.characteristic.ValueChanged(s.batteryToken);}catch(...){}s.subscribed=false;throw;}
                        }}
                }
            }
        }catch(...){root.device.detail=L"部分接口暂不可读，将自动重试";}
        auto stamp=s.state->time.load();int value=s.state->value.load();if(value>=0&&(root.device.batteryTime>Now()||stamp>=root.device.batteryTime)){root.device.battery=value;root.device.batteryTime=stamp;root.device.source=L"BLE 读取";}
    }
};
Devices::Devices(std::atomic<bool>&stop):impl(std::make_unique<Impl>(stop)){}
Devices::~Devices()=default;
std::vector<Device> Devices::Scan(){
    auto&b=*impl;auto roots=ReadPnp();std::set<std::wstring> active;std::vector<Device> devices;
    for(auto&root:roots){if(b.stop)break;b.ReadBluetooth(root);active.insert((root.ble?L"LE:":L"BT:")+root.device.address);
        auto found=std::find_if(devices.begin(),devices.end(),[&](auto&d){return d.key==root.device.key;});
        if(found==devices.end())devices.push_back(root.device);else{if(root.device.connected==1)found->connected=1;else if(root.device.connected<0&&found->connected==0)found->connected=-1;if(root.device.battery>=0&&root.device.batteryTime>=found->batteryTime){found->battery=root.device.battery;found->batteryTime=root.device.batteryTime;found->source=root.device.source;}}
    }
    for(auto it=b.sessions.begin();it!=b.sessions.end();)if(!active.count(it->first))it=b.sessions.erase(it);else++it;
    b.hids=ReadHids();std::set<std::wstring> watched,receivers;
    for(const auto&h:b.hids){
        bool input=std::any_of(h.fields.begin(),h.fields.end(),[](auto&f){return f.first==HidP_Input;});
        if((input||RapooChannel(h))&&h.caps.InputReportByteLength>=2&&h.caps.InputReportByteLength<=4096){watched.insert(h.path);auto&watch=b.watches[h.path];if(!watch)watch=std::make_unique<Watch>(h);watch->Poll();
            if(RapooChannel(h)){
                auto receiverKey = L"rapoo:" + (h.container.empty() ? h.path : h.container);
                auto it=std::find_if(devices.begin(),devices.end(),[&](auto&d){return (!h.container.empty() && d.key==h.container) || d.key==receiverKey;});
                if(it==devices.end()){Device d;d.key=receiverKey;d.name=L"雷柏 USB / 2.4G 接收器";d.address=L"USB 24AE:1235";d.transport=L"USB / 2.4G";d.receiver=true;devices.push_back(d);it=devices.end()-1;}
                if(watch->battery>=0){it->name=L"雷柏 2.4G 鼠标";it->battery=watch->battery;it->batteryTime=watch->time;it->source=L"雷柏状态上报";it->connected=Now()-watch->time<900000000?1:-1;}
            }
        }
        for(auto&d:devices)if(!h.container.empty()&&h.container==d.key&&d.connected==1&&d.battery<0){
            auto w=b.watches.find(h.path);if(w!=b.watches.end()&&w->second->battery>=0){d.battery=w->second->battery;d.batteryTime=w->second->time;d.source=L"标准 HID 电量";continue;}
            // Fetch only declared battery fields; input reports are also watched.
            Handle file(CreateFileW(h.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));if(file.h==INVALID_HANDLE_VALUE)continue;
            PHIDP_PREPARSED_DATA parsed=nullptr;if(!HidD_GetPreparsedData(file.h,&parsed))continue;
            for(auto&f:h.fields){USHORT length=f.first==HidP_Feature?h.caps.FeatureReportByteLength:h.caps.InputReportByteLength;if(length<2||length>4096)continue;std::vector<BYTE> report(length);report[0]=f.second.ReportID;
                bool ok=f.first==HidP_Feature?HidD_GetFeature(file.h,report.data(),ULONG(report.size())):HidD_GetInputReport(file.h,report.data(),ULONG(report.size()));
                if(ok){ULONG v=0;auto&cap=f.second;if(HidP_GetUsageValue(f.first,cap.UsagePage,cap.LinkCollection,cap.NotRange.Usage,&v,parsed,reinterpret_cast<PCHAR>(report.data()),ULONG(report.size()))==HIDP_STATUS_SUCCESS){int n=NormalizeBattery(cap.LogicalMin,cap.LogicalMax,v);if(n>=0){d.battery=n;d.batteryTime=Now();d.source=L"标准 HID 电量";break;}}}}
            HidD_FreePreparsedData(parsed);
        }
        if(h.vid==0x046d&&h.pid==0xc53f&&h.caps.UsagePage==0xff00&&h.caps.Usage==2&&h.caps.InputReportByteLength==20&&h.caps.OutputReportByteLength==20){
            receivers.insert(h.path);auto&r=b.receivers[h.path];if(!r)r=std::make_unique<Receiver>(h.path);auto d=r->Poll(L"logitech:"+h.path);b.history[h.path]=d;devices.push_back(std::move(d));
        }
    }
    for(auto&d:devices)ReadK380(d,b.hids);
    for(auto it=b.watches.begin();it!=b.watches.end();)if(!watched.count(it->first))it=b.watches.erase(it);else++it;
    for(auto it=b.receivers.begin();it!=b.receivers.end();)if(!receivers.count(it->first))it=b.receivers.erase(it);else++it;
    for(auto&entry:b.history)if(!receivers.count(entry.first)){Device d=entry.second;d.connected=0;d.receiver=false;d.detail=L"接收器未插入";devices.push_back(std::move(d));}
    std::stable_sort(devices.begin(),devices.end(),[](auto&a,auto&d){return (a.connected==1)>(d.connected==1);});return devices;
}
std::wstring Devices::Diagnostic(const std::vector<Device>&devices){
    std::wstring text=L"设备电量悬浮窗 2.1 原生版 · 本机诊断\r\n";
    for(auto&d:devices)text+=d.name+L" | "+d.StateText()+L" | "+d.BatteryText()+L" | "+d.transport+L" | "+d.Note()+L"\r\n";
    text+=L"\r\nHID 接口：\r\n";for(auto&h:impl->hids){wchar_t id[128];swprintf(id,128,L"VID_%04X PID_%04X · Usage %04X:%04X",h.vid,h.pid,h.caps.UsagePage,h.caps.Usage);text+=std::wstring(id)+L" | "+h.product+L" | "+h.bus+L" | 电量字段 "+std::to_wstring(h.fields.size());auto it=impl->watches.find(h.path);if(it!=impl->watches.end())text+=L" | 输入帧 "+std::to_wstring(it->second->received);text+=L"\r\n";}return text;
}

