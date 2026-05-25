#define AppName GetEnv("UIL_APP_NAME")
#define AppDisplayName GetEnv("UIL_APP_DISPLAY_NAME")
#define AppPublisher GetEnv("UIL_APP_PUBLISHER")
#define AppVersion GetEnv("UIL_VERSION")
#define ExeName GetEnv("UIL_EXE_NAME")
#define SourceDir GetEnv("UIL_STAGE_DIR")
#define OutputDir GetEnv("UIL_OUTPUT_DIR")
#define OutputBaseFilename GetEnv("UIL_OUTPUT_BASE")

[Setup]
AppId={{6D59B16D-C1B8-482E-B190-7022D4A414B1}
AppName={#AppDisplayName}
AppVersion={#AppVersion}
AppVerName={#AppDisplayName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppDisplayName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\{#ExeName}
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
MinVersion=10.0
VersionInfoDescription={#AppDisplayName} Windows Installer
VersionInfoProductName={#AppDisplayName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppDisplayName}"; Filename: "{app}\{#ExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppDisplayName}"; Filename: "{app}\{#ExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ExeName}"; Description: "{cm:LaunchProgram,{#AppDisplayName}}"; Flags: nowait postinstall skipifsilent
