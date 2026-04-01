# Flic Philosophy

Flic keeps firmware minimal, deterministic, and hardware-focused. Core device behavior lives in firmware startup, device bring-up, and visual boot signaling, while any evolving or dynamic behavior belongs in the `ai/` layer.

Principles:
- Firmware stays simple and predictable.
- Device initialization is explicit and safe.
- AI-owned behavior is isolated from firmware.
- Boot-time UI remains lightweight and fast.
