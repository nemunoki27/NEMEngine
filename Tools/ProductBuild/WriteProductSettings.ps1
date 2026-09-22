function Write-ProductSettings {
    param($manifest, [string]$stageDirectory, [string]$productName, [string]$executableName)

    $packageDependencies = [ordered]@{}
    $packageLockDependencies = [ordered]@{}
    foreach ($package in $manifest.packages) {
        $name = [string]$package.name
        $version = [string]$package.version
        $packageDependencies[$name] = $version
        $packageLockDependencies[$name] = [ordered]@{
            version = $version
            source = "embedded"
            path = $name
            contentHash = [string]$package.contentHash
        }
    }
    $packagesDirectory = Join-Path $stageDirectory "Packages"
    New-Item -ItemType Directory -Path $packagesDirectory -Force | Out-Null
    Write-Utf8Json -Path (Join-Path $packagesDirectory "manifest.json") -Value ([ordered]@{
        schemaVersion = 1
        dependencies = $packageDependencies
    })
    Write-Utf8Json -Path (Join-Path $packagesDirectory "packages-lock.json") -Value ([ordered]@{
        schemaVersion = 1
        dependencies = $packageLockDependencies
    })

    $runtimeSettingsDirectory = Join-Path $stageDirectory "ProjectSettings\Runtime"
    New-Item -ItemType Directory -Path $runtimeSettingsDirectory -Force | Out-Null
    Write-Utf8Json -Path (Join-Path $runtimeSettingsDirectory "StartupScene.json") -Value @{
        activeScene = [string]$manifest.startupScene
    }
    Write-Utf8Json -Path (Join-Path $runtimeSettingsDirectory "Game.json") -Value @{
        gameName = $productName
        startupFullscreen = [bool]$manifest.startupFullscreen
    }
    $descriptorName = [System.IO.Path]::GetFileNameWithoutExtension($executableName) + ".nemproject"
    Write-Utf8Json -Path (Join-Path $stageDirectory $descriptorName) -Value @{
        schemaVersion = 1
        projectGuid = [string]$manifest.projectGuid
        name = $productName
        assetsDirectory = "GameAssets"
        packagesDirectory = "Packages"
        projectSettingsDirectory = "ProjectSettings"
    }
    Write-Utf8Json -Path (Join-Path $stageDirectory ".nemBuildManifest.json") -Value @{
        schemaVersion = 2
        productName = $productName
        executableName = $executableName
        startupScene = [string]$manifest.startupScene
        startupFullscreen = [bool]$manifest.startupFullscreen
        assetFileCount = @($manifest.files).Count
        packageCount = @($manifest.packages).Count
        cookHash = [string]$manifest.cookHash
        configuration = "Release"
    }
    $stageRootFull = [System.IO.Path]::GetFullPath($stageDirectory).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $cookFiles = @(Get-ChildItem -LiteralPath $stageDirectory -Recurse -File |
        ForEach-Object {
            $relative = $_.FullName.Substring($stageRootFull.Length).TrimStart(
                [System.IO.Path]::DirectorySeparatorChar,
                [System.IO.Path]::AltDirectorySeparatorChar).Replace('\', '/')
            [ordered]@{
                path = $relative
                size = [long]$_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        } | Sort-Object { $_.path })
    Write-Utf8Json -Path (Join-Path $stageDirectory ".nemCookManifest.json") -Value ([ordered]@{
        schemaVersion = 1
        cookHash = [string]$manifest.cookHash
        files = $cookFiles
    })

}
