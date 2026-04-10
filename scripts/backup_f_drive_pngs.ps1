param(
    [Parameter(Mandatory=$true)]
    [string]$SourceRoot,

    [Parameter(Mandatory=$true)]
    [string]$BackupRoot,

    [Parameter(Mandatory=$true)]
    [string]$LogFile
)

# Create backup root if missing
if (!(Test-Path $BackupRoot)) {
    New-Item -ItemType Directory -Path $BackupRoot | Out-Null
}

# Start log
"Backup started: $(Get-Date)" | Out-File -FilePath $LogFile -Encoding UTF8

# Find all PNG files under the SD card
$pngFiles = Get-ChildItem -Path $SourceRoot -Recurse -Filter *.png -ErrorAction SilentlyContinue

foreach ($file in $pngFiles) {

    # Compute relative path
    $relativePath = $file.FullName.Substring($SourceRoot.Length).TrimStart("\","/")

    # Compute destination path
    $destPath = Join-Path $BackupRoot $relativePath

    # Ensure destination folder exists
    $destDir = Split-Path $destPath
    if (!(Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    # Copy file safely
    Copy-Item -Path $file.FullName -Destination $destPath -Force

    # Verify file size matches
    $srcSize = (Get-Item $file.FullName).Length
    $dstSize = (Get-Item $destPath).Length

    if ($srcSize -eq $dstSize) {
        "[OK] $relativePath ($srcSize bytes)" | Out-File -FilePath $LogFile -Append
    } else {
        "[ERROR] Size mismatch: $relativePath" | Out-File -FilePath $LogFile -Append
    }
}

"Backup completed: $(Get-Date)" | Out-File -FilePath $LogFile -Append
