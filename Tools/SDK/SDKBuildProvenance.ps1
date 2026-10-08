function Get-SDKBuildProvenance([string]$EngineRoot) {

    # 配布した構築条件と外部ライブラリの設定を残す
    $records = @()
    foreach ($library in @(
        @{ name = 'assimp'; source = 'Project\Externals\assimp'; required = $true },
        @{ name = 'msdf'; source = 'Premake\cmake\msdf'; required = (Test-Path -LiteralPath (Join-Path $EngineRoot 'Project\Externals\msdf-atlas-gen\CMakeLists.txt')) }
    )) {
        $path = Join-Path $EngineRoot ('Generated\Externals\' + $library.name + '\CMakeCache.txt')
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            if ($library.required) { throw "外部ライブラリの構築設定がありません: $path" }
            $records += [ordered]@{ name = $library.name; available = $false }
            continue
        }
        $values = [ordered]@{}
        foreach ($line in [IO.File]::ReadAllLines($path)) {
            if ($line -notmatch '^([^#/:][^:]*):[^=]+=(.*)$') { continue }
            $key = $Matches[1]
            $value = $Matches[2]
            if ($key -match '^(CMAKE_(GENERATOR.*|HOME_DIRECTORY|CXX_COMPILER.*|MSVC_RUNTIME_LIBRARY|DEBUG_POSTFIX)|LIBRARY_SUFFIX|ASSIMP_.*|MSDF.*|FREETYPE_.*|BUILD_SHARED_LIBS)$') {
                $values[$key] = $value
            }
        }
        # 別のソースや構築対象を指すcacheを配布に使用しない
        $expectedSource = [IO.Path]::GetFullPath((Join-Path $EngineRoot $library.source)).TrimEnd('\', '/')
        if (-not $values.Contains('CMAKE_HOME_DIRECTORY') -or
            [IO.Path]::GetFullPath($values['CMAKE_HOME_DIRECTORY']).TrimEnd('\', '/') -ne $expectedSource -or
            $values['CMAKE_GENERATOR'] -ne 'Visual Studio 18 2026' -or $values['CMAKE_GENERATOR_PLATFORM'] -ne 'x64') {
            throw "外部ライブラリのcacheが現在の構築条件と異なります: $path"
        }
        $records += [ordered]@{ name = $library.name; available = $true; cacheSHA256 = Get-FileSHA256 $path; settings = $values }
    }
    return [ordered]@{ generator = 'Visual Studio 18 2026'; platform = 'x64'; externals = $records }
}
