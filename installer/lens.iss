; Lens, packaged. windeployqt lays the Qt runtime out beside the executable and Inno Setup packs
; that, plus data/, into one installer:
;
;     cmake --build build-ninja-release --target installer
;
; Run it in the release tree (scripts\build\build-release.bat): the payload below is named by tree, so
; from the debug tree the deploy would land there and this script would pack the release tree
; anyway. The number of the version lives in project() alone -- CMake generates the include below
; from it, so a release bumps one place.
;
; Two things the installer deliberately does not do.
;
; It writes no Run entry for autostart. The app writes that itself, from its own path, when the
; reader turns the switch on (src/app/autostart.cpp); an entry naming this tree would start the
; wrong copy, and one written here would be a second thing claiming to own the setting. The
; uninstaller removes that entry when it names the copy being removed.
;
; It does not carry the settings document, and it does not remove it on uninstall. It lives in
; %APPDATA%\Lens and holds the reader's API key and word marks (src/app/main.cpp); uninstalling
; the program must not take the reader's data with it.

#include "version.iss"

; SourcePath is the directory of this script, with a trailing backslash. The release tree name
; is scripts\build\build-release.bat's and is written here rather than passed in: this file is in the
; repository, and the repository knows its own layout.
#define PayloadDir SourcePath + "..\build-ninja-release\installer\payload"
#define DataDir SourcePath + "..\data"

[Setup]
AppId={{60F0699D-0B49-4E0C-B700-4497793B55E8}
AppName=Lens
AppVersion={#AppVersion}
AppVerName=Lens {#AppVersion}
DefaultDirName={autopf}\Lens
DefaultGroupName=Lens
DisableProgramGroupPage=yes
; Per-user, so no elevation: the program goes to %LOCALAPPDATA%\Programs\Lens and the shortcut
; to the reader's own Start Menu. One reader's tool, and the Run entry autostart writes is
; per-user too; nothing here needs an administrator's rights or a machine-wide location.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
OutputDir=..\build-ninja-release\installer
OutputBaseFilename=Lens-{#AppVersion}-setup
SetupIconFile=..\data\icons\lens.ico
UninstallDisplayIcon={app}\lens.exe
Compression=lzma2/max
SolidCompression=yes
; An upgrade closes a running Lens before its files are replaced. RestartApplications is off --
; it is on by default -- so the closed process is not started again behind the reader's back;
; a silent install must not leave a process running that was not running when it began.
CloseApplications=yes
RestartApplications=no

[Files]
; The payload is exactly what windeployqt wrote plus lens.exe: a directory of its own, so this
; glob cannot reach the build tree's object files, and the size the package report measures is
; the size the reader gets.
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#DataDir}\*"; DestDir: "{app}\data"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; WorkingDir is named rather than left to the default: the app writes its rotating log to logs/
; below the directory it is started in (src/util/log.cpp), and a shortcut that does not say which
; that is leaves the file wherever the shell happened to choose.
Name: "{group}\Lens"; Filename: "{app}\lens.exe"; WorkingDir: "{app}"

[UninstallDelete]
; The log directory the app creates at run time. It is not in the [Files] list, so nothing else
; would remove it and an uninstall would leave it behind.
Type: filesandordirs; Name: "{app}\logs"
; The plugin directories windeployqt lays out. Inno removes directories it created, but an
; install laid over an earlier version does not own those that were already there, and an
; upgrade-then-uninstall would leave them empty. dirifempty removes an empty directory only,
; so a file the reader put here stays.
Type: dirifempty; Name: "{app}\generic"
Type: dirifempty; Name: "{app}\iconengines"
Type: dirifempty; Name: "{app}\imageformats"
Type: dirifempty; Name: "{app}\networkinformation"
Type: dirifempty; Name: "{app}\platforms"
Type: dirifempty; Name: "{app}\qml"
Type: dirifempty; Name: "{app}\styles"
Type: dirifempty; Name: "{app}\texttospeech"
Type: dirifempty; Name: "{app}\tls"

[Run]
Filename: "{app}\lens.exe"; Description: "{cm:LaunchProgram,Lens}"; Flags: nowait postinstall skipifsilent

[Code]
const
  LensBackground = $00EFEBE9;
  LensPanel = $00FFFFFF;
  LensText = $001F1A1A;
  LensMuted = $006F6666;
  LensRunningMutex = 'Lens.Desktop.Running';

procedure RemoveRunEntry;
var
  Command: string;
begin
  if not RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Lens', Command) then
    exit;
  { The app writes native separators, but QSettings has stored this value with forward slashes;
    normalize both before comparing. }
  StringChangeEx(Command, '/', '\', True);
  if CompareText(ExpandFileName(RemoveQuotes(Command)), ExpandFileName(ExpandConstant('{app}\lens.exe'))) <> 0 then
    exit;
  RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Lens');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usUninstall then
    exit;
  { Windows will not delete a running executable's image. Refuse before the uninstaller touches
    anything rather than leave a half-removed install behind. The mutex name is the app's; see
    src/app/platform/running_instance.h. }
  if CheckForMutexes(LensRunningMutex) then begin
    SuppressibleMsgBox('Lens is still running. Quit it from the tray menu, then run the uninstaller again.', mbError, MB_OK, IDOK);
    Abort;
  end;
  RemoveRunEntry;
end;

procedure StyleLabel(const ALabel: TNewStaticText; const ASize: Integer; const AColor: TColor);
begin
  ALabel.Font.Name := 'Segoe UI Variable';
  ALabel.Font.Size := ASize;
  ALabel.Font.Color := AColor;
end;

procedure InitializeWizard;
begin
  { Keep the wizard in the same restrained neutral visual language as Lens. }
  WizardForm.Font.Name := 'Segoe UI Variable';
  WizardForm.Font.Size := 9;
  WizardForm.Font.Color := LensText;
  WizardForm.Color := LensBackground;
  WizardForm.MainPanel.Color := LensPanel;
  WizardForm.InnerPage.Color := LensBackground;

  StyleLabel(WizardForm.PageNameLabel, 14, LensText);
  StyleLabel(WizardForm.PageDescriptionLabel, 9, LensMuted);
  StyleLabel(WizardForm.WelcomeLabel1, 14, LensText);
  StyleLabel(WizardForm.WelcomeLabel2, 9, LensMuted);
  StyleLabel(WizardForm.FinishedHeadingLabel, 14, LensText);
  StyleLabel(WizardForm.FinishedLabel, 9, LensMuted);
  StyleLabel(WizardForm.StatusLabel, 9, LensText);
  StyleLabel(WizardForm.FilenameLabel, 9, LensMuted);

  WizardForm.NextButton.Font.Name := 'Segoe UI Variable';
  WizardForm.NextButton.Font.Size := 9;
  WizardForm.BackButton.Font.Name := 'Segoe UI Variable';
  WizardForm.BackButton.Font.Size := 9;
  WizardForm.CancelButton.Font.Name := 'Segoe UI Variable';
  WizardForm.CancelButton.Font.Size := 9;

end;
