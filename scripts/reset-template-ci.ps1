$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$WorkflowDir = Join-Path $Root '.github\workflows'

if (-not (Test-Path $WorkflowDir)) {
    throw "Workflow directory not found: $WorkflowDir"
}

$keep = @('build.yml', 'push.yaml', 'pr-pull.yaml')
Write-Host "Bacons Helper workflow directory: $WorkflowDir"

Get-ChildItem -LiteralPath $WorkflowDir -File | ForEach-Object {
    if ($keep -notcontains $_.Name) {
        Write-Host "Removing stale/template workflow: $($_.Name)"
        Remove-Item -LiteralPath $_.FullName -Force
    }
}

Write-Host ''
Write-Host 'Remaining workflows:'
Get-ChildItem -LiteralPath $WorkflowDir -File | Select-Object -ExpandProperty Name
Write-Host ''
Write-Host 'Commit the workflow replacements/deletions:'
Write-Host '  git add -A'
Write-Host '  git commit -m "Use Bacons Helper OBS Companion CI"'
Write-Host '  git push'
