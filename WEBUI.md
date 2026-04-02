# Flic WebUI Guide

The WebUI now includes a left navigation sidebar with dedicated panels:
- SD Card Manager
- Face Animation Tools
- Logs & Diagnostics

## SD Card Manager
The **SD Card Manager** panel supports:
- Recursive directory browsing (`/api/sd/list`)
- Breadcrumb path navigation
- Upload (including drag/drop) with progress (`/api/sd/upload`)
- Delete (`/api/sd/delete`)
- Create folder (`/api/sd/mkdir`)
- Rename (`/api/sd/rename`)
- Download (`/api/sd/download`)
- PNG file preview action
- SD mounted/free-space status indicators

Notes:
- Paths are restricted to `/Flic` and `/ai` roots.
- Path traversal (`..`) is rejected.
- File operations are logged to `/Flic/logs/webui_sd.log`.

## Face Animation Tools
The **Face Animation Tools** panel supports:
- Animation discovery from SD metadata/index (`/api/face/animations`)
- Device preview trigger (`/api/face/preview`)
- Device play trigger (`/api/face/play`)
- Emotion trigger (`/api/face/set_emotion`)
- Hot reload of active style (`/api/face/reload`)
- Validation workflow (`/api/face/validate`)
- Live snapshot stream endpoint (`/api/face/snapshot`)
- Frame-level actions (view/replace/delete)
- Built-in emotion quick buttons (Neutral, Listening, Thinking, Speaking, Happy, Sad, Surprise, Tired)

## PNG Validation Workflow
PNG uploads are validated for:
- Name pattern: `frame_XXX.png`
- Dimensions: `240x240`
- PNG color type: RGBA (alpha-capable)
- Alpha-capable requirement for transparent-background animation frames

Invalid files are rejected and removed.

## Snapshot Preview
The live snapshot uses `/api/face/snapshot` and updates at ~10 FPS in the browser.

## Logs Panel
The **Logs & Diagnostics** panel supports:
- Subsystem filter buttons and text filter
- Auto-refresh
- Download full log
- Clear log (`POST /api/logs/clear`)

Endpoints:
- `GET /api/logs`
- `POST /api/logs/clear`
