# Cloudflare Tunnel Setup (No Port Forwarding)

This setup keeps your Brain Server private on localhost and exposes it securely via Cloudflare.
Cloudflare Tunnel is transport-only in this architecture. All AI execution remains local (Whisper + Ollama + Piper).

## 1. Preconditions
- Cloudflare account with a domain connected to Cloudflare.
- Brain Server running on `127.0.0.1:8000`.
- `cloudflared` installed on the PC.

## 2. Install cloudflared
- Windows: download from Cloudflare and place `cloudflared.exe` in PATH.
- Linux/macOS: install from package manager or Cloudflare binary.

## 3. Login and create tunnel
1. `cloudflared tunnel login`
2. `cloudflared tunnel create flic-brain`

## 4. Create tunnel config
Create `%USERPROFILE%\\.cloudflared\\config.yml` (Windows) or `~/.cloudflared/config.yml` (Linux/macOS):

```yaml
tunnel: flic-brain
credentials-file: C:\\Users\\YOUR_USER\\.cloudflared\\<TUNNEL_ID>.json

ingress:
  - hostname: brain.yourdomain.com
    service: https://127.0.0.1:8000
    originRequest:
      noTLSVerify: true
  - service: http_status:404
```

If your local Brain Server runs without TLS, use:

```yaml
service: http://127.0.0.1:8000
```

## 5. Route DNS
Run:

`cloudflared tunnel route dns flic-brain brain.yourdomain.com`

## 6. Run tunnel
Foreground test:

`cloudflared tunnel run flic-brain`

Install as service for persistence:

`cloudflared service install`

## 7. Flic connection details
- Base URL for firmware: `https://brain.yourdomain.com`
- Send API token in header:
  - `Authorization: Bearer <token>`

## 8. Security checklist
- Keep Brain Server bound to `127.0.0.1` only.
- Use a long random API token in `config.json`.
- Rotate token periodically.
- Review `brain_server/logs/brain_server.log` for failed auth attempts.
