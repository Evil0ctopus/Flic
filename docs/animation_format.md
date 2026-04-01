# Flic Animation Format

Flic animations are stored as safe JSON documents in `ai/animations`.

## Schema

{
  "name": "string",
  "fps": 10,
  "frames": [
    {
      "duration": 100,
      "pixels": [
        {"x": 10, "y": 20, "color": "#FFFFFF"},
        {"x": 11, "y": 20, "color": "#AAAAAA"}
      ]
    }
  ]
}

## Rules
- No executable code.
- No dynamic imports.
- Only pixel data and timing.
- Store animation files in `ai/animations`.
- Invalid files must be rejected safely by firmware.

## Milestone Hook
- When `flic_first_animation.json` is created, AI may record the `first_animation` milestone in `ai/memory/milestone_state.json`.
- Firmware should only observe or consume the resulting JSON state; it should not author AI memory directly.
