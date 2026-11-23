param([string]$GameExe)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class FlowKeys {
    public struct Rect { public int left,top,right,bottom; }
    public struct Point { public int x,y; }
    [StructLayout(LayoutKind.Sequential)] struct Key { public ushort vk,scan; public uint flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Explicit,Size=40)] struct Input { [FieldOffset(0)] public uint type; [FieldOffset(8)] public Key key; }
    [DllImport("user32.dll")] static extern uint SendInput(uint n,Input[] inputs,int size);
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int key);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint owner);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref Point point);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point pt);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder name,int max);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("xinput1_4.dll")] public static extern uint XInputGetState(uint slot,IntPtr state);

    public static bool OwnsForeground(IntPtr h,int pid) {
        uint owner; var name=new StringBuilder(100);
        return h!=IntPtr.Zero && GetForegroundWindow()==h && GetWindowThreadProcessId(h,out owner)!=0 && owner==pid &&
            GetClassName(h,name,100)>0 && name.ToString()=="WindowClass";
    }
    public static void KeyEvent(IntPtr h,int pid,ushort scan,bool up) {
        if(!OwnsForeground(h,pid)) throw new InvalidOperationException("対象ゲームにフォーカスがないため入力を中断しました");
        if(!up) foreach(int vk in new[]{16,17,18,91,92}) if((GetAsyncKeyState(vk)&0x8000)!=0) throw new InvalidOperationException("修飾キーが押されているため中断しました");
        var key=new Key {scan=scan,flags=8u|(up?2u:0u)};
        if(SendInput(1,new[]{new Input {type=1,key=key}},40)!=1) throw new InvalidOperationException("Windowsが入力を受理しませんでした");
    }
    [StructLayout(LayoutKind.Sequential)] struct Mouse { public int dx,dy; public uint data,flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Explicit,Size=40)] struct MouseInput { [FieldOffset(0)] public uint type; [FieldOffset(8)] public Mouse mouse; }
    [DllImport("user32.dll", EntryPoint="SendInput")] static extern uint SendMouse(uint n,MouseInput[] inputs,int size);
    public static void MouseEvent(IntPtr h,int pid,bool up) {
        Point point;
        if(!OwnsForeground(h,pid) || !GetCursorPos(out point) || WindowFromPoint(point)!=h)
            throw new InvalidOperationException("ゲームウィンドウから外れたためマウス入力を中断しました");
        if(SendMouse(1,new[]{new MouseInput{mouse=new Mouse{flags=up?4u:2u}}},40)!=1)
            throw new InvalidOperationException("Windowsがマウス入力を受理しませんでした");
    }
}
'@
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$exe=Join-Path $repo 'LocalWork/Verification/ProductFlow/Products/SceneFlow/SceneFlow.exe'
if ($GameExe) { $exe=[IO.Path]::GetFullPath($GameExe) }
$run=Join-Path $repo ('LocalWork/Verification/ProductView/Keys-'+(Get-Date -Format yyyyMMdd-HHmmssfff))
[IO.Directory]::CreateDirectory($run)|Out-Null
$prev=[FlowKeys]::GetForegroundWindow()
[FlowKeys]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
$info=[Diagnostics.ProcessStartInfo]::new($exe)
$info.UseShellExecute=$false
$info.WorkingDirectory=[IO.Path]::GetDirectoryName($exe)
$info.Environment.Remove('NEM_PRODUCT_FLOW_REPORT')|Out-Null
$info.Environment.Remove('NEM_PRODUCT_COMPONENT_TEST')|Out-Null
$info.Environment.Remove('NEM_PRODUCT_RESTART')|Out-Null
$info.Environment['LOCALAPPDATA']=(Join-Path $run 'Local')
$report=Join-Path $run 'interactions.log'
$info.Environment['NEM_PRODUCT_INTERACTIONS_REPORT']=$report
$info.RedirectStandardOutput=$true
$info.RedirectStandardError=$true
$p=[Diagnostics.Process]::Start($info)
$output=$p.StandardOutput.ReadToEndAsync()
$errors=$p.StandardError.ReadToEndAsync()
$h=[IntPtr]::Zero
$original=[FlowKeys+Point]::new()
$cursor=[FlowKeys+Point]::new()
$moved=$false
try {
    $deadline=[DateTime]::Now.AddSeconds(50)
    do {
        Start-Sleep -Milliseconds 400; $p.Refresh()
    if ($p.HasExited) {throw 'サンプルが入力確認前に終了しました'}
        $h=$p.MainWindowHandle
    } while ($h -eq [IntPtr]::Zero -and [DateTime]::Now -lt $deadline)
    if ($h -eq [IntPtr]::Zero) {throw 'ゲームウィンドウが見つかりません'}
    [FlowKeys]::ShowWindow($h,9)|Out-Null
    [FlowKeys]::SetForegroundWindow($h)|Out-Null
    Start-Sleep -Seconds 7
    if (![FlowKeys]::OwnsForeground($h,$p.Id)) {
        [FlowKeys]::SetForegroundWindow($h)|Out-Null
        Start-Sleep -Milliseconds 150
        if (![FlowKeys]::OwnsForeground($h,$p.Id)) {throw 'ゲームにフォーカスを取得できません'}
    }
    function Save-Frame([string]$name) {
        if (![FlowKeys]::OwnsForeground($h,$p.Id)) {throw '入力テスト中に別のウィンドウへ切り替わりました'}
        $r=[FlowKeys+Rect]::new()
        if (![FlowKeys]::GetClientRect($h,[ref]$r)) {throw 'ゲーム画面のサイズが不明です'}
        $bmp=[Drawing.Bitmap]::new($r.right,$r.bottom)
        try {
            $g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc()
            try { if (![FlowKeys]::PrintWindow($h,$dc,3)) { throw 'ゲームの画面を取得できません' } }
            finally { $g.ReleaseHdc($dc);$g.Dispose() }
            $bmp.Save((Join-Path $run ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
        } finally { $bmp.Dispose() }
    }
    function Press([UInt16]$scan) {
        [FlowKeys]::KeyEvent($h,$p.Id,$scan,$false)
        Start-Sleep -Milliseconds 130
        [FlowKeys]::KeyEvent($h,$p.Id,$scan,$true)
        Start-Sleep -Milliseconds 850
    }
    function Set-GameCursor([int]$x,[int]$y) {
        $point=[FlowKeys+Point]::new()
        $point.x=$x
        $point.y=$y
        if (![FlowKeys]::ClientToScreen($h,[ref]$point) -or ![FlowKeys]::OwnsForeground($h,$p.Id) -or
            [FlowKeys]::WindowFromPoint($point) -ne $h -or ![FlowKeys]::SetCursorPos($point.x,$point.y)) {
            throw 'ゲームの操作位置を確認できません'
        }
        $point
    }
    if (![FlowKeys]::GetCursorPos([ref]$original)) {throw 'マウス位置を取得できません'}
    $cursor=Set-GameCursor 40 40
    $moved=$true

    function Wait-Report([string]$entry) {
        $limit=[DateTime]::UtcNow.AddSeconds(45)
        $lines=@()
        do {
            if ($p.HasExited) {throw "Game terminated before $entry"}
            if (Test-Path -LiteralPath $report) {
                $lines=[IO.File]::ReadAllLines($report)
                if (@($lines | Where-Object { $_.StartsWith('FAIL') }).Count) {throw ($lines -join ' / ')}
                if ($lines -contains $entry) {return}
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $limit)
        throw "Timeout: $entry / $($lines -join ',')"
    }
    Wait-Report 'UI ready'
    Save-Frame 'Selectable'
    Press 0x40
    Wait-Report 'UI navigated'
    Press 0x41
    Wait-Report 'UI right button submitted'
    $cursor=Set-GameCursor 160 260
    Start-Sleep -Milliseconds 300
    [FlowKeys]::MouseEvent($h,$p.Id,$false)
    Start-Sleep -Milliseconds 120
    [FlowKeys]::MouseEvent($h,$p.Id,$true)
    Wait-Report 'UI left button clicked'
    Wait-Report 'PASS'
    Get-Content -LiteralPath $report
    Save-Frame '1-Title'
    Press 0x0f
    Save-Frame '2-Tab'
    Press 0x1c
    Save-Frame '3-Enter'
    Press 0x39
    Save-Frame '4-Space'
    Press 0x1c
    Save-Frame '5-Enter'
    Press 0x1c
    Save-Frame '6-Enter'
    $frames=@('1-Title','2-Tab','3-Enter','4-Space','5-Enter','6-Enter') | ForEach-Object {
        (Get-FileHash -LiteralPath (Join-Path $run ($_+'.png'))).Hash
    }
    for ($index=1; $index -lt $frames.Count; ++$index) {
        if ($frames[$index-1] -eq $frames[$index]) {throw "画面が入力に応答しませんでした ($index)"}
    }
    $state=[Runtime.InteropServices.Marshal]::AllocHGlobal(32)
    try { foreach($i in 0..3) { Write-Output ("XInput {0}: {1}" -f $i,[FlowKeys]::XInputGetState($i,$state)) }}
    finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($state) }
    Write-Output $run
} finally {
    $current=[FlowKeys+Point]::new()
    if ($moved -and [FlowKeys]::GetCursorPos([ref]$current) -and $current.x -eq $cursor.x -and $current.y -eq $cursor.y) {
        [FlowKeys]::SetCursorPos($original.x,$original.y)|Out-Null
    }
    if (!$p.HasExited) {
        if ($h -ne [IntPtr]::Zero) { [FlowKeys]::PostMessage($h,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null }
        if (!$p.WaitForExit(5000)) {$p.Kill();$p.WaitForExit()}
    }
    [IO.File]::WriteAllText((Join-Path $run 'stdout.log'),$output.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $run 'stderr.log'),$errors.GetAwaiter().GetResult())
    $p.Dispose()
    if ($prev -ne [IntPtr]::Zero) {[FlowKeys]::SetForegroundWindow($prev)|Out-Null}
}
