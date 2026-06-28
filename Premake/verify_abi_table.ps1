# C++ ManagedNativeApiTable と C# NativeApiTable の関数ポインタ列が一致するか検査する
# 手動ミラーのdrift(追加/削除/並べ替え/改名)をbuild前に失敗させるための整合チェック
param(
    [Parameter(Mandatory = $true)][string]$NativeHeader,
    [Parameter(Mandatory = $true)][string]$CsFile
)

$ErrorActionPreference = 'Stop'

# C++ ManagedNativeApiTable から <Type> <name> = nullptr; のフィールド名を順序通りに抜き出す
function Get-NativeFields([string]$path) {
    $fields = New-Object System.Collections.Generic.List[string]
    $inStruct = $false
    foreach ($line in (Get-Content -LiteralPath $path -Encoding UTF8)) {
        if (-not $inStruct) {
            if ($line -match 'struct\s+ManagedNativeApiTable\s*\{') {
                $inStruct = $true
            }
            continue
        }
        if ($line -match '^\s*\};') {
            break
        }
        if ($line -match '^\s*\w+\s+(\w+)\s*=\s*nullptr\s*;') {
            $fields.Add($Matches[1])
        }
    }
    return $fields
}

# C# NativeApiTable から public delegate* unmanaged[Cdecl]<...> name; のフィールド名を順序通りに抜き出す
function Get-CsFields([string]$path) {
    $fields = New-Object System.Collections.Generic.List[string]
    $inStruct = $false
    foreach ($line in (Get-Content -LiteralPath $path -Encoding UTF8)) {
        if (-not $inStruct) {
            if ($line -match 'struct\s+NativeApiTable\s*\{') {
                $inStruct = $true
            }
            continue
        }
        if ($line -match '^\s*\}') {
            break
        }
        if ($line -match 'delegate\*\s+unmanaged\[Cdecl\]<.*>\s+(\w+)\s*;') {
            $fields.Add($Matches[1])
        }
    }
    return $fields
}

$nativeFields = Get-NativeFields $NativeHeader
$csFields = Get-CsFields $CsFile

# 抽出に失敗した場合も検査が無意味になるため失敗扱いにする
if ($nativeFields.Count -eq 0 -or $csFields.Count -eq 0) {
    Write-Output "[ERROR] ABI table fields could not be extracted (C++=$($nativeFields.Count), C#=$($csFields.Count)). Check struct names or formatting."
    exit 1
}

if ($nativeFields.Count -ne $csFields.Count) {
    Write-Output "[ERROR] ABI table function pointer count mismatch: C++ ManagedNativeApiTable=$($nativeFields.Count), C# NativeApiTable=$($csFields.Count)."
    $max = [Math]::Min($nativeFields.Count, $csFields.Count)
    for ($i = 0; $i -lt $max; $i++) {
        if ($nativeFields[$i] -ne $csFields[$i]) {
            Write-Output "  first divergence at index $i : C++='$($nativeFields[$i])' C#='$($csFields[$i])'"
            break
        }
    }
    exit 1
}

for ($i = 0; $i -lt $nativeFields.Count; $i++) {
    if ($nativeFields[$i] -ne $csFields[$i]) {
        Write-Output "[ERROR] ABI table field mismatch at index $i : C++='$($nativeFields[$i])' C#='$($csFields[$i])'. C++/C# must keep the same order and names."
        exit 1
    }
}

Write-Output "[OK] ABI table verify: $($nativeFields.Count) function pointers match between C++ ManagedNativeApiTable and C# NativeApiTable."
exit 0
