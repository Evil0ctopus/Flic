const els = {
  connDot: document.getElementById('connDot'),
  connText: document.getElementById('connText'),
  btnConnect: document.getElementById('btnConnect'),
  btnClearLogs: document.getElementById('btnClearLogs'),
  logConsole: document.getElementById('logConsole'),
  toggleAutoConnect: document.getElementById('toggleAutoConnect'),
  toggleHeartbeat: document.getElementById('toggleHeartbeat'),
  toggleVerbose: document.getElementById('toggleVerbose'),
  toggleMock: document.getElementById('toggleMock'),
  voiceStyleSelect: document.getElementById('voiceStyleSelect'),
  btnApplyVoiceStyle: document.getElementById('btnApplyVoiceStyle'),
  voiceStyleStatus: document.getElementById('voiceStyleStatus'),
  toggleUseBrowserSpeech: document.getElementById('toggleUseBrowserSpeech'),
  btnSpeakOnce: document.getElementById('btnSpeakOnce'),
  voiceTranscript: document.getElementById('voiceTranscript'),
  voiceCommandStatus: document.getElementById('voiceCommandStatus'),

  imuEvent: document.getElementById('imuEvent'),
  imuDetail: document.getElementById('imuDetail'),
  lightEvent: document.getElementById('lightEvent'),
  lightDetail: document.getElementById('lightDetail'),
  touchGesture: document.getElementById('touchGesture'),
  touchMeaning: document.getElementById('touchMeaning'),
  emotionState: document.getElementById('emotionState'),
  animationState: document.getElementById('animationState'),
  uptime: document.getElementById('uptime'),
  lastUpdate: document.getElementById('lastUpdate'),
};

let ws = null;
let mockTimer = null;

async function sendVoiceCommand(text) {
  const payload = { text, source: 'web_speech' };
  try {
    const res = await fetch('/api/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const body = await res.text();
    els.voiceCommandStatus.textContent = res.ok ? 'sent' : `error ${res.status}`;
    logLine(`voice cmd -> ${text}`);
    if (els.toggleVerbose.checked) {
      logLine(`voice response ${body}`);
    }
  } catch (err) {
    els.voiceCommandStatus.textContent = 'send failed';
    logLine(`voice send failed: ${err}`, 'ERROR');
  }
}

async function applyVoiceStyle(style) {
  const normalized = (style || '').toLowerCase();
  if (!['natural', 'bright', 'deep'].includes(normalized)) {
    els.voiceStyleStatus.textContent = 'invalid';
    return;
  }

  const payload = {
    settings: {
      voice_style: normalized,
    },
  };

  try {
    const res = await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const body = await res.text();
    els.voiceStyleStatus.textContent = res.ok ? 'applied' : `error ${res.status}`;
    logLine(`voice style -> ${normalized}`);
    if (els.toggleVerbose.checked) {
      logLine(`settings response ${body}`);
    }
  } catch (err) {
    els.voiceStyleStatus.textContent = 'send failed';
    logLine(`voice style send failed: ${err}`, 'ERROR');
  }
}

function startSpeechOnce() {
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    els.voiceCommandStatus.textContent = 'speech API unavailable';
    logLine('Browser speech recognition not supported.', 'WARN');
    return;
  }

  const recognition = new SpeechRecognition();
  recognition.lang = 'en-US';
  recognition.interimResults = false;
  recognition.maxAlternatives = 1;

  els.voiceCommandStatus.textContent = 'listening';
  recognition.onresult = (event) => {
    const transcript = event.results?.[0]?.[0]?.transcript || '';
    const text = transcript.trim();
    els.voiceTranscript.textContent = text.length ? text : '-';
    if (text.length) {
      sendVoiceCommand(text);
    } else {
      els.voiceCommandStatus.textContent = 'no speech';
    }
  };

  recognition.onerror = () => {
    els.voiceCommandStatus.textContent = 'speech error';
  };

  recognition.onend = () => {
    if (els.voiceCommandStatus.textContent === 'listening') {
      els.voiceCommandStatus.textContent = 'idle';
    }
  };

  recognition.start();
}

function logLine(msg, level = 'INFO') {
  const ts = new Date().toLocaleTimeString();
  els.logConsole.textContent += `[${ts}] [${level}] ${msg}\n`;
  els.logConsole.scrollTop = els.logConsole.scrollHeight;
}

function setConnected(connected) {
  els.connDot.classList.toggle('on', connected);
  els.connDot.classList.toggle('off', !connected);
  els.connText.textContent = connected ? 'Connected' : 'Disconnected';
}

function setEngineStatus(name, on) {
  const row = document.querySelector(`[data-engine="${name}"]`);
  if (!row) return;
  const badge = row.querySelector('.badge');
  badge.textContent = on ? 'ON' : 'OFF';
  badge.classList.toggle('on', on);
  badge.classList.toggle('off', !on);
}

function updateFromPayload(payload) {
  const p = payload || {};
  const settings = p.settings || {};

  els.uptime.textContent = p.uptime_ms ?? '-';
  els.emotionState.textContent = p.emotion ?? els.emotionState.textContent;
  els.animationState.textContent = p.animation ?? (p.animation_playing ? 'playing' : 'idle');
  els.imuEvent.textContent = p.imu_event ?? '-';
  els.imuDetail.textContent = p.imu_detail ?? '-';
  els.lightEvent.textContent = p.light_event ?? '-';
  els.lightDetail.textContent = p.light_detail ?? '-';
  els.touchGesture.textContent = p.touch_gesture ?? '-';
  els.touchMeaning.textContent = p.touch_meaning ?? '-';
  els.lastUpdate.textContent = new Date().toLocaleTimeString();

  if (typeof settings.voice_style === 'string' && els.voiceStyleSelect) {
    const style = settings.voice_style.toLowerCase();
    if (['natural', 'bright', 'deep'].includes(style)) {
      els.voiceStyleSelect.value = style;
    }
  }

  const engines = p.engines || {};
  [
    'touch', 'imu', 'light', 'camera', 'audio', 'usb',
    'communication', 'emotion', 'animation', 'milestone', 'learning'
  ].forEach((name) => setEngineStatus(name, !!engines[name]));
}

function parseMessage(raw) {
  try {
    return JSON.parse(raw);
  } catch {
    return null;
  }
}

function connectWs() {
  if (els.toggleMock.checked) {
    startMockMode();
    return;
  }

  if (ws) {
    ws.close();
    ws = null;
  }

  const host = location.hostname || '127.0.0.1';
  const url = `ws://${host}:81/`;
  logLine(`Connecting to ${url}`);

  ws = new WebSocket(url);

  ws.onopen = () => {
    setConnected(true);
    logLine('WebSocket opened');
    ws.send('state');
  };

  ws.onclose = () => {
    setConnected(false);
    logLine('WebSocket closed', 'WARN');
  };

  ws.onerror = () => {
    logLine('WebSocket error', 'ERROR');
  };

  ws.onmessage = (evt) => {
    const parsed = parseMessage(evt.data);
    if (!parsed) {
      if (els.toggleVerbose.checked) logLine(`RAW ${evt.data}`);
      return;
    }

    if (parsed.type === 'heartbeat' && !els.toggleHeartbeat.checked) {
      return;
    }

    updateFromPayload(parsed);
    if (els.toggleVerbose.checked || parsed.type === 'heartbeat') {
      logLine(`${parsed.type || 'event'} ${JSON.stringify(parsed)}`);
    }
  };
}

function startMockMode() {
  stopMockMode();
  setConnected(true);
  logLine('Mock data mode enabled', 'WARN');

  let tick = 0;
  mockTimer = setInterval(() => {
    tick += 1;
    const payload = {
      type: 'heartbeat',
      uptime_ms: tick * 1000,
      emotion: ['calm', 'curious', 'happy', 'sleepy'][tick % 4],
      animation_playing: tick % 2 === 0,
      animation: tick % 2 === 0 ? 'thinking_loop' : 'idle_breathing',
      imu_event: tick % 3 === 0 ? 'shake' : 'stillness',
      imu_detail: `${(Math.random() * 3).toFixed(2)}`,
      light_event: tick % 4 === 0 ? 'hand_wave' : 'steady',
      light_detail: 'mock',
      touch_gesture: tick % 5 === 0 ? 'tap' : '-',
      touch_meaning: tick % 5 === 0 ? 'acknowledge' : '-',
      engines: {
        touch: true, imu: true, light: true, camera: true, audio: true, usb: true,
        communication: true, emotion: true, animation: true, milestone: true, learning: true,
      },
    };
    updateFromPayload(payload);
    if (els.toggleHeartbeat.checked) {
      logLine(`heartbeat ${JSON.stringify(payload)}`);
    }
  }, 1000);
}

function stopMockMode() {
  if (mockTimer) {
    clearInterval(mockTimer);
    mockTimer = null;
  }
}

els.btnConnect.addEventListener('click', () => {
  if (els.toggleMock.checked) {
    startMockMode();
  } else {
    stopMockMode();
    connectWs();
  }
});

els.toggleMock.addEventListener('change', () => {
  if (els.toggleMock.checked) {
    if (ws) ws.close();
    startMockMode();
  } else {
    stopMockMode();
    setConnected(false);
  }
});

els.btnClearLogs.addEventListener('click', () => {
  els.logConsole.textContent = '';
});

els.btnSpeakOnce.addEventListener('click', () => {
  if (!els.toggleUseBrowserSpeech.checked) {
    els.voiceCommandStatus.textContent = 'disabled';
    return;
  }
  startSpeechOnce();
});

if (els.btnApplyVoiceStyle) {
  els.btnApplyVoiceStyle.addEventListener('click', () => {
    applyVoiceStyle(els.voiceStyleSelect?.value || 'natural');
  });
}

if (els.toggleAutoConnect.checked) {
  connectWs();
}
