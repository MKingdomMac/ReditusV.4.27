param(
    [string]$EngineRoot = 'D:\Engines\UE_4.27'
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$projectFile = Join-Path $projectRoot 'Chapter1Reditus.uproject'
$buildFile = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$main = Join-Path $projectRoot 'Reditus_TechMastery_UE427.patch'
$originalAI = Join-Path $projectRoot 'Reditus_TechMastery_UE427_OriginalAI.patch'
$olderBuild = Join-Path $projectRoot 'Reditus_TechMastery_UE427_BuildFix.patch'
$olderEditor = Join-Path $projectRoot 'Reditus_TechMastery_UE427_ReinstancingFix.patch'
$presentation = Join-Path $projectRoot 'Reditus_TechMastery_UE427_BattlePresentation.patch'
$propertyLookupFix = Join-Path $projectRoot 'Reditus_TechMastery_UE427_PropertyLookupFix.patch'
$levelUpDetails = Join-Path $projectRoot 'Reditus_TechMastery_UE427_LevelUpAndDetails.patch'
$levelUpStats = Join-Path $projectRoot 'Reditus_TechMastery_UE427_LevelUpStats.patch'
$overdriveMastery = Join-Path $projectRoot 'Reditus_TechMastery_UE427_OverdriveMastery.patch'
$hud = Join-Path $projectRoot 'Reditus_TechMastery_BattleHUD_UE427.patch'
$hudTargetConfirm = Join-Path $projectRoot 'Reditus_TechMastery_BattleHUD_UE427_TargetConfirm.patch'
$hudOverdrive = Join-Path $projectRoot 'Reditus_TechMastery_BattleHUD_UE427_Overdrive.patch'

foreach ($file in @($projectFile, $main, $originalAI, $olderBuild, $olderEditor, $presentation, $propertyLookupFix,
                    $levelUpDetails, $levelUpStats, $overdriveMastery, $hud, $hudTargetConfirm, $hudOverdrive)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Missing $file. Extract the updated ZIP beside Chapter1Reditus.uproject."
    }
}
if (!(Test-Path -LiteralPath $buildFile -PathType Leaf)) {
    throw "Build.bat is missing at $buildFile. Pass your UE_4.27 directory with -EngineRoot."
}
if (!(Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git must be available in this PowerShell session.'
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
    $updates = @()
    if (!(Test-MasteryPatch $presentation -Reverse)) {
        $masterySource = Join-Path $projectRoot 'Plugins\ReditusTableBuffs\Source\ReditusTableBuffs\Private\ReditusTechMasteryLibrary.cpp'
        $candidates = @($main, $originalAI)
        if (Test-Path -LiteralPath $masterySource -PathType Leaf) {
            $candidates = @($overdriveMastery, $levelUpStats, $levelUpDetails, $propertyLookupFix, $presentation, $olderEditor, $olderBuild)
        }
        $sourceUpdate = $candidates | Where-Object { Test-MasteryPatch $_ } | Select-Object -First 1
        if (!$sourceUpdate) {
            git apply --ignore-space-change --check $candidates[0]
            throw 'Could not match the Tech Mastery source. See the file and hunk above.'
        }
        $updates += $sourceUpdate
    }

    $hudCandidates = @($hud, $hudTargetConfirm)
    $currentHUD = $hudCandidates | Where-Object { Test-MasteryPatch $_ -Reverse } | Select-Object -First 1
    if (!$currentHUD) {
        $hudUpdate = @($hudOverdrive) + $hudCandidates | Where-Object { Test-MasteryPatch $_ } | Select-Object -First 1
        if (!$hudUpdate) {
            git apply --ignore-space-change --check $hud
            throw 'Could not match the battle HUD source. See the file and hunk above.'
        }
        $updates += $hudUpdate
    }

    if ($updates.Count -gt 0) {
        git apply --ignore-space-change --check @updates
        if ($LASTEXITCODE -ne 0) { throw 'The combined source check failed.' }
        git apply --ignore-space-change @updates
        if ($LASTEXITCODE -ne 0) { throw 'Git could not apply the source update.' }
        Write-Host 'Applied the Tech Mastery and victory presentation source for UE 4.27.'
    } else {
        Write-Host 'The Tech Mastery and victory presentation source is already applied.'
    }

    & $buildFile Chapter1ReditusEditor Win64 Development "-Project=$projectFile" -WaitMutex
    if ($LASTEXITCODE -ne 0) { throw "The Unreal build returned $LASTEXITCODE. See the first error in the build output." }
}
finally {
    Pop-Location
}

Write-Host ''
Write-Host 'Build finished. Reopen Chapter1Reditus and open Window > Developer Tools > Output Log.'
Write-Host 'With Play stopped, run Reditus.InstallTechMastery and choose Save All after it succeeds.'
