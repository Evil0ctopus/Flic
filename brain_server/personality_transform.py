import random
import re
from typing import Any, Dict, List


class StitchPersonaTransformer:
    def __init__(self, config: Dict[str, Any], logger):
        self.config = config
        self.logger = logger
        self.enabled = bool(config.get("enabled", True))
        self.max_chars = int(config.get("max_chars", 180))
        self.interjections: List[str] = list(config.get("interjections", ["Heh!", "Eep!", "Ooh!"]))
        self.noises: List[str] = list(config.get("noises", ["eep", "heh", "ooh", "huh"]))
        self.filler_pattern = re.compile(r"\b(um|uh|like|you know|actually|basically|literally)\b", re.IGNORECASE)

    def transform(self, text: str, emotion: str = "curious") -> str:
        original = (text or "").strip()
        if not self.enabled or not original:
            return original

        cleaned = self._drop_fillers(original)
        cleaned = self._shorten_sentences(cleaned)
        cleaned = self._inject_interjection(cleaned)
        cleaned = self._add_playful_repetition(cleaned)
        cleaned = self._append_noise(cleaned, emotion)
        cleaned = self._clamp(cleaned)
        self.logger.info("persona_done in_len=%s out_len=%s", len(original), len(cleaned))
        return cleaned

    def _drop_fillers(self, text: str) -> str:
        text = self.filler_pattern.sub("", text)
        text = re.sub(r"\s+", " ", text)
        return text.strip(" ,")

    def _shorten_sentences(self, text: str) -> str:
        parts = re.split(r"(?<=[.!?])\s+", text)
        compact = []
        for part in parts[:3]:
            words = part.split()
            if len(words) > 12:
                part = " ".join(words[:12]) + "..."
            compact.append(part)
        return " ".join(compact).strip()

    def _inject_interjection(self, text: str) -> str:
        if not self.interjections:
            return text
        interjection = random.choice(self.interjections)
        if text.startswith(tuple(self.interjections)):
            return text
        return f"{interjection} {text}"

    def _add_playful_repetition(self, text: str) -> str:
        words = text.split()
        if len(words) < 4:
            return text
        key = words[min(2, len(words) - 1)]
        if len(key) >= 4 and random.random() < 0.35:
            return f"{text} {key}, {key}."
        return text

    def _append_noise(self, text: str, emotion: str) -> str:
        if not self.noises:
            return text
        if emotion.lower() in {"curious", "happy", "surprised"}:
            return f"{text} {random.choice(self.noises)}!"
        return text

    def _clamp(self, text: str) -> str:
        text = re.sub(r"\s+", " ", text).strip()
        if len(text) <= self.max_chars:
            return text
        clipped = text[: self.max_chars].rstrip()
        if not clipped.endswith((".", "!", "?")):
            clipped += "..."
        return clipped
