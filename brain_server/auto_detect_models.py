import json
import shutil
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional

BASE_DIR = Path(__file__).resolve().parent
CONFIG_PATH = BASE_DIR / "config.json"


def _load_config(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _save_config(path: Path, config: Dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8") as f:
        json.dump(config, f, indent=2)
        f.write("\n")


def _find_first_existing(paths: List[str]) -> Optional[str]:
    for p in paths:
        if Path(p).exists():
            return p.replace("\\", "/")
    return None


def detect_whisper_model(config: Dict[str, Any]) -> Optional[str]:
    candidates = [
        str(config.get("WHISPER_MODEL_PATH", "")),
        str(config.get("stt", {}).get("model_path", "")),
        "C:/ai/models/whisper/ggml-base.en.bin",
        "C:/ai/models/whisper/ggml-small.en.bin",
    ]
    valid = [c for c in candidates if c and c != "None"]
    found = _find_first_existing(valid)
    if found:
        return found

    roots = [Path("C:/ai/models/whisper"), Path("C:/models/whisper"), Path.home()]
    for root in roots:
        if root.exists():
            for bin_file in root.rglob("*.bin"):
                return str(bin_file).replace("\\", "/")
    return None


def detect_piper_model(config: Dict[str, Any]) -> Optional[str]:
    candidates = [
        str(config.get("PIPER_MODEL_PATH", "")),
        str(config.get("tts", {}).get("model_path", "")),
        "C:/PiperPreview/models/en_US-lessac-medium.onnx",
    ]
    valid = [c for c in candidates if c and c != "None"]
    found = _find_first_existing(valid)
    if found:
        return found

    roots = [Path("C:/PiperPreview/models"), Path("C:/ai/models/piper"), Path.home()]
    for root in roots:
        if root.exists():
            for onnx_file in root.rglob("*.onnx"):
                return str(onnx_file).replace("\\", "/")
    return None


def detect_whisper_binary(config: Dict[str, Any]) -> Optional[str]:
    candidates = [
        str(config.get("stt", {}).get("binary_path", "")),
        "C:/ai/whisper.cpp/build/bin/whisper-cli.exe",
    ]
    valid = [c for c in candidates if c and c != "None"]
    found = _find_first_existing(valid)
    if found:
        return found

    cmd = shutil.which("whisper-cli") or shutil.which("whisper-cli.exe")
    if cmd:
        return cmd.replace("\\", "/")
    return None


def detect_piper_binary(config: Dict[str, Any]) -> Optional[str]:
    candidates = [
        str(config.get("tts", {}).get("binary_path", "")),
        str(BASE_DIR.parent / ".venv/Scripts/piper.exe"),
    ]
    valid = [c for c in candidates if c and c != "None"]
    found = _find_first_existing(valid)
    if found:
        return found

    cmd = shutil.which("piper") or shutil.which("piper.exe")
    if cmd:
        return cmd.replace("\\", "/")
    return None


def detect_ollama_models() -> List[str]:
    try:
        proc = subprocess.run(
            ["ollama", "list"],
            capture_output=True,
            text=True,
            check=True,
            timeout=20,
        )
    except Exception:
        return []

    models: List[str] = []
    for line in proc.stdout.splitlines()[1:]:
        line = line.strip()
        if not line:
            continue
        model = line.split()[0]
        models.append(model)
    return models


def pick_llm_model(installed: List[str], preferred: str = "llama3.1:8b") -> str:
    if preferred in installed:
        return preferred
    if installed:
        return installed[0]
    return preferred


def main() -> int:
    config = _load_config(CONFIG_PATH)

    whisper_model = detect_whisper_model(config)
    piper_model = detect_piper_model(config)
    whisper_bin = detect_whisper_binary(config)
    piper_bin = detect_piper_binary(config)
    ollama_models = detect_ollama_models()

    config["LLM_PROVIDER"] = "ollama"
    config["USE_CLOUD"] = False
    config["LLM_MODEL"] = pick_llm_model(ollama_models, "llama3.1:8b")

    config.setdefault("llm", {})["LLM_PROVIDER"] = "ollama"
    config["llm"]["USE_CLOUD"] = False
    config["llm"]["LLM_MODEL"] = config["LLM_MODEL"]
    config["llm"]["model"] = config["LLM_MODEL"]
    config["llm"]["base_url"] = "http://localhost:11434"

    config.setdefault("stt", {})
    config.setdefault("tts", {})

    if whisper_model:
        config["WHISPER_MODEL_PATH"] = whisper_model
        config["stt"]["model_path"] = whisper_model
    if whisper_bin:
        config["stt"]["binary_path"] = whisper_bin

    if piper_model:
        config["PIPER_MODEL_PATH"] = piper_model
        config["tts"]["model_path"] = piper_model
    if piper_bin:
        config["tts"]["binary_path"] = piper_bin

    _save_config(CONFIG_PATH, config)

    print(json.dumps(
        {
            "LLM_PROVIDER": config.get("LLM_PROVIDER"),
            "LLM_MODEL": config.get("LLM_MODEL"),
            "USE_CLOUD": config.get("USE_CLOUD"),
            "WHISPER_MODEL_PATH": config.get("WHISPER_MODEL_PATH"),
            "PIPER_MODEL_PATH": config.get("PIPER_MODEL_PATH"),
            "ollama_models_detected": ollama_models,
        },
        indent=2,
    ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
