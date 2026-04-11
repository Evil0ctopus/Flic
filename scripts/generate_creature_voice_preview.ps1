param(
    [string]$Text = "Heh! Ooh! Hello Joshua. Flic awake now-wiggly and curious! Eep! Heehee!",
    [string]$ModelPath = "C:\PiperPreview\models\en_US-lessac-medium.onnx",
    [string]$OutputPath = "C:\PiperPreview\samples\stitch_creature_voice.wav",
    [double]$SpeedFactor = 1.18,
    [double]$PitchFactor = 1.10
)

$ErrorActionPreference = "Stop"

$samplesDir = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $samplesDir -Force | Out-Null

$modelConfigPath = "$ModelPath.json"
if (-not (Test-Path $ModelPath)) {
    throw "Model not found: $ModelPath"
}
if (-not (Test-Path $modelConfigPath)) {
    throw "Model config not found: $modelConfigPath"
}

$piperCmd = Get-Command piper -ErrorAction SilentlyContinue
if ($piperCmd) {
    $piperExe = $piperCmd.Source
} elseif (Test-Path "C:\Users\jlors\OneDrive\Desktop\Flic\.venv\Scripts\piper.exe") {
    $piperExe = "C:\Users\jlors\OneDrive\Desktop\Flic\.venv\Scripts\piper.exe"
} else {
    throw "Piper executable not found on PATH or expected virtualenv location."
}

$pythonExeCandidate = Join-Path (Split-Path -Parent $piperExe) "python.exe"
if (Test-Path $pythonExeCandidate) {
    $pythonExe = $pythonExeCandidate
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $pythonExe = (Get-Command python).Source
} else {
    throw "Python executable not found for post-processing."
}

$tempRawWav = Join-Path $env:TEMP "piper_raw_creature_$([guid]::NewGuid().ToString('N')).wav"

try {
    $Text | & $piperExe --model $ModelPath --output_file $tempRawWav
    if (-not (Test-Path $tempRawWav)) {
        throw "Piper did not create raw WAV output."
    }

    $pythonScript = @"
import wave
import numpy as np

in_path = r'''$tempRawWav'''
out_path = r'''$OutputPath'''
speed_factor = float($SpeedFactor)
pitch_factor = float($PitchFactor)

with wave.open(in_path, 'rb') as w:
    nchannels = w.getnchannels()
    sampwidth = w.getsampwidth()
    framerate = w.getframerate()
    nframes = w.getnframes()
    comptype = w.getcomptype()
    compname = w.getcompname()
    data = w.readframes(nframes)

if nchannels != 1:
    raise RuntimeError('Expected mono WAV from Piper for this transform.')
if sampwidth != 2:
    raise RuntimeError('Expected 16-bit PCM WAV from Piper for this transform.')

# Convert to a lower effective source rate and write back at original rate.
# This raises both speed and pitch for a small, lively creature-like timbre.
combined = max(1.01, speed_factor * pitch_factor)
samples = np.frombuffer(data, dtype=np.int16)
target_len = max(1, int(len(samples) / combined))
src_positions = np.linspace(0, len(samples) - 1, num=len(samples), dtype=np.float64)
dst_positions = np.linspace(0, len(samples) - 1, num=target_len, dtype=np.float64)
shifted = np.interp(dst_positions, src_positions, samples).astype(np.int16).tobytes()

with wave.open(out_path, 'wb') as out:
    out.setnchannels(nchannels)
    out.setsampwidth(sampwidth)
    out.setframerate(framerate)
    out.setcomptype(comptype, compname)
    out.writeframes(shifted)
"@

    & $pythonExe -c $pythonScript

    if (-not (Test-Path $OutputPath)) {
        throw "Final output file was not created: $OutputPath"
    }

    Start-Process $OutputPath
    Write-Host "Playback launched: $OutputPath"
    Write-Host "Creature voice test complete."
}
finally {
    if (Test-Path $tempRawWav) {
        Remove-Item $tempRawWav -Force
    }
}
