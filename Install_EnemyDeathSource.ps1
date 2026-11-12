param([string]$ProjectRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
if (Get-Process UE4Editor,UnrealEditor -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor before adding a native DataTable field. Do not use Hot Reload.'
}
$header = Join-Path $ProjectRoot 'Source/Chapter1Reditus/Data/ReditusEnemyDefinition.h'
$build = Join-Path $ProjectRoot 'Source/Chapter1Reditus/Chapter1Reditus.Build.cs'
$plugin = Join-Path $ProjectRoot 'Plugins/ReditusEnemyDeath/ReditusEnemyDeath.uplugin'
foreach ($file in @($header,$build,$plugin)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file. Copy this package into your project root first." }
}
$h = [IO.File]::ReadAllText($header)
$b = [IO.File]::ReadAllText($build)
if ($h -notmatch 'struct\s+\w+\s+FReditusEnemyDefinition\s*:\s*public\s+FTableRowBase') { throw 'Unrecognized enemy definition. No files changed.' }
$include = '#include "ReditusEnemyDeathVisuals.h"'
if ($h -notmatch '#include\s+"ReditusEnemyDeathVisuals\.h"') {
    $marker = '#include "ReditusEnemyDefinition.generated.h"'
    if (($h.Split(@($marker),[StringSplitOptions]::None).Count - 1) -ne 1) { throw 'Expected one generated header marker. No files changed.' }
    $h = $h.Replace($marker, $include + "`r`n" + $marker)
}
if ($h -notmatch '\bFReditusEnemyBossDeathOptions\s+BossDeath\s*;') {
    if ($h -match '\bBossDeath\b') { throw 'An unrelated BossDeath member exists. No files changed.' }
    $struct = [regex]::Matches($h, 'struct\s+\w+\s+FReditusEnemyDefinition\s*:\s*public\s+FTableRowBase')
    if ($struct.Count -ne 1) { throw 'Expected one enemy definition struct. No files changed.' }
    $opening = $h.IndexOf('{', $struct[0].Index + $struct[0].Length)
    if ($opening -lt 0) { throw 'Cannot find enemy struct body. No files changed.' }
    $depth = 1; $closing = $opening + 1
    while ($closing -lt $h.Length -and $depth -gt 0) {
        if ($h[$closing] -eq '{') { $depth++ }
        if ($h[$closing] -eq '}') { $depth-- }
        $closing++
    }
    if ($depth -ne 0 -or $closing -ge $h.Length -or $h[$closing] -ne ';') { throw 'Cannot match enemy struct closing brace. No files changed.' }
    $field = "`r`n    // Boss decisions only; normal death appearance/timing inherit BP_EnemyUnitBase.`r`n    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = `"Boss Death`")`r`n    FReditusEnemyBossDeathOptions BossDeath;`r`n"
    $h = $h.Insert($closing - 1, $field)
}
# Preserve an earlier DeathVisual value and Blueprint pin compatibility, but hide
# this retired full-row override from the Details panel. Only our exact prior declaration is changed.
$legacy = 'UPROPERTY\(EditAnywhere,\s*BlueprintReadWrite,\s*Category\s*=\s*"Death Visual"\)\s*FReditusEnemyDeathSettings\s+DeathVisual\s*;'
$h = [regex]::Replace($h, $legacy, "UPROPERTY(BlueprintReadWrite, Category = `"Legacy Enemy Death`")`r`n    FReditusEnemyDeathSettings DeathVisual;")

if ($b -notmatch '"ReditusEnemyDeath"') {
    $matches = [regex]::Matches($b, 'PCHUsage\s*=\s*PCHUsageMode\.UseExplicitOrSharedPCHs\s*;')
    if ($matches.Count -ne 1) { throw 'Unrecognized Build.cs constructor. No files changed.' }
    $m = $matches[0]
    $b = $b.Insert($m.Index+$m.Length, "`r`n        PublicDependencyModuleNames.Add(`"ReditusEnemyDeath`");")
}
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$utf8 = New-Object System.Text.UTF8Encoding($false)
# Keep binary-table and Blueprint recovery copies OUTSIDE Content before the first cold rebuild/open.
$backupRoot = Join-Path $ProjectRoot ('Saved/ReditusEnemyDeathBackups/' + $stamp)
foreach ($relative in @('Content/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Enemies.uasset', 'Content/TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits/BP_EnemyUnitBase.uasset')) {
    $source = Join-Path $ProjectRoot $relative
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        $destination = Join-Path $backupRoot $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
    }
}
foreach ($entry in @(@($header,$h),@($build,$b))) {
    $path = [string]$entry[0]; $text = [string]$entry[1]
    if ([IO.File]::ReadAllText($path) -cne $text) {
        $backup = $path + '.before-enemy-death-' + $stamp + '.bak'
        Copy-Item -LiteralPath $path -Destination $backup -ErrorAction Stop
        [IO.File]::WriteAllText($path,$text,$utf8)
        Write-Host "Updated $path (backup: $backup)"
    } else { Write-Host "Already installed: $path" }
}
Write-Host 'Source setup complete. Rebuild Development Editor / Win64 with Unreal CLOSED.'
Write-Host 'Then reopen Unreal. Keep your working On Unit Die hookup. DT_Enemies now has Boss Death options; set Is Boss only for boss rows.'
