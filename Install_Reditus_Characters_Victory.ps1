param(
    [string]$EngineRoot = 'D:\Engines\UE_4.27'
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -Filter '*.uproject' -File)
if ($projectFiles.Count -ne 1) {
    throw 'Extract this ZIP into the game folder, beside Chapter1Reditus.uproject, and run this script again.'
}

$sourceFile = Join-Path $projectRoot 'Plugins\ReditusBlueprintRepair\Source\ReditusBlueprintRepairEditor\Private\ReditusBlueprintRepairEditor.cpp'
if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
    throw 'Extract the Plugins folder from this ZIP into the game folder first.'
}

$buildFile = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $buildFile -PathType Leaf)) {
    throw "Build.bat was not found at $buildFile. Run this script with -EngineRoot followed by your UE_4.27 folder."
}

$projectFile = $projectFiles[0]
$editorTarget = $projectFile.BaseName + 'Editor'
Write-Host "Building $editorTarget with the Blueprint repair editor plugin."
Push-Location -LiteralPath $projectRoot
try {
    & $buildFile $editorTarget Win64 Development "-Project=$($projectFile.FullName)" -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        throw "The Unreal build returned $LASTEXITCODE. Read the first compiler error above."
    }
}
finally {
    Pop-Location
}

Write-Host ''
Write-Host 'Build finished. Open the project and stop Play.'
Write-Host 'Open Window > Developer Tools > Output Log.'
Write-Host 'Enter Reditus.RepairCharactersAndVictory in the Cmd field.'
Write-Host 'After the success message, choose Save All and test a fresh battle.'
