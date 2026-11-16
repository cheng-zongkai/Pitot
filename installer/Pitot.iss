; Pitot 安装包（Inno Setup 6，D-091）。由 scripts\package.ps1 编译，它传入：
;   AppVersion     发布版本号，例如 0.1.0 或 0.1.0-dev
;   VersionNumber  纯数字版本号，写进 setup.exe 的文件版本
;   StageDir       要安装的文件，和 zip 的内容一样（可能含 PitotAdlx.dll 和它的 EULA）
;   OutputDir、OutputName  输出目录和不带扩展名的文件名
; 本文件存为带 BOM 的 UTF-8：没有 BOM 时 ISCC 按 ANSI 读，中文消息会乱码

#ifndef AppVersion
  #error AppVersion is not defined. Build the installer with scripts\package.ps1.
#endif

[Setup]
; AppId 永远不能改：升级和卸载靠它找到已经装好的那一份
AppId={{4F966455-5453-476E-BBBA-C2B9E7FED4A9}
AppName=Pitot
AppVersion={#AppVersion}
AppVerName=Pitot {#AppVersion}
AppPublisher=kyzon
AppPublisherURL=https://github.com/cheng-zongkai/Pitot
AppSupportURL=https://github.com/cheng-zongkai/Pitot/issues
AppUpdatesURL=https://github.com/cheng-zongkai/Pitot/releases
VersionInfoVersion={#VersionNumber}
DefaultDirName={autopf}\Pitot
DisableProgramGroupPage=yes
; Pitot 每次运行都要管理员权限（D-002），装进 Program Files 也要
PrivilegesRequired=admin
; 只装在 x64 Windows 上：ARM64 不在支持范围内，x64compatible 会把它也算进来
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile=..\src\app\pitot.ico
UninstallDisplayIcon={app}\Pitot.exe
UninstallDisplayName=Pitot
WizardStyle=modern
ShowLanguageDialog=auto
Compression=lzma2/max
SolidCompression=yes
; 正在运行的 Pitot 由 [Code] 发 WM_CLOSE 关掉，让它先把流量写盘；重启管理器会直接结束进程
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
; 非官方翻译，scripts\get-innosetup.ps1 下载到编译器目录
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
english.PitotStillRunning=Pitot is still running and could not be closed. Right-click the taskbar strip, choose Exit Pitot, then try again.
chinesesimplified.PitotStillRunning=Pitot 仍在运行，没能自动关闭。请右键单击任务栏条，选择“退出 Pitot”，然后重试。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; 新的安装包不一定带 PitotAdlx.dll（发布流程先产出不带它的版本），不能留下旧版本的 DLL 和协议
Type: files; Name: "{app}\PitotAdlx.dll"
Type: files; Name: "{app}\PitotAdlx-EULA.txt"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Pitot"; Filename: "{app}\Pitot.exe"
Name: "{autodesktop}\Pitot"; Filename: "{app}\Pitot.exe"; Tasks: desktopicon

[Run]
; Pitot.exe 的清单要求管理员权限：用安装程序自己的权限启动，以原用户身份 CreateProcess 会失败
Filename: "{app}\Pitot.exe"; Description: "{cm:LaunchProgram,Pitot}"; Flags: nowait postinstall skipifsilent runascurrentuser

[Code]
const
  WM_CLOSE = $0010;
  // 与 src/common/names.h 保持一致
  MainWindowClass = 'Pitot.Main';
  SettingsWindowClass = 'Pitot.Settings';
  // Pitot.exe 的各个进程把这些互斥量一直持有到退出，全部释放才说明 exe 不再被占用
  PitotMutexes = 'Local\Pitot.SingleInstance,Local\Pitot.Settings,Local\Pitot.PawnIoInstall';
  AutostartTask = '\Pitot\Autostart';

procedure PostClose(const ClassName: String);
var
  Wnd: HWND;
begin
  Wnd := FindWindowByClassName(ClassName);
  if Wnd <> 0 then
    PostMessage(Wnd, WM_CLOSE, 0, 0);
end;

function ClosePitot(): Boolean;
var
  I: Integer;
begin
  Result := not CheckForMutexes(PitotMutexes);
  if Result then
    Exit;
  PostClose(MainWindowClass);
  PostClose(SettingsWindowClass);
  for I := 1 to 50 do
  begin
    Sleep(200);
    if not CheckForMutexes(PitotMutexes) then
    begin
      // 互斥量在 wWinMain 返回时释放，进程随后才真正结束、放开 exe 文件
      Sleep(500);
      Result := True;
      Exit;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not ClosePitot() then
    Result := CustomMessage('PitotStillRunning');
end;

// 只删除指向本次安装目录的自启任务：用户可能把自启改成了别处的便携版
procedure RemoveAutostart();
var
  ResultCode, I: Integer;
  Output: TExecOutput;
  ExePath: String;
begin
  ExePath := Lowercase(ExpandConstant('{app}\Pitot.exe'));
  if not ExecAndCaptureOutput(ExpandConstant('{sys}\schtasks.exe'), '/Query /TN "' + AutostartTask + '" /V /FO LIST', '',
                              SW_HIDE, ewWaitUntilTerminated, ResultCode, Output) or (ResultCode <> 0) then
    Exit;
  for I := 0 to GetArrayLength(Output.StdOut) - 1 do
    if Pos(ExePath, Lowercase(Output.StdOut[I])) > 0 then
    begin
      Exec(ExpandConstant('{sys}\schtasks.exe'), '/Delete /TN "' + AutostartTask + '" /F', '', SW_HIDE,
           ewWaitUntilTerminated, ResultCode);
      Exit;
    end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := ClosePitot();
  if not Result then
    SuppressibleMsgBox(CustomMessage('PitotStillRunning'), mbError, MB_OK, IDOK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveAutostart();
end;
