import json
import logging
import logging.handlers
import os
import time
from collections import defaultdict, deque
from pathlib import Path
from typing import Any, Deque, Dict, Optional

import uvicorn
from fastapi import Depends, FastAPI, File, Form, Header, HTTPException, Request, UploadFile, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, Field

from llm_engine import OllamaEngine
from personality_transform import StitchPersonaTransformer
from stt_whisper import WhisperCppEngine
from tts_piper import PiperTtsEngine


BASE_DIR = Path(__file__).resolve().parent
CONFIG_PATH = Path(os.environ.get("BRAIN_CONFIG", str(BASE_DIR / "config.json")))


def load_config(path: Path) -> Dict[str, Any]:
    if not path.exists():
        raise RuntimeError(f"Config file not found: {path}")
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


CONFIG = load_config(CONFIG_PATH)


def build_llm_config(config: Dict[str, Any]) -> Dict[str, Any]:
    llm_cfg = dict(config.get("llm", {}))
    if "LLM_PROVIDER" in config:
        llm_cfg["LLM_PROVIDER"] = config["LLM_PROVIDER"]
    if "LLM_MODEL" in config:
        llm_cfg["LLM_MODEL"] = config["LLM_MODEL"]
    if "USE_CLOUD" in config:
        llm_cfg["USE_CLOUD"] = config["USE_CLOUD"]
    return llm_cfg


def build_stt_config(config: Dict[str, Any]) -> Dict[str, Any]:
    stt_cfg = dict(config.get("stt", {}))
    if config.get("WHISPER_MODEL_PATH"):
        stt_cfg["model_path"] = config["WHISPER_MODEL_PATH"]
    return stt_cfg


def build_tts_config(config: Dict[str, Any]) -> Dict[str, Any]:
    tts_cfg = dict(config.get("tts", {}))
    if config.get("PIPER_MODEL_PATH"):
        tts_cfg["model_path"] = config["PIPER_MODEL_PATH"]
    return tts_cfg


def configure_logging() -> logging.Logger:
    log_cfg = CONFIG.get("logging", {})
    log_dir = BASE_DIR / log_cfg.get("dir", "logs")
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / log_cfg.get("file", "brain_server.log")
    level_name = log_cfg.get("level", "INFO").upper()
    level = getattr(logging, level_name, logging.INFO)

    logger = logging.getLogger("brain_server")
    logger.setLevel(level)
    logger.handlers.clear()

    formatter = logging.Formatter("%(asctime)s %(levelname)s %(name)s %(message)s")
    rotating = logging.handlers.RotatingFileHandler(
        log_file,
        maxBytes=int(log_cfg.get("max_bytes", 5_000_000)),
        backupCount=int(log_cfg.get("backup_count", 5)),
        encoding="utf-8",
    )
    rotating.setFormatter(formatter)

    console = logging.StreamHandler()
    console.setFormatter(formatter)

    logger.addHandler(rotating)
    logger.addHandler(console)
    logger.info("Brain Server logging initialized")
    return logger


LOGGER = configure_logging()


class RateLimiter:
    def __init__(self, max_requests: int, window_seconds: int) -> None:
        self.max_requests = max_requests
        self.window_seconds = window_seconds
        self._events: Dict[str, Deque[float]] = defaultdict(deque)

    def allow(self, key: str) -> bool:
        now = time.time()
        q = self._events[key]
        cutoff = now - self.window_seconds
        while q and q[0] < cutoff:
            q.popleft()
        if len(q) >= self.max_requests:
            return False
        q.append(now)
        return True


SECURITY_CFG = CONFIG.get("security", {})
RATE_LIMITER = RateLimiter(
    max_requests=int(SECURITY_CFG.get("rate_limit", {}).get("requests", 60)),
    window_seconds=int(SECURITY_CFG.get("rate_limit", {}).get("window_seconds", 60)),
)
API_TOKEN = str(CONFIG.get("API_TOKEN", SECURITY_CFG.get("api_token", "CHANGE_ME")))


STT_ENGINE = WhisperCppEngine(build_stt_config(CONFIG), LOGGER)
LLM_ENGINE = OllamaEngine(build_llm_config(CONFIG), LOGGER)
PERSONA_ENGINE = StitchPersonaTransformer(CONFIG.get("persona", {}), LOGGER)
TTS_ENGINE = PiperTtsEngine(build_tts_config(CONFIG), LOGGER)


app = FastAPI(title="Flic Brain Server", version="1.0.0")


@app.middleware("http")
async def access_log_and_rate_limit(request: Request, call_next):
    client = request.client.host if request.client else "unknown"
    route = request.url.path
    limiter_key = f"{client}:{route}"
    if not RATE_LIMITER.allow(limiter_key):
        LOGGER.warning("rate_limit_exceeded ip=%s route=%s", client, route)
        raise HTTPException(status_code=429, detail="Rate limit exceeded")

    start = time.time()
    response = await call_next(request)
    duration_ms = int((time.time() - start) * 1000)
    LOGGER.info(
        "access ip=%s method=%s route=%s status=%s duration_ms=%s",
        client,
        request.method,
        route,
        response.status_code,
        duration_ms,
    )
    return response


def require_token(
    authorization: Optional[str] = Header(default=None),
    x_api_token: Optional[str] = Header(default=None),
) -> str:
    supplied = None
    if authorization and authorization.startswith("Bearer "):
        supplied = authorization.replace("Bearer ", "", 1).strip()
    elif x_api_token:
        supplied = x_api_token.strip()

    if not supplied or supplied != API_TOKEN:
        LOGGER.warning("auth_failed authorization_present=%s x_api_present=%s", bool(authorization), bool(x_api_token))
        raise HTTPException(status_code=401, detail="Unauthorized")
    return supplied


class RespondRequest(BaseModel):
    text: str = Field(min_length=1, max_length=2000)


class PersonaRequest(BaseModel):
    text: str = Field(min_length=1, max_length=2000)
    emotion: str = Field(default="curious", max_length=64)


class TtsRequest(BaseModel):
    text: str = Field(min_length=1, max_length=2000)


@app.get("/health")
def health(_: str = Depends(require_token)) -> Dict[str, Any]:
    return {
        "ok": True,
        "service": "flic_brain_server",
        "stt": STT_ENGINE.status(),
        "llm": LLM_ENGINE.status(),
        "tts": TTS_ENGINE.status(),
        "time": int(time.time()),
    }


@app.post("/stt")
async def stt(
    _: str = Depends(require_token),
    audio: UploadFile = File(...),
    sample_rate: int = Form(default=16000),
) -> Dict[str, Any]:
    raw = await audio.read()
    if not raw:
        raise HTTPException(status_code=400, detail="Empty audio payload")
    try:
        text = STT_ENGINE.transcribe(raw, sample_rate=sample_rate)
    except RuntimeError as exc:
        LOGGER.error("stt_unavailable error=%s", exc)
        raise HTTPException(status_code=503, detail=str(exc))
    return {"text": text}


@app.post("/respond")
def respond(payload: RespondRequest, _: str = Depends(require_token)) -> Dict[str, Any]:
    return LLM_ENGINE.generate_local(payload.text)


@app.post("/persona")
def persona(payload: PersonaRequest, _: str = Depends(require_token)) -> Dict[str, Any]:
    transformed = PERSONA_ENGINE.transform(payload.text, payload.emotion)
    return {"text": transformed}


@app.post("/tts")
def tts(payload: TtsRequest, _: str = Depends(require_token)) -> Dict[str, Any]:
    wav_path = TTS_ENGINE.synthesize(payload.text)
    return {"wav_path": wav_path}


@app.websocket("/ws")
async def ws_endpoint(websocket: WebSocket):
    token = websocket.query_params.get("token", "")
    client = websocket.client.host if websocket.client else "unknown"
    if token != API_TOKEN:
        LOGGER.warning("ws_auth_failed ip=%s", client)
        await websocket.close(code=1008)
        return

    if not RATE_LIMITER.allow(f"{client}:/ws"):
        LOGGER.warning("ws_rate_limit_exceeded ip=%s", client)
        await websocket.close(code=1013)
        return

    await websocket.accept()
    LOGGER.info("ws_connected ip=%s", client)
    try:
        while True:
            message = await websocket.receive_json()
            action = str(message.get("action", "")).strip().lower()

            if action == "health":
                await websocket.send_json({"type": "health", "data": health(API_TOKEN)})
            elif action == "respond":
                text = str(message.get("text", "")).strip()
                await websocket.send_json({"type": "respond", "data": LLM_ENGINE.generate_local(text)})
            elif action == "persona":
                text = str(message.get("text", "")).strip()
                emotion = str(message.get("emotion", "curious")).strip()
                await websocket.send_json({"type": "persona", "data": {"text": PERSONA_ENGINE.transform(text, emotion)}})
            elif action == "tts":
                text = str(message.get("text", "")).strip()
                await websocket.send_json({"type": "tts", "data": {"wav_path": TTS_ENGINE.synthesize(text)}})
            else:
                await websocket.send_json({"type": "error", "error": "Unknown action"})
    except WebSocketDisconnect:
        LOGGER.info("ws_disconnected ip=%s", client)


def main() -> None:
    server_cfg = CONFIG.get("server", {})
    uvicorn_cfg: Dict[str, Any] = {
        "app": "server:app",
        "host": server_cfg.get("host", "127.0.0.1"),
        "port": int(server_cfg.get("port", 8443)),
        "reload": False,
        "workers": 1,
        "log_level": "info",
    }

    tls_cfg = server_cfg.get("tls", {})
    certfile = tls_cfg.get("certfile", "")
    keyfile = tls_cfg.get("keyfile", "")
    if certfile and keyfile:
        uvicorn_cfg["ssl_certfile"] = certfile
        uvicorn_cfg["ssl_keyfile"] = keyfile
        LOGGER.info("TLS enabled cert=%s key=%s", certfile, keyfile)
    else:
        LOGGER.warning("TLS disabled in app server config; secure remote access must be via Cloudflare Tunnel")

    uvicorn.run(**uvicorn_cfg)


if __name__ == "__main__":
    main()
