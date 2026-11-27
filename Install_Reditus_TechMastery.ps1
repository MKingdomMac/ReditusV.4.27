param(
    [string]$EngineRoot = 'D:\Engines\UE_4.27'
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$projectFile = Join-Path $projectRoot 'Chapter1Reditus.uproject'
$patchFile = Join-Path $projectRoot 'Reditus_TechMastery_UE427.patch'
$olderPatchFile = Join-Path $projectRoot 'Reditus_TechMastery_UE427_OriginalAI.patch'
$buildFixFile = Join-Path $projectRoot 'Reditus_TechMastery_UE427_BuildFix.patch'
$reinstancingFixFile = Join-Path $projectRoot 'Reditus_TechMastery_UE427_ReinstancingFix.patch'
$buildFile = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'

if (!(Test-Path -LiteralPath $projectFile -PathType Leaf) -or !(Test-Path -LiteralPath $patchFile -PathType Leaf) -or
    !(Test-Path -LiteralPath $buildFixFile -PathType Leaf) -or !(Test-Path -LiteralPath $reinstancingFixFile -PathType Leaf)) {
    throw 'Extract the updated ZIP beside Chapter1Reditus.uproject. The installer and patches should be in that folder.'
}
if (!(Test-Path -LiteralPath $buildFile -PathType Leaf)) {
    throw "Build.bat is missing at $buildFile. Pass the path to your UE_4.27 folder with -EngineRoot."
}
if (!(Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git must be available in this PowerShell session.'
}
$aiSource = Join-Path $projectRoot 'Plugins\ReditusTableBuffs\Source\ReditusTableBuffs\Private\ReditusEnemyCombatAIComponent.cpp'
if (!(Test-Path -LiteralPath $aiSource -PathType Leaf)) {
    throw 'This update requires the existing Reditus Enemy Combat AI source.'
}

function Test-MasteryPatch([string]$Candidate, [switch]$Reverse) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $gitArguments = @('apply', '--ignore-space-change', '--check')
        if ($Reverse) { $gitArguments += '--reverse' }
        git @gitArguments $Candidate 2>$null
        return ($LASTEXITCODE -eq 0)
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
}

Push-Location -LiteralPath $projectRoot
try {
    $masterySource = Join-Path $projectRoot 'Plugins\ReditusTableBuffs\Source\ReditusTableBuffs\Private\ReditusTechMasteryLibrary.cpp'
    $hasMastery = Test-Path -LiteralPath $masterySource -PathType Leaf
    $variants = @($patchFile)
    if (Test-Path -LiteralPath $olderPatchFile -PathType Leaf) { $variants += $olderPatchFile }
    $installed = $null
    if ($hasMastery) {
        $installed = $variants | Where-Object { Test-MasteryPatch $_ -Reverse } | Select-Object -First 1
    }
    if ($installed) {
        Write-Host 'The source patch is already applied.'
    } else {
        $candidates = $variants
        if ($hasMastery) {
            # The combined fix also corrects the original C2664 calls.
            $candidates = @($buildFixFile, $reinstancingFixFile) + $variants
        }
        $compatible = $candidates | Where-Object { Test-MasteryPatch $_ } | Select-Object -First 1
        if (!$compatible) {
            git apply --ignore-space-change --check $patchFile
            throw 'The patch check failed. Git has not applied any source changes. The file and hunk are listed above.'
        }
        git apply --ignore-space-change $compatible
        if ($LASTEXITCODE -ne 0) { throw 'Git could not apply the source patch.' }
        Write-Host 'Applied the Tech Mastery source update for UE 4.27.'
    }

    & $buildFile Chapter1ReditusEditor Win64 Development "-Project=$projectFile" -WaitMutex
    if ($LASTEXITCODE -ne 0) { throw "The Unreal build returned $LASTEXITCODE. See the first error in the build output." }
}
finally {
    Pop-Location
}

Write-Host ''
Write-Host 'Build finished. Open Chapter1Reditus and open Window > Developer Tools > Output Log.'
Write-Host 'For a first profile migration, run Reditus.MigrateEnemyAIProfiles and save DT_Enemies.'
Write-Host 'Run Reditus.InstallTechMastery. After the success message, choose Save All.'
