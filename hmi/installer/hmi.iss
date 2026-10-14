; 阶段 I · Inno Setup 安装包
; 编译: .\installer\package.ps1  (会自动跑 Release 构建 + windeployqt,再调 ISCC)
;      或手工:"<Inno Setup 6>\ISCC.exe" installer\hmi.iss
; 产出:installer\Output\hmi-setup-<AppVersion>.exe
;
; ★ [Files] 指向的 install_root\ 是 windeployqt 的产物,
;   不是 hmi.exe 单个文件 —— Qt 少一个 DLL/插件,程序在别人机器上就起不来。
;   手工重跑这一步:
;     D:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe ^
;       --release --no-translations ^
;       --dir installer\install_root ^
;       out\build\release\hmi.exe

[Setup]
AppName=HMI
; ★ 0.2.0:第一次把 install_root 接进 [Files]。
;   在此之前这一行是占位的 "x64\Release\hmi.exe" —— 那个路径不存在,
;   就算装了 Inno Setup 编出来也只有一个裸 exe,拷到别的机器上一运行就
;   报缺 Qt6Core.dll。"看起来能打包"和"打包出来能用"是两件事。
AppVersion=0.2.0
AppPublisher=QianWei
AppPublisherURL=https://qianwei.com
DefaultDirName={autopf}\QianWei\HMI
DefaultGroupName=QianWei HMI
OutputBaseFilename=hmi-setup-0.2.0
OutputDir=Output
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
; ★ install_root\ 是 windeployqt 收好的 Qt 运行时 + 插件树(hmi.exe 在里面)。
;   recursesubdirs/createallsubdirs 是必须的:platforms\、styles\、
;   sqldrivers\、imageformats\、tls\、iconengines\、canbus\、
;   generic\、networkinformation\ 全在子目录里,平铺过去 Qt 找不到插件。
;   没有 --no-translations 的话还会多带 20 来 MB 语言文件,这里不要。
;   (写成一行而不是用 "\" 续行:续行能过,但这一行本来就该一眼读完)
Source: "install_root\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

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
