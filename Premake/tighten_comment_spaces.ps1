# templateClass ルールの「コメント内の文字の間にスペースを入れない」を機械適用する補助
# 純粋なコメント行のみ対象にし、日本語文字に隣接する半角スペースだけを除去する
# 英単語どうしの間のスペース GPU Hang などは両側 ASCII なので保持される
# 。/（） の言い換えは意味保持が要るためこのスクリプトでは触らない
param(
    [switch]$Apply,
    [switch]$StripTrailingKuten,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Files
)

# Hiragana Katakana CJK と全角記号を日本語文字とみなす文字クラス
# CJK記号と句読点 3000-303F / かな 3040-30FF / CJK漢字 3400-9FFF / 全角形 FF00-FFEF
$jp = '　-〿぀-ヿ㐀-鿿＀-￯'
$reBefore = [regex]"([$jp]) +"
$reAfter = [regex]" +([$jp])"

function Tighten([string]$text) {
    $prev = $null
    $cur = $text
    while ($cur -ne $prev) {
        $prev = $cur
        $cur = $reBefore.Replace($cur, '$1')
        $cur = $reAfter.Replace($cur, '$1')
    }
    return $cur
}

$filesChanged = 0
$linesChanged = 0
$samples = New-Object System.Collections.Generic.List[string]

foreach ($path in $Files) {
    if (-not (Test-Path $path)) { continue }
    $lines = [System.IO.File]::ReadAllLines($path)
    $fileChanged = 0
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $ln = $lines[$i]
        # //// 直後の区切りスペース1つ tab は残し、それ以降のみ詰める
        $m = [regex]::Match($ln, '^(\s*/{2,}[ \t]?)(.*)$')
        if (-not $m.Success) { continue }
        $head = $m.Groups[1].Value
        $body = $m.Groups[2].Value
        if ($ln.Contains('====')) { continue }
        if ($body.Trim().Length -gt 0 -and ($body.Trim() -replace '-', '').Length -eq 0) { continue }
        if ($body.Contains('"')) { continue }
        $newBody = Tighten $body
        # 行末の句点。は template ルールで付けないため除去する、文中の。は意味保持のため触らない
        if ($StripTrailingKuten) {
            $newBody = $newBody -replace '。+\s*$', ''
        }
        if ($newBody -eq $body) { continue }
        $fileChanged++
        if ($samples.Count -lt 50) {
            $samples.Add("--- ${path}:$($i+1)")
            $samples.Add("  - $ln")
            $samples.Add("  + $head$newBody")
        }
        $lines[$i] = "$head$newBody"
    }
    if ($fileChanged -gt 0) {
        $filesChanged++
        $linesChanged += $fileChanged
        if ($Apply) {
            $enc = New-Object System.Text.UTF8Encoding($false)
            [System.IO.File]::WriteAllLines($path, $lines, $enc)
        }
    }
}

"files changed: $filesChanged  lines changed: $linesChanged  apply: $($Apply.IsPresent)"
$samples | ForEach-Object { $_ }
