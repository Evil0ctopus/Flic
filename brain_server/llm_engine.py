import json
from typing import Any, Dict

import requests

MODEL_NAME = "llama3.1:8b"


class OllamaEngine:
    def __init__(self, config: Dict[str, Any], logger):
        self.config = config
        self.logger = logger
        # Local-only endpoint. No cloud provider routing is supported.
        self.base_url = "http://localhost:11434"
        self.model = str(config.get("LLM_MODEL", config.get("model", MODEL_NAME)))
        self.provider = str(config.get("LLM_PROVIDER", "ollama")).lower()
        self.use_cloud = bool(config.get("USE_CLOUD", False))
        self.timeout_seconds = int(config.get("timeout_seconds", 40))
        self.max_reply_chars = int(config.get("max_reply_chars", 180))
        self.ollama_options = dict(config.get("ollama_options", {}))

        if self.provider != "ollama":
            raise ValueError(f"Unsupported LLM provider: {self.provider}. Only 'ollama' is allowed.")
        if self.use_cloud:
            raise ValueError("USE_CLOUD must be false. Cloud LLM providers are disabled.")

    def status(self) -> Dict[str, Any]:
        return {
            "provider": self.provider,
            "use_cloud": self.use_cloud,
            "base_url": self.base_url,
            "model": self.model,
        }

    def generate_local(self, user_text: str) -> Dict[str, str]:
        clean = (user_text or "").strip()
        if not clean:
            return {"intent": "none", "reply": ""}

        prompt = self._build_prompt(clean)
        payload = {
            "model": self.model,
            "prompt": prompt,
            "stream": False,
            "format": "json",
            "options": {
                "temperature": 0.4,
                "top_p": 0.9,
                "num_predict": 120,
                "repeat_penalty": 1.1,
                "num_gpu": 1,
                **self.ollama_options,
            },
        }

        url = f"{self.base_url}/api/generate"
        try:
            res = requests.post(url, json=payload, timeout=self.timeout_seconds)
            res.raise_for_status()
            response_json = res.json()
            raw = str(response_json.get("response", "")).strip()
            parsed = self._parse_model_json(raw)
            parsed["reply"] = self._clamp_reply(parsed.get("reply", ""))
            self.logger.info("llm_ok intent=%s reply_len=%s", parsed.get("intent", "unknown"), len(parsed.get("reply", "")))
            return parsed
        except Exception as exc:
            self.logger.error("llm_failed error=%s", exc)
            return {
                "intent": "fallback",
                "reply": "I heard you. Give me a second and try again.",
            }

    def respond(self, user_text: str) -> Dict[str, str]:
        # Backward-compatible alias for older callers.
        return self.generate_local(user_text)

    def _build_prompt(self, text: str) -> str:
        return (
            "You are the high-level cognition engine for a small robot named Flic. "
            "Return strict JSON only with keys intent and reply. "
            "Reply must be safe, concise, and one or two short sentences. "
            "Intent should be a lowercase snake_case label.\n"
            f"User text: {text}"
        )

    def _parse_model_json(self, raw: str) -> Dict[str, str]:
        if not raw:
            return {"intent": "unknown", "reply": ""}
        try:
            parsed = json.loads(raw)
            intent = str(parsed.get("intent", "unknown")).strip() or "unknown"
            reply = str(parsed.get("reply", "")).strip()
            return {"intent": intent, "reply": reply}
        except Exception:
            return {"intent": "unparsed", "reply": raw}

    def _clamp_reply(self, reply: str) -> str:
        reply = (reply or "").strip()
        if len(reply) <= self.max_reply_chars:
            return reply
        clipped = reply[: self.max_reply_chars].rstrip()
        if not clipped.endswith("."):
            clipped += "."
        return clipped
