function Write-ProductAssetRoots {
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

    $descriptorName = [System.IO.Path]::GetFileNameWithoutExtension($executableName) + ".nemproject"
    Write-Utf8Json -Path (Join-Path $stageDirectory $descriptorName) -Value @{
        schemaVersion = 1
        projectGuid = [string]$manifest.projectGuid
        name = $productName
        assetsDirectory = "GameAssets"
        packagesDirectory = "Packages"
        projectSettingsDirectory = "ProjectSettings"
    }
}

function Write-ProductSettings {
    param($manifest, [string]$stageDirectory, [string]$productName, [string]$executableName, [bool]$managedScripts = $true)

    Write-ProductAssetRoots $manifest $stageDirectory $productName $executableName

    $runtimeSettingsDirectory = Join-Path $stageDirectory "ProjectSettings\Runtime"
    New-Item -ItemType Directory -Path $runtimeSettingsDirectory -Force | Out-Null
    Write-Utf8Json -Path (Join-Path $runtimeSettingsDirectory "StartupScene.json") -Value @{
        activeScene = [string]$manifest.startupScene
    }
    Write-Utf8Json -Path (Join-Path $runtimeSettingsDirectory "Game.json") -Value @{
        gameName = $productName
        managedScripts = $managedScripts
        startupFullscreen = [bool]$manifest.startupFullscreen
        gameWidth = [int]$manifest.gameWidth
        gameHeight = [int]$manifest.gameHeight
    }
}
