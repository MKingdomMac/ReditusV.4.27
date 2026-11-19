# Run beside your .uproject after merging this package, with Unreal closed.
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$ProjectRoot = $PSScriptRoot,
    [switch]$KeepBuildFiles
)
$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
if (-not (Get-ChildItem -LiteralPath $rootPath -Filter '*.uproject' -File)) {
    throw 'Run this helper beside your .uproject, or provide -ProjectRoot.'
}
if (Get-Process -Name 'UE4Editor','UnrealEditor' -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor before cleaning plugin source/build output.'
}
$pluginPath = Join-Path $rootPath 'Plugins\ReditusQuestNPC'
if (-not (Test-Path -LiteralPath (Join-Path $pluginPath 'ReditusQuestNPC.uplugin') -PathType Leaf)) {
    throw 'ReditusQuestNPC was not found under this project. No files removed.'
}
# These are retired files shipped in 2.7.6, never a Content or save-data path.
$retiredFiles = @(
    @{ RelativePath = 'Plugins\ReditusQuestNPC\Source\ReditusQuestNPCEditor\Private\ReditusQuestStepExamples.cpp'; SHA256 = '42266936E45A17726C06BCED194BCDD0BD1D5B2A530A210F58AAE1F102EE9925' }
    @{ RelativePath = 'NPC_AND_DIALOGUE_PLAN.md'; SHA256 = 'E4FEDA93837DBFE67BD24C9DC2F7DDD96AF74C62394EC8D879F13EF2A840AA63' }
    @{ RelativePath = 'EXTORTION_MIGRATION.md'; SHA256 = '3FAD1B60171D55714FCF91410B4E1F3B719CDDD3DCDB6338B01D6B7ED128B248' }
    @{ RelativePath = 'START_HERE_MULTI_STEP_QUESTS.txt'; SHA256 = 'DC34F389776D72D7761E839E7C73E6289EE4FC668091FA5DBD537F9959306A5F' }
    @{ RelativePath = 'Backups\DialoguePlugin\Source\DialoguePlugin\Private\DialogueUserWidget.cpp'; SHA256 = '446FE6DD51536F5F4B4AFFC7367B7520CC9DB302D34ADEAA3BC514C8BE893436' }
    @{ RelativePath = 'Backups\DialoguePlugin\Source\DialoguePlugin\Public\DialogueUserWidget.h'; SHA256 = 'C374AAEEFAF7EB6927B3E439CB2014F03289A330A6DDF30CBA47D32500CB0E9E' }
)
[long]$removedBytes = 0
foreach ($entry in $retiredFiles) {
    $candidate = Join-Path $rootPath $entry.RelativePath
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    $item = Get-Item -LiteralPath $candidate -Force
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        Write-Warning "Preserved link: $($entry.RelativePath)"
        continue
    }
    if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -ne $entry.SHA256) {
        Write-Warning "Preserved modified file: $($entry.RelativePath)"
        continue
    }
    if ($PSCmdlet.ShouldProcess($candidate, 'Remove hash-matched retired shipped file')) {
        $length = $item.Length
        Remove-Item -LiteralPath $candidate -Force
        $removedBytes += $length
        Write-Host "Removed: $($entry.RelativePath)"
    }
}
if (-not $KeepBuildFiles) {
    foreach ($folderName in @('Binaries','Intermediate')) {
        $candidate = Join-Path $pluginPath $folderName
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) { continue }
        $item = Get-Item -LiteralPath $candidate -Force
        if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            Write-Warning "Preserved link: $candidate"
            continue
        }
        $nestedLinks = Get-ChildItem -LiteralPath $candidate -Recurse -Force |
            Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint }
        if ($nestedLinks) {
            Write-Warning "Preserved generated folder containing a link: $candidate"
            continue
        }
        if ($PSCmdlet.ShouldProcess($candidate, 'Remove regeneratable plugin build output')) {
            [long]$length = (Get-ChildItem -LiteralPath $candidate -Recurse -File -Force |
                Measure-Object -Property Length -Sum).Sum
            Remove-Item -LiteralPath $candidate -Recurse -Force
            $removedBytes += $length
            Write-Host "Removed: Plugins\ReditusQuestNPC\$folderName"
        }
    }
}
Write-Host ('Removed {0:N2} MiB. Build Development Editor / Win64 before reopening Unreal.' -f ($removedBytes / 1MB))
Write-Host 'Compilation recreates required binaries and intermediate files.'
