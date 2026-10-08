; Inno Setup 6 script for the Voxwright installer.
;
; Built by CI (see .github/workflows/ci.yml, job "Windows installer"):
;   iscc /DAppVersion=0.1.0 /DSourceDir=<install tree> /DOutputDir=<dir> voxwright.iss
; SourceDir is the output of "cmake --install", which already holds the
; Qt runtime deployed by windeployqt.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must point at the installed application files
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6E3C2B9A-4F1D-4B7E-9A51-2C8D7F0E4A13}
AppName=Voxwright
AppVersion={#AppVersion}
AppVerName=Voxwright {#AppVersion}
AppPublisher=Voxwright
AppPublisherURL=https://github.com/Yousefz-z/Test
DefaultDirName={autopf}\Voxwright
DefaultGroupName=Voxwright
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=Voxwright-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\app\icons\voxwright.ico
UninstallDisplayIcon={app}\bin\Voxwright.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Voxwright"; Filename: "{app}\bin\Voxwright.exe"
Name: "{autodesktop}\Voxwright"; Filename: "{app}\bin\Voxwright.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\Voxwright.exe"; Description: "{cm:LaunchProgram,Voxwright}"; Flags: nowait postinstall skipifsilent

[Registry]
; Remove the sign-in entry on uninstall so Windows does not try to start a
; program that is gone. The app writes it only when the user asks.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Voxwright"; Flags: uninsdeletevalue dontcreatekey
