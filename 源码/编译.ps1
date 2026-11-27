param([string]$ZigPath='', [string]$WinRtInclude='', [string]$OutputPath='')
$ErrorActionPreference='Stop'
if(-not $ZigPath) { $ZigPath=Join-Path $PSScriptRoot '..\..\..\work\native-tools\package\zig.exe' }
if(-not $WinRtInclude) { $WinRtInclude=Join-Path $PSScriptRoot '..\..\..\work\native-tools\sdk\cppwinrt' }
if(-not (Test-Path -LiteralPath $ZigPath)) { throw '请用 -ZigPath 指定 Zig C++ 编译器' }
if(-not (Test-Path -LiteralPath $WinRtInclude)) { throw '请用 -WinRtInclude 指定 Windows SDK 的 cppwinrt 头文件目录' }
$output=if ($OutputPath) { [IO.Path]::GetFullPath($OutputPath) } else { Join-Path (Split-Path $PSScriptRoot -Parent) 'BlueBatteryFloat.exe' }
$resource=Join-Path ([IO.Path]::GetTempPath()) ('BlueBatteryFloat-'+[guid]::NewGuid().ToString('N')+'.res')
Push-Location $PSScriptRoot
try { & $ZigPath rc /c 65001 /fo $resource 'App.rc'; if($LASTEXITCODE -ne 0) { throw '资源编译失败' } } finally { Pop-Location }
$arguments=@('c++',(Join-Path $PSScriptRoot 'NativeMain.cpp'),(Join-Path $PSScriptRoot 'Devices.cpp'),(Join-Path $PSScriptRoot 'Capsule.cpp'),(Join-Path $PSScriptRoot 'Network.cpp'),'-isystem',$WinRtInclude,'-target','x86_64-windows-gnu','-std=c++20','-fms-extensions','-mcx16','-Wno-ignored-attributes','-Wno-nonportable-include-path','-Wno-implicit-function-declaration','-O2','-s','-municode','-Wl,--subsystem,windows','-liphlpapi','-lsetupapi','-lhid','-lcfgmgr32','-lcomctl32','-lshell32','-lcomdlg32','-lgdiplus','-lgdi32','-luser32','-ladvapi32','-limm32','-lpsapi','-lole32','-loleaut32','-lapi-ms-win-core-winrt-l1-1-0','-lapi-ms-win-core-winrt-string-l1-1-0','-lapi-ms-win-core-winrt-error-l1-1-1','-o',$output)
try { & $ZigPath ($arguments + @($resource)); if($LASTEXITCODE -ne 0) { throw '原生版编译失败' } } finally { Remove-Item -LiteralPath $resource -ErrorAction SilentlyContinue }
Remove-Item -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'NativeMain.lib') -ErrorAction SilentlyContinue
Write-Output $output




