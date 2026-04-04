# Flic Animation Builder (Flux + ComfyUI)

This package generates Flic eye animation frames with ComfyUI, validates frame constraints, mirrors right-eye frames, and exports to SD when available.

## 1) Install Python dependencies

```bash
pip install -r tools/animation_builder/requirements.txt
```

## 2) Install ComfyUI + Flux model

1. Install ComfyUI locally (portable installer script):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/animation_builder/install_comfyui.ps1
```

Or run the VS Code task: `AnimationBuilder: Install ComfyUI`

Installer behavior:
- Uses fixed install path `C:/Flic/ComfyUI`
- Installs/updates optional custom nodes used by some templates
- If ComfyUI or required template nodes are missing at runtime, the default automation path falls back to a built-in local renderer
2. Place Flux model files in your ComfyUI model directory (typically `ComfyUI/models/checkpoints` or your selected Flux loader path).
3. Start ComfyUI and keep it running:

```bash
python main.py --listen 127.0.0.1 --port 8188
```

Expected API base URL: `http://127.0.0.1:8188`

## 3) Wire the workflow template

Template file: `tools/animation_builder/flic_flux_eye_workflow.json`

Required node titles in that JSON:

- `STYLE_PROMPT`
- `NEG_PROMPT`
- `EYELID_NODE`
- `SEED`
- `IMAGE`

The generator injects style/negative prompts, eyelid parameters, and deterministic seed values per frame.

## 4) Verify ComfyUI connectivity

```bash
python -m tools.animation_builder.comfy_healthcheck
```

Outputs:
- `COMFY_READY` (exit code 0)
- `COMFY_DOWN` (exit code 1)

## 5) Generate animations

Single animation:

```bash
python -m tools.animation_builder.generate_animation blink --seed 12345
```

All specs immediately:

```bash
python -m tools.animation_builder.generate_animation all
```

Wait for ComfyUI and then run all specs:

```bash
python -m tools.animation_builder.run_when_comfy --all
```

Strict CI mode (fail if required Comfy workflow nodes/custom nodes are missing):

```bash
python -m tools.animation_builder.run_when_comfy --all --strict-comfy
```

VS Code strict task: `Start ComfyUI + Generate Animations (strict)`

Recommended production task: `Start ComfyUI + Generate Animations (auto)`

Wait for ComfyUI and generate one animation:

```bash
python -m tools.animation_builder.run_when_comfy --animation blink --seed 12345
```

## 6) Run full system validation

```bash
python -m tools.animation_builder.system_test
```

Checks performed:
- ComfyUI health check
- Blink generation (`seed=12345` by default)
- PNG presence
- PNG dimension validation (240x240)
- PNG max-size validation

Outputs:
- `SYSTEM_OK` on pass
- `SYSTEM_FAIL` with reason on failure

## Output locations

- Local build output: `build/animations/<animation>/000.png...`
- Mirrored output: `build/animations/<animation>_right/000.png...`
- SD export target: `D:/Flic/animations/face/default/<animation>/` (skipped with warning if unavailable)

## Included animation specs

- blink
- idle
- listening
- thinking
- happy
- sad
- speak
