import subprocess
import tempfile
import wave
from pathlib import Path
from typing import Any, Dict


class WhisperCppEngine:
    def __init__(self, config: Dict[str, Any], logger):
        self.config = config
        self.logger = logger
        self.model_path = str(config.get("model_path", "models/whisper/ggml-base.en.bin"))
        self.binary_path = str(config.get("binary_path", "whisper-cli"))
        self.language = str(config.get("language", "en"))
        self.threads = int(config.get("threads", 6))

    def status(self) -> Dict[str, Any]:
        binary = Path(self.binary_path)
        model = Path(self.model_path)
        return {
            "model_path": self.model_path,
            "binary_path": self.binary_path,
            "binary_exists": binary.exists(),
            "model_exists": model.exists(),
        }

    def transcribe(self, audio_bytes: bytes, sample_rate: int = 16000) -> str:
        if not audio_bytes:
            return ""

        binary = Path(self.binary_path)
        model = Path(self.model_path)
        if not binary.exists():
            raise RuntimeError(f"Whisper binary not found: {self.binary_path}")
        if not model.exists():
            raise RuntimeError(f"Whisper model not found: {self.model_path}")

        with tempfile.TemporaryDirectory(prefix="flic_stt_") as tmpdir:
            wav_path = Path(tmpdir) / "input.wav"
            out_base = Path(tmpdir) / "result"

            self._write_pcm16_mono_wav(wav_path, audio_bytes, sample_rate)
            cmd = [
                self.binary_path,
                "-m",
                self.model_path,
                "-f",
                str(wav_path),
                "-l",
                self.language,
                "-t",
                str(self.threads),
                "-otxt",
                "-of",
                str(out_base),
                "-np",
            ]

            self.logger.info("stt_start cmd=%s", " ".join(cmd))
            try:
                result = subprocess.run(cmd, capture_output=True, text=True)
            except FileNotFoundError as exc:
                self.logger.error("stt_binary_missing error=%s", exc)
                raise RuntimeError(f"Whisper binary not executable: {self.binary_path}") from exc
            if result.returncode != 0:
                self.logger.error("stt_failed rc=%s stderr=%s", result.returncode, result.stderr.strip())
                raise RuntimeError("Whisper transcription failed")

            txt_path = out_base.with_suffix(".txt")
            if txt_path.exists():
                text = txt_path.read_text(encoding="utf-8", errors="ignore").strip()
            else:
                text = (result.stdout or "").strip()

            self.logger.info("stt_done text_len=%s", len(text))
            return text

    @staticmethod
    def _write_pcm16_mono_wav(path: Path, pcm_bytes: bytes, sample_rate: int) -> None:
        with wave.open(str(path), "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sample_rate)
            wf.writeframes(pcm_bytes)
