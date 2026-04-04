$ErrorActionPreference = "Stop"

$target = "C:\Flic\ComfyUI"
$apiUrl = "https://api.github.com/repos/comfyanonymous/ComfyUI/releases/latest"
$tempArchive = Join-Path $env:TEMP "comfyui_portable.7z"

function Test-ComfyRoot {
    param([string]$Path)

    return (
        (Test-Path (Join-Path $Path "run_nvidia_gpu.bat")) -or
        (Test-Path (Join-Path $Path "run_cpu.bat")) -or
        (Test-Path (Join-Path $Path "main.py"))
    )
}

function Resolve-ComfyPortableUrl {
    param([string]$ReleaseApi)

    $release = Invoke-RestMethod -Uri $ReleaseApi -UseBasicParsing
    $assets = @($release.assets)
    if ($assets.Count -eq 0) {
        throw "No release assets found at $ReleaseApi"
    }

    $preferred = @(
        "ComfyUI_windows_portable_nvidia_cu126.7z",
        "ComfyUI_windows_portable_nvidia.7z",
        "ComfyUI_windows_portable_amd.7z",
        "ComfyUI_windows_portable.zip"
    )

    foreach ($name in $preferred) {
        $hit = $assets | Where-Object { $_.name -eq $name } | Select-Object -First 1
        if ($null -ne $hit) {
            return $hit.browser_download_url
        }
    }

    $fallback = $assets |
        Where-Object { $_.name -match "ComfyUI_windows_portable.*(\\.7z|\\.zip)$" } |
        Select-Object -First 1
    if ($null -eq $fallback) {
        throw "No Windows portable ComfyUI asset found in latest release."
    }

    return $fallback.browser_download_url
}

function Install-OptionalCustomNodes {
    param([string]$ComfyRoot)

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Host "WARN: git not found. Skipping optional custom-node install."
        return
    }

    $customNodesDir = Join-Path $ComfyRoot "custom_nodes"
    if (-not (Test-Path $customNodesDir)) {
        New-Item -ItemType Directory -Path $customNodesDir | Out-Null
    }

    $repos = @(
        "https://github.com/pythongosssss/ComfyUI-Custom-Scripts.git",
        "https://github.com/ltdrdata/ComfyUI-Manager.git"
    )

    foreach ($repo in $repos) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($repo)
        $dest = Join-Path $customNodesDir $name
        try {
            if (Test-Path $dest) {
                Write-Host "Updating optional custom node: $name"
                git -C $dest pull --ff-only | Out-Null
            } else {
                Write-Host "Installing optional custom node: $name"
                git clone --depth 1 $repo $dest | Out-Null
            }
        } catch {
            Write-Host "WARN: Optional custom-node install failed for $name: $($_.Exception.Message)"
        }
    }
}

Write-Host "=== Flic ComfyUI Installer ==="
Write-Host "Target directory: $target"
Write-Host ""

# 1. Create folder structure
if (-not (Test-Path "C:\Flic")) {
    Write-Host "Creating C:\Flic..."
    New-Item -ItemType Directory -Path "C:\Flic" | Out-Null
}

if (-not (Test-Path $target)) {
    Write-Host "Creating $target..."
    New-Item -ItemType Directory -Path $target | Out-Null
}

# 2. Download portable ComfyUI
Write-Host "Downloading ComfyUI portable build..."
$downloadUrl = Resolve-ComfyPortableUrl -ReleaseApi $apiUrl
Write-Host "Resolved asset: $downloadUrl"
Invoke-WebRequest -Uri $downloadUrl -OutFile $tempArchive -UseBasicParsing

# 3. Extract into C:\Flic\ComfyUI
Write-Host "Extracting..."
if (Get-Command tar -ErrorAction SilentlyContinue) {
    tar -xf $tempArchive -C $target
    if ($LASTEXITCODE -ne 0) {
        throw "Extraction failed (tar exit code $LASTEXITCODE)."
    }
} else {
    throw "Extraction failed: 'tar' command is required to unpack ComfyUI portable archives."
}

# Normalize nested archive layout if needed.
if (-not (Test-ComfyRoot -Path $target)) {
    $nested = Get-ChildItem -Path $target -Directory |
        Where-Object { Test-ComfyRoot -Path $_.FullName } |
        Select-Object -First 1

    if ($null -ne $nested) {
        Write-Host "Normalizing nested folder layout from $($nested.FullName)..."
        Get-ChildItem -Path $nested.FullName -Force | Move-Item -Destination $target -Force
        Remove-Item -Path $nested.FullName -Recurse -Force
    }
}

# 4. Install optional custom nodes used by some templates.
Install-OptionalCustomNodes -ComfyRoot $target

# 5. Validate entrypoints
$expected = @(
    "$target\run_nvidia_gpu.bat",
    "$target\run_cpu.bat",
    "$target\main.py"
)

if (-not (Test-ComfyRoot -Path $target)) {
    Write-Host ""
    Write-Host "ERROR: Missing required ComfyUI entrypoints in $target"
    Write-Host "Checked for any of:"
    $expected | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host ""
Write-Host "SUCCESS: ComfyUI installed and validated."
Write-Host "You can now run:"
Write-Host "  Start ComfyUI + Generate Animations (auto)"
