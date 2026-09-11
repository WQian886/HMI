; 阶段 A · A1 Inno Setup 安装包
; 编译:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\hmi.iss
; 产出:installer\Output\hmi-setup-0.1.0.exe
; 前置:先用 windeployqt 收集 Qt 依赖到 install_root,然后调整 [Files] 段

[Setup]
AppName=HMI
AppVersion=0.1.0
AppPublisher=QianWei
AppPublisherURL=https://qianwei.com
DefaultDirName={autopf}\QianWei\HMI
DefaultGroupName=QianWei HMI
OutputBaseFilename=hmi-setup-0.1.0
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
Uninstallable=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "开机自动启动(--minimize)"; GroupDescription: "其他:"

[Files]
; TODO:实际打包时,先用 windeployqt 把依赖收集到 install_root\,然后改成:
;   Source: "..\install_root\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; 此处占位,Release 打包前替换
Source: "x64\Release\hmi.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\HMI"; Filename: "{app}\hmi.exe"
Name: "{group}\卸载 HMI"; Filename: "{uninstallexe}"
Name: "{autodesktop}\HMI"; Filename: "{app}\hmi.exe"; Tasks: desktopicon
Name: "{userstartup}\HMI"; Filename: "{app}\hmi.exe"; Parameters: "--minimize"; Tasks: autostart

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "QianWeiHMI"; ValueData: """{app}\hmi.exe"" --minimize"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\hmi.exe"; Description: "{cm:LaunchProgram,HMI}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 默认保留用户配置(工程文件 / 历史库);如需清理取消注释:
; Type: filesandordirs; Name: "{userappdata}\QianWei\HMI"
