param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference = 'Stop'
$plan = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
function Write-Result($state, $errorText, $extra) {
    $result = @{state=$state; error=$errorText}
    if ($extra) { foreach ($key in $extra.Keys) { $result[$key] = $extra[$key] } }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $plan.resultPath -Encoding UTF8
}
function Check-RelativePath([string]$relative) {
    if ([string]::IsNullOrWhiteSpace($relative) -or $relative.StartsWith('/') -or $relative.StartsWith('\') -or $relative.Contains(':')) { throw 'unsafe-package-path' }
    foreach ($part in $relative.Replace('\','/').Split('/')) {
        if ($part -eq '..' -or $part -eq '.') { throw 'unsafe-package-path' }
    }
}
function Check-AppFile([string]$relative) {
    Check-RelativePath $relative
    $parts = $relative.Replace('\','/').Split('/')
    $name = $parts[-1]
    if ($name -match '(?i)\.ini$|\.log$|\.pdb$' -or $parts[0] -match '(?i)^(vendor|glossary|logs|saves|config)$') { throw 'protected-user-file-in-package' }
    if ($parts.Length -eq 1) {
        if ($name -eq 'unityTools.exe' -or $name -match '^(?i)(Qt6[A-Za-z0-9]+|msvcp[0-9_]+|vcruntime[0-9_]+|concrt[0-9_]+)\.dll$' -or $name -match '^(?i)(LICENSE|NOTICE\.md|MANIFEST\.txt|RELEASE-NOTES\.md|qt\.conf)$') { return }
    } else {
        if ($parts[0] -match '^(?i)(bridge|platforms|generic|imageformats|networkinformation|styles|tls)$' -and $name -match '(?i)\.dll$') { return }
        if ($parts[0] -eq 'fonts' -and $name -match '(?i)\.(ttf|otf)$') { return }
        if ($parts[0] -eq 'translations' -and $name -match '(?i)\.qm$') { return }
        if ($parts[0] -eq 'THIRD-PARTY-NOTICES' -and $name -match '(?i)\.(txt|md)$') { return }
    }
    throw ('unsupported-package-file: ' + $relative)
}
if ($plan.mode -eq 'prepare') {
    $zip = $null
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead($plan.archivePath)
        foreach ($entry in $zip.Entries) {
            Check-RelativePath $entry.FullName
            $kind = ($entry.ExternalAttributes -shr 16) -band 61440
            if ($kind -eq 40960 -or ($kind -ne 0 -and $kind -ne 32768 -and $kind -ne 16384)) { throw 'package-link-not-allowed' }
        }
        New-Item -ItemType Directory -Path $plan.extractPath -Force | Out-Null
        foreach ($entry in $zip.Entries) {
            $target = Join-Path $plan.extractPath $entry.FullName
            $full = [System.IO.Path]::GetFullPath($target)
            $prefix = [System.IO.Path]::GetFullPath($plan.extractPath).TrimEnd('\') + '\'
            if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'unsafe-package-path' }
            if ($entry.FullName.EndsWith('/')) { New-Item -ItemType Directory -Path $full -Force | Out-Null; continue }
            New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($full)) -Force | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $full, $true)
        }
        $root = $plan.extractPath
        if (-not (Test-Path -LiteralPath (Join-Path $root 'unityTools.exe') -PathType Leaf)) {
            $children = @(Get-ChildItem -LiteralPath $root)
            if ($children.Count -ne 1 -or -not $children[0].PSIsContainer) { throw 'updated-executable-missing' }
            $root = $children[0].FullName
        }
        if (-not (Test-Path -LiteralPath (Join-Path $root 'unityTools.exe') -PathType Leaf)) { throw 'updated-executable-missing' }
        foreach ($file in (Get-ChildItem -LiteralPath $root -File -Recurse)) {
            if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'package-link-not-allowed' }
            Check-AppFile $file.FullName.Substring($root.Length).TrimStart('\','/')
        }
        Write-Result 'prepared' '' @{stagedDir=$root}
        exit 0
    } catch { Write-Result 'failed' $_.Exception.Message $null; exit 1 }
    finally { if ($zip) { $zip.Dispose() } }
}
if ($plan.mode -ne 'apply') { throw 'unknown-update-mode' }
$backup = Join-Path (Split-Path $PlanPath -Parent) 'backup'
$changed = New-Object System.Collections.Generic.List[string]
$success = $false
$restored = $true
try {
    $staged = [IO.Path]::GetFullPath($plan.stagedDir).TrimEnd('\','/')
    $install = [IO.Path]::GetFullPath($plan.installDir).TrimEnd('\','/')
    if ($staged -eq $install) { throw 'invalid-update-directory' }
    $sourceExe = Join-Path $staged 'unityTools.exe'
    if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) { throw 'updated-executable-missing' }
    $files = @(Get-ChildItem -LiteralPath $staged -File -Recurse | Sort-Object FullName)
    foreach ($file in $files) { Check-AppFile $file.FullName.Substring($staged.Length).TrimStart('\','/') }
    $parent = Get-Process -Id $plan.parentPid -ErrorAction SilentlyContinue
    if ($parent) {
        $parent.Refresh()
        if ($parent.Path -ne $plan.parentExePath) { throw 'update-parent-identity-changed' }
        if (-not $parent.WaitForExit(10000)) { throw 'old-process-still-running' }
        $parent.Dispose()
    }
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    # Verify every existing destination is writable before replacing any file.
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($staged.Length).TrimStart('\','/')
        $destination = Join-Path $install $relative
        if (Test-Path -LiteralPath $destination) {
            $handle = [IO.File]::Open($destination, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $handle.Dispose()
            $copy = Join-Path $backup $relative
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($copy)) -Force | Out-Null
            [IO.File]::Copy($destination, $copy, $true)
        }
    }
    $ordered = @($files | Where-Object { $_.FullName -ne $sourceExe }) + @($files | Where-Object { $_.FullName -eq $sourceExe })
    foreach ($file in $ordered) {
        $relative = $file.FullName.Substring($staged.Length).TrimStart('\','/')
        $destination = Join-Path $install $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        $temporary = $destination + '.ut-update-new'
        [IO.File]::Copy($file.FullName, $temporary, $true)
        if (Test-Path -LiteralPath $destination) { [IO.File]::Replace($temporary, $destination, $null) }
        else { [IO.File]::Move($temporary, $destination) }
        $changed.Add($relative)
    }
    $newProcess = Start-Process -FilePath (Join-Path $install 'unityTools.exe') -WorkingDirectory $plan.workingDir -PassThru
    Write-Result 'applied' '' @{pid=$newProcess.Id}
    $success = $true
} catch {
    $failure = $_.Exception.Message
    foreach ($relative in $changed) {
        try {
            $destination = Join-Path $install $relative
            $old = Join-Path $backup $relative
            if (Test-Path -LiteralPath $old) { [IO.File]::Copy($old, $destination, $true) }
            elseif (Test-Path -LiteralPath $destination) { [IO.File]::Delete($destination) }
        } catch { $restored = $false }
    }
    Write-Result 'failed' $failure @{restored=$restored; backupPath=$backup}
} finally {
    if ($success -or $restored) { Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue }
}
if ($success) { exit 0 }
exit 1
