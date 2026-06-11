; NCD Windows Installer — Inno Setup 6+
; Build with: iscc NCD.iss

#define MyAppName "NewChangeDirectory"
#define MyAppShortName "NCD"
#define MyAppVersion "1.5.0"
#define MyAppPublisher "NCD Project"
#define MyAppURL "https://github.com/your-org/ncd"

[Setup]
AppId={{NCD-7D3F9A2E-1A2B-4C5D-6E7F-8A9B0C1D2E3F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\NCD
DisableDirPage=no
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequiredOverridesAllowed=commandline dialog
OutputDir=..\..\dist
OutputBaseFilename=NCD-{#MyAppVersion}-setup
SetupIconFile=..
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add to PATH"; GroupDescription: "Integration:"
Name: "startup"; Description: "Start NCD Service at logon"; GroupDescription: "Integration:"
Name: "installservice"; Description: "Install NCD Service (requires Admin)"; GroupDescription: "Service:"; Check: IsAdmin

[Files]
Source: "..\..\src\ncd\NewChangeDirectory.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\src\ncd\NCDService.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\ncd.bat"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\ncd_service.bat"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\completions\ncd.bash"; DestDir: "{app}\completions"; Flags: ignoreversion
Source: "..\..\completions\_ncd"; DestDir: "{app}\completions"; Flags: ignoreversion
Source: "..\..\completions\ncd.ps1"; DestDir: "{app}\completions"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; Per-user startup (HKCU Run)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "NCDService"; \
    ValueData: """{app}\bin\NCDService.exe"" start"; \
    Tasks: startup; Check: not IsAdmin

; System-wide startup (HKLM Run)  
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "NCDService"; \
    ValueData: """{app}\bin\NCDService.exe"" start"; \
    Tasks: startup; Check: IsAdmin

; Add to PATH (User)
Root: HKCU; Subkey: "Environment"; \
    ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\bin"; \
    Tasks: addtopath; Check: not IsAdmin

; Add to PATH (System)
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\bin"; \
    Tasks: addtopath; Check: IsAdmin

[Run]
Filename: "{app}\bin\NCDService.exe"; Parameters: "start"; \
    Description: "Start NCD Service"; Flags: postinstall skipifsilent nowait; \
    Tasks: installservice

[UninstallRun]
Filename: "{app}\bin\NCDService.exe"; Parameters: "stop"; RunOnceId: "StopNCDService"

[Code]
function InitializeSetup(): Boolean;
begin
    Result := True;
end;
