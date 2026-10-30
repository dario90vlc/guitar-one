; ============================================================================
;  Instalador de GUITAR ONE (Inno Setup 6).
;
;  Compilar:  "_mkdist.ps1"  o  ISCC.exe _installer.iss
;  Salida:    dist\GuitarAmp-Setup-<ver>.exe
;
;  Va por usuario (PrivilegesRequired=lowest) y no a Program Files a proposito:
;  la app guarda su GuitarAmp.ini, los presets de usuario y las grabaciones
;  AL LADO del .exe. En Program Files esa carpeta es de solo lectura para un
;  usuario normal y no se podria guardar nada.
;
;  El .exe ya lleva dentro los 26 PNG del arte (RCDATA), asi que el instalador
;  no necesita copiar amps_frontales\: la app funciona con el .exe solo.
; ============================================================================

#define AppName    "GUITAR ONE"
#define AppVersion "1.0.1"
#define AppExe     "GuitarAmp.exe"
#define AppUrl     "https://github.com/dario90vlc/guitar-one"

[Setup]
AppId={{8F3C1B7A-2D4E-4C9A-9E61-5A7B0D2C4F18}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=GuitarAmp
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases/latest
VersionInfoVersion=1.0.1.0
VersionInfoDescription=Instalador de {#AppName}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=LICENSE
OutputDir=dist
OutputBaseFilename=GuitarAmp-Setup-{#AppVersion}
SetupIconFile=native\GuitarAmp.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "native\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; DestName: "LEEME.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Solo los ajustes. Grabaciones\ se queda: son ficheros del usuario.
Type: files; Name: "{app}\GuitarAmp.ini"
