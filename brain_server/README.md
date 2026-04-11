# Flic Brain Server

Persistent high-level intelligence backend for Flic, running on your PC 24/7.

## Features
- STT: Whisper.cpp (`POST /stt`)
- LLM: Ollama local LLaMA (`POST /respond`)
- Persona shaping: Stitch creature transform (`POST /persona`)
- TTS: Piper WAV generation and cache (`POST /tts`)
- Health check (`GET /health`)
- WebSocket endpoint (`/ws`) with token auth
- Token auth, rate limit, and access logs
- Auto-restart scripts for persistent operation

## File Layout
- `server.py` main API service
- `stt_whisper.py` Whisper.cpp adapter
- `llm_engine.py` Ollama adapter
- `personality_transform.py` Stitch persona shaping
- `tts_piper.py` Piper synthesis and cache
- `config.json` runtime configuration
- `run_forever.bat` Windows persistent loop
- `run_forever.sh` Linux/macOS persistent loop
- `cloudflare_tunnel_setup.md` secure public access guide

## 1. Install Dependencies
From `brain_server`:

```powershell
py -3 -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

## 2. Install AI Engines
1. Ollama
   - Install Ollama and pull your model:
   - `ollama pull llama3.1:8b`
2. Whisper.cpp
   - Build `whisper-cli` and download model file (for example `ggml-base.en.bin`).
3. Piper
   - Install Piper binary and voice model `en_US-lessac-medium.onnx`.

Update `config.json` with absolute paths for Whisper and Piper binaries and models.

## 3. Configure Security
In `config.json`:
- Set `security.api_token` to a long random token.
- Keep `server.host` as `127.0.0.1`.

## 4. Run Server
One-shot:

```powershell
.venv\Scripts\Activate.ps1
$env:BRAIN_CONFIG = "C:\\Users\\jlors\\OneDrive\\Desktop\\Flic\\brain_server\\config.json"
python server.py
```

Persistent:

```powershell
run_forever.bat
```

## 5. API Contract
All requests require either:
- `Authorization: Bearer <token>`
- or `X-API-Token: <token>`

### `POST /stt`
Multipart form fields:
- `audio` file containing 16-bit PCM mono bytes
- `sample_rate` (default `16000`)

Response:

```json
{"text":"recognized speech"}
```

### `POST /respond`

```json
{"text":"hello flic"}
```

Response:

```json
{"intent":"greeting","reply":"Hello. I am online and ready."}
```

### `POST /persona`

```json
{"text":"Hello. I am online and ready.","emotion":"curious"}
```

Response:

```json
{"text":"Heh! Hello. I am online and ready. eep!"}
```

### `POST /tts`

```json
{"text":"Heh! Hello. I am online and ready. eep!"}
```

Response:

```json
{"wav_path":"C:/.../brain_server/voicepack_cache/abc123.wav"}
```

### `GET /health`

```json
{"ok":true,"service":"flic_brain_server","stt":{},"llm":{},"tts":{},"time":0}
```

## 6. Firmware Integration Sequence
Flic should do:
1. Capture audio (PCM 16kHz mono)
2. `POST /stt`
3. `POST /respond` with recognized text
4. `POST /persona` with LLM reply
5. `POST /tts` with persona output
6. Play WAV from returned path (or copy/stream)

Expected firmware logs:
- `VOICE: stt_text=...`
- `VOICE: llm_reply=...`
- `VOICE: persona_output=...`
- `VOICE: wav_ready=...`

## 7. 24/7 and Remote Reachability
- Keep server local (`127.0.0.1`).
- Expose securely with Cloudflare Tunnel only.
- See `cloudflare_tunnel_setup.md`.

## 8. Start On Boot (Windows)
Use Task Scheduler so Brain Server survives reboots:
1. Open Task Scheduler and create a new task.
2. Trigger: At startup.
3. Action: Start a program.
4. Program/script: `cmd.exe`
5. Add arguments: `/c "C:\\Users\\jlors\\OneDrive\\Desktop\\Flic\\brain_server\\run_forever.bat"`
6. Enable "Run whether user is logged on or not" if desired.

This provides crash restart (inside script) and boot restart (via scheduler).
