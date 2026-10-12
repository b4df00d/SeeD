<#
.SYNOPSIS
    Capture a calibration project in SeeD and score it against the Cycles reference.

.DESCRIPTION
    Runs the engine on the given .seedproj -- the [calibration] section makes it
    render each entry and exit on its own -- then runs compare.py over the results.

    Re-rendering the Blender reference is a separate, manual step, done when the
    .blend changes:
        blender -b <scene>.blend -P render_reference.py -- <project dir>

.EXAMPLE
    .\SeeD\tools\calibration\run.ps1 calibration\falloff.seedproj
#>
param(
    [Parameter(Mandatory = $true)][string]$Project,
    [int]$TimeoutSeconds = 600,
    [switch]$SkipCapture      # score whatever is already in out\, without re-rendering
)

$ErrorActionPreference = 'Stop'

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $toolsDir '..\..\..')
$exe = Join-Path $repoRoot 'x64\Release\SeeD.exe'
# cwd has to be SeeD\ so src\Shaders\ and D3D12SDKPath resolve.
$workingDir = Join-Path $repoRoot 'SeeD'

$projectPath = (Resolve-Path $Project).Path
$projectDir = Split-Path -Parent $projectPath

if (-not $SkipCapture) {
    if (-not (Test-Path $exe)) {
        Write-Host "SeeD.exe not found at $exe -- build the .sln first" -ForegroundColor Red
        exit 2
    }

    Write-Host "capturing $projectPath" -ForegroundColor Cyan
    $proc = Start-Process -FilePath $exe -ArgumentList "`"$projectPath`"" `
        -WorkingDirectory $workingDir -PassThru
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        $proc.Kill()
        Write-Host "SeeD did not exit within $TimeoutSeconds s" -ForegroundColor Red
        exit 2
    }

    $log = Join-Path $projectDir 'calibration.log'
    if (Test-Path $log) { Get-Content $log | ForEach-Object { "  $_" } }

    if ($proc.ExitCode -ne 0) {
        # 3 = timed out waiting for assets, 4 = capture failed or wrong size
        Write-Host "capture failed, exit code $($proc.ExitCode)" -ForegroundColor Red
        exit $proc.ExitCode
    }
}

$compare = Join-Path $toolsDir 'compare.py'
& python $compare $projectPath
$score = $LASTEXITCODE

if ($score -eq 0) {
    Write-Host "`ncalibration passed" -ForegroundColor Green
} else {
    Write-Host "`ncalibration failed -- see the per-object table above and *_compare.png" -ForegroundColor Yellow
}
exit $score
