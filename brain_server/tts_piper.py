import hashlib
import subprocess
import wave
from pathlib import Path
from typing import Any, Dict


class PiperTtsEngine:
    def __init__(self, config: Dict[str, Any], logger):
        self.config = config
        self.logger = logger
        self.binary_path = str(config.get("binary_path", "piper"))
        self.model_path = str(config.get("model_path", "models/piper/en_US-lessac-medium.onnx"))
        self.sample_rate = int(config.get("sample_rate", 22050))
        self.cache_dir = Path(config.get("cache_dir", "voicepack_cache")).resolve()
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def status(self) -> Dict[str, Any]:
        return {
            "binary_path": self.binary_path,
            "model_path": self.model_path,
            "cache_dir": str(self.cache_dir),
        }

    def synthesize(self, text: str) -> str:
        clean = (text or "").strip()
        if not clean:
            raise ValueError("TTS text is empty")

        out_path = self.cache_dir / f"{self._cache_key(clean)}.wav"
        if out_path.exists() and self._is_valid_wav(out_path):
            self.logger.info("tts_cache_hit file=%s", out_path)
            return str(out_path)

        cmd = [
            self.binary_path,
            "--model",
            self.model_path,
            "--output_file",
            str(out_path),
        ]
        self.logger.info("tts_start cmd=%s", " ".join(cmd))
        proc = subprocess.run(cmd, input=clean, text=True, capture_output=True)
        if proc.returncode != 0:
            self.logger.error("tts_failed rc=%s stderr=%s", proc.returncode, proc.stderr.strip())
            raise RuntimeError("Piper synthesis failed")

        if not self._is_valid_wav(out_path):
            raise RuntimeError("Generated WAV is invalid")

        self.logger.info("tts_done file=%s", out_path)
        return str(out_path)

    def _cache_key(self, text: str) -> str:
        payload = f"{self.model_path}|{self.sample_rate}|{text}".encode("utf-8")
        return hashlib.sha256(payload).hexdigest()[:24]

    def _is_valid_wav(self, path: Path) -> bool:
        try:
            with wave.open(str(path), "rb") as wf:
                return (
                    wf.getnchannels() == 1
                    and wf.getsampwidth() == 2
                    and wf.getframerate() == self.sample_rate
                    and wf.getnframes() > 0
                )
        except Exception:
            return False
