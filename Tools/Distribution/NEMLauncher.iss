#ifndef LauncherVersion
  #error LauncherVersion is required
#endif
#ifndef PublishDirectory
  #error PublishDirectory is required
#endif
#ifndef SetupDirectory
  #error SetupDirectory is required
#endif
[Setup]
AppId={{10222A70-8CB4-4542-88D4-FEB797A13A55}
AppName=NEMLauncher
AppVersion={#LauncherVersion}
AppPublisher=NEMEngine
DefaultDirName={localappdata}\Programs\NEMLauncher
DefaultGroupName=NEMLauncher
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#SetupDirectory}
OutputBaseFilename=NEMLauncherSetup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=yes
UninstallDisplayIcon={app}\NEMLauncher.exe
UninstallFilesDir={app}\Uninstall
ChangesAssociations=yes
[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
[Files]
Source: "{#PublishDirectory}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\NEMLauncher"; Filename: "{app}\NEMLauncher.exe"
[Registry]
Root: HKCU; Subkey: "Software\Classes\.nemproject"; ValueType: string; ValueData: "NEMEngine.Project"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.nemproject\OpenWithProgids"; ValueType: string; ValueName: "NEMEngine.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\NEMEngine.Project"; ValueType: string; ValueData: "NEMEngineプロジェクト"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\NEMEngine.Project\DefaultIcon"; ValueType: string; ValueData: "{app}\NEMLauncher.exe,0"
Root: HKCU; Subkey: "Software\Classes\NEMEngine.Project\shell\open\command"; ValueType: string; ValueData: """{app}\NEMLauncher.exe"" --project ""%1"""
[Run]
Filename: "{app}\NEMLauncher.exe"; Description: "NEMLauncherを起動"; Flags: nowait postinstall skipifsilent

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Command: String;
  ExitCode: Integer;
begin
  Result := '';
  // 旧形式の所有ファイルは旧アンインストーラーで整理する
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{10222A70-8CB4-4542-88D4-FEB797A13A55}_is1', 'UninstallString', Command) then
  begin
    Command := RemoveQuotes(Command);
    if CompareText(ExtractFileDir(Command), ExpandConstant('{app}')) = 0 then
    begin
      if not Exec(Command, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
        Result := '旧ランチャーを整理できません ランチャーを終了して再試行してください'
      else if ExitCode <> 0 then
        Result := '旧ランチャーの整理に失敗しました コード: ' + IntToStr(ExitCode);
    end;
  end;
end;
