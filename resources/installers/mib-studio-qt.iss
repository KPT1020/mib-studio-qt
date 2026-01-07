; InnoSetup installer script for MIB Studio Qt
; This script packages the application with all dependencies and optionally installs egrabber

#define AppName "MIB Studio Qt"
#define AppVersion "0.1.0"
#define AppPublisher "MIB Studio"
#define AppURL "https://github.com/your-org/mib-studio-qt"
#define AppExeName "mib_studio_qt.exe"
#define MockExeName "mock_studio_qt.exe"
#define BuildDir "build\Release"
#define SourceDir "..\..\"
#define EgrabberInstaller "egrabber-win-x86_64-25.10.0.57.exe"

[Setup]
; App identification
AppId={{A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
LicenseFile=
OutputDir=build\dist
OutputBaseFilename=MIB_Studio_Qt_Setup_v{#AppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "installegrabber"; Description: "Install eGrabber SDK (required for camera functionality)"; GroupDescription: "Additional components"

[Files]
; Main executables
Source: "{#SourceDir}{#BuildDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}{#BuildDir}\{#MockExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Qt DLLs (deployed by windeployqt)
; Expected: Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Charts.dll, Qt6OpenGL.dll, Qt6OpenGLWidgets.dll
Source: "{#SourceDir}{#BuildDir}\Qt6*.dll"; DestDir: "{app}"; Flags: ignoreversion

; Third-party DLLs (deployed by windeployqt and CMake post-build)
; Expected: spdlog.dll, OpenCV DLLs, HDF5 DLLs, SQLite DLLs, codec DLLs (jpeg, tiff, webp, etc.)
; Also includes: XMT_DLL_SER.dll (Coremor), and other dependencies
Source: "{#SourceDir}{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; Qt plugins (critical: platforms/qwindows.dll must be present)
Source: "{#SourceDir}{#BuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}{#BuildDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}{#BuildDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}{#BuildDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}{#BuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs

; Data directory (logs, etc.)
; Note: This copies existing data files if present in build directory
Source: "{#SourceDir}{#BuildDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

; eGrabber installer (bundled but only run if user selects the task)
Source: "{#SourceDir}resources\installers\{#EgrabberInstaller}"; DestDir: "{tmp}"; Flags: deleteafterinstall; Tasks: installegrabber

[Dirs]
; Ensure data directory structure exists even if source data directory is empty
; This is critical for the application to write logs
Name: "{app}\data"; Flags: uninsneveruninstall
Name: "{app}\data\logs"; Flags: uninsneveruninstall

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{#AppName} (Mock Mode)"; Filename: "{app}\{#MockExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
; Run eGrabber installer if selected (silent mode)
Filename: "{tmp}\{#EgrabberInstaller}"; Parameters: "/S"; StatusMsg: "Installing eGrabber SDK..."; Tasks: installegrabber; Flags: runhidden waituntilterminated

[Code]

