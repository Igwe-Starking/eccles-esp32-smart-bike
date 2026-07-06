// Eccles Smart Bike — app.js
// Binary protocol encoder + full UI logic
// Frame layout (see Executor.h BinaryCommand::parse):
//   [0] id      1 byte   command id
//   [1] action  1 byte   CommandAction enum
//   [2] target  1 byte   DeviceID enum
//   [3] delay   1 byte   seconds, 0 = immediate
//   [4] interval 1 byte  0 = one-shot
//   [5] dataType 1 byte  0 = none, COND_MGC=21, CONF_MGC_*, MNT_MGC=26
//   [6-7] size  2 bytes  big-endian data length
//   [8..] data  n bytes  optional payload

const Action = Object.freeze({
  NO_OP:0, ON:1, OFF:2, ENABLE:3, TOGGLE:4, DISABLE:5,
  SILENCE:6, VOICE:7, QUERY_ON:8, QUERY_OFF:9, QUERY_STATE:10,
  GET_STATE:11, QUERY_DATA:12, GET_DATA:13, VOICE_DATA:14,
  QUERY_DATA_G:15, QUERY_DATA_L:16, WRITE:17, READ:18,
  START_AI:19, START_REAL:20, CANCEL:21,
  NEXT:22, PLAY:23, PAUSE:24, SET_VOLUME:25, VOLUME_UP:26, VOLUME_DOWN:27, PREV:28
});

const Target = Object.freeze({
  UNKNOWN:0,
  IGNITION:1, HORN:2, HEADLAMP:3, LEFT_TURN:4, RIGHT_TURN:5, STARTER:6, ENGINE:7,
  IGNITION_FB:8, FUEL_GAUGE:9, TEMP_GAUGE:10, MICROPHONE:11,
  SHOCK_SENSOR:12, ALL:13,
  CONFIG:14, BLUETOOTH:15, CONVERSATION:16
});

// word -> Target map for the text parser
const WORD_TARGET = {
  ignition:Target.IGNITION, horn:Target.HORN, headlamp:Target.HEADLAMP,
  'left-turn':Target.LEFT_TURN, left:Target.LEFT_TURN,
  'right-turn':Target.RIGHT_TURN, right:Target.RIGHT_TURN,
  starter:Target.STARTER, engine:Target.ENGINE,
  fuel:Target.FUEL_GAUGE, temp:Target.TEMP_GAUGE,
  bluetooth:Target.BLUETOOTH, conversation:Target.CONVERSATION,
  configuration:Target.CONFIG, all:Target.ALL
};

// word -> Action map for the text parser
const WORD_ACTION = {
  on:Action.ON, off:Action.OFF, enable:Action.ENABLE, disable:Action.DISABLE,
  silence:Action.SILENCE, voice:Action.VOICE, toggle:Action.TOGGLE,
  query:Action.QUERY_DATA, get:Action.GET_DATA,
  'query_on':Action.QUERY_ON, 'query_off':Action.QUERY_OFF,
  play:Action.PLAY, pause:Action.PAUSE, next:Action.NEXT, prev:Action.PREV
};

let cmdId = 1;

function nextId(){ const id = cmdId; cmdId = (cmdId >= 255) ? 1 : cmdId + 1; return id; }

function buildFrame(action, target, delay=0, interval=0, dataType=0, data=null){
  const size = data ? data.length : 0;
  const buf = new Uint8Array(8 + size);
  buf[0] = nextId();
  buf[1] = action;
  buf[2] = target;
  buf[3] = delay;
  buf[4] = interval;
  buf[5] = dataType;
  buf[6] = (size >> 8) & 0xFF;
  buf[7] = size & 0xFF;
  if(data) buf.set(data, 8);
  return buf;
}

// Convert a text command string into a binary frame matching StringCommand::parse semantics
function parseTextCommand(text){
  const words = text.trim().toLowerCase().split(/\s+/);
  let action = Action.NO_OP;
  let target = Target.UNKNOWN;
  let delay  = 0;
  let interval = 0;

  for(let i = 0; i < words.length; i++){
    const w = words[i];
    if(WORD_ACTION[w] !== undefined && action === Action.NO_OP){
      action = WORD_ACTION[w];
    } else if(WORD_TARGET[w] !== undefined && target === Target.UNKNOWN){
      target = WORD_TARGET[w];
    } else if(w === 'for' && words[i+1]){
      delay = parseInt(words[++i]) || 0;
    } else if(w === 'times' && words[i+1]){
      interval = parseInt(words[++i]) || 0;
    }
  }

  if(action === Action.NO_OP || target === Target.UNKNOWN) return null;
  return buildFrame(action, target, delay, interval);
}

//  ─── WebSocket ───────────────────────────────────────────────────────────

const App = (() => {
  let ws = null;
  let reconnectTimer = null;
  let pingStart = 0;
  let pingTimer = null;
  const deviceState = {}; // target id -> boolean

  function connect(){
    if(ws && ws.readyState < 2) return;
    ws = new WebSocket('ws://' + location.host + '/ws');
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
      banner(false);
      dot(true);
      setBadge(true);
      log('Connected to Eccles bike', 'recv');
      startPing();
      if(reconnectTimer){ clearTimeout(reconnectTimer); reconnectTimer = null; }
    };

    ws.onclose = () => {
      banner(true);
      dot(false);
      setBadge(false);
      stopPing();
      reconnectTimer = setTimeout(connect, 3000);
    };

    ws.onerror = () => ws.close();

    ws.onmessage = (ev) => {
      // result frame: [id, sizeHi, sizeLo, ...data]
      if(!(ev.data instanceof ArrayBuffer)) return;
      const d = new Uint8Array(ev.data);
      if(d.length < 3) return;
      const id   = d[0];
      const size = (d[1] << 8) | d[2];
      if(id === 0xFF){ // pong
        document.getElementById('ping-ms').textContent = Date.now() - pingStart;
        return;
      }
      // Update sensor bars if this looks like a sensor result (size 0-100)
      // We can't fully decode without knowing which query it answers,
      // so we display the value as a generic numeric result
      if(d.length === 3 && size <= 100){
        log('Result id=' + id + ' value=' + size, 'recv');
      }
    };
  }

  function send(frame){
    if(!ws || ws.readyState !== 1){
      log('Not connected', 'err');
      return false;
    }
    ws.send(frame);
    return true;
  }

  function cmd(action, target, delay=0, interval=0, data=null, dtype=0){
    return send(buildFrame(action, target, delay, interval, dtype, data));
  }

  // ─── Ping ────────────────────────────────────────────────────────────────
  function startPing(){
    stopPing();
    pingTimer = setInterval(() => {
      pingStart = Date.now();
      const f = new Uint8Array(8);
      f[0] = 0xFF; // reserved ping id
      send(f);
    }, 5000);
  }
  function stopPing(){ if(pingTimer){ clearInterval(pingTimer); pingTimer = null; } }

  // ─── UI helpers ──────────────────────────────────────────────────────────
  function banner(show){
    const el = document.getElementById('conn-banner');
    el.classList.toggle('visible', show);
  }
  function dot(alive){
    document.getElementById('power-indicator').classList.toggle('alive', alive);
  }
  function setBadge(on){
    const el = document.getElementById('status-text');
    el.textContent = on ? 'Connected' : 'Disconnected';
    el.className = 'badge ' + (on ? 'badge-on' : 'badge-off');
  }

  function log(msg, type='sent'){
    const box = document.getElementById('cmd-log');
    const time = new Date().toLocaleTimeString();
    const el = document.createElement('div');
    el.className = 'log-entry ' + type;
    el.innerHTML = '<span class="log-time">' + time + '</span><span>' + msg + '</span>';
    box.prepend(el);
    while(box.children.length > 40) box.removeChild(box.lastChild);
  }

  function setSensor(id, value, max){
    const pct = Math.min(100, Math.round((value / max) * 100));
    const bar = document.getElementById('bar-' + id);
    const val = document.getElementById('val-' + id);
    if(bar) bar.style.width = pct + '%';
    if(val) val.textContent = value + (id === 'voltage' ? ' V' : '%');
    // warn colours
    if(bar){
      bar.style.background = (pct < 20)
        ? 'linear-gradient(90deg,#ef4444,#f97316)'
        : 'linear-gradient(90deg,var(--accent2),var(--accent))';
    }
  }

  // ─── Device cards ────────────────────────────────────────────────────────
  function initDeviceCards(){
    document.querySelectorAll('.device-card').forEach(card => {
      const tgt = parseInt(card.dataset.device);
      deviceState[tgt] = false;
      card.addEventListener('click', () => toggleCard(card, tgt));
    });
  }

  function toggleCard(card, tgt){
    const on = !deviceState[tgt];
    deviceState[tgt] = on;
    const action = on ? Action.ON : Action.OFF;
    const label  = card.dataset.label || 'Device';
    if(cmd(action, tgt)){
      card.classList.toggle('active', on);
      const st = card.querySelector('.card-state');
      if(st){ st.textContent = on ? 'ON' : 'OFF'; st.className = 'card-state ' + (on ? 'state-on' : 'state-off'); }
      log((on ? 'ON ' : 'OFF ') + label, 'sent');
    }
  }

  // ─── Sensor polling ──────────────────────────────────────────────────────
  function pollAll(){
    cmd(Action.GET_DATA, Target.FUEL_GAUGE);
    cmd(Action.GET_DATA, Target.TEMP_GAUGE);
    cmd(Action.GET_DATA, Target.IGNITION_FB);
    cmd(Action.GET_DATA, Target.SHOCK_SENSOR);
    log('Sensor refresh requested', 'sent');
  }

  // ─── Bluetooth controls ──────────────────────────────────────────────────
  function btOn()      { cmd(Action.ON,          Target.BLUETOOTH); log('BT enable',   'sent'); }
  function btOff()     { cmd(Action.OFF,         Target.BLUETOOTH); log('BT disable',  'sent'); }
  function btPlay()    { cmd(Action.PLAY,         Target.BLUETOOTH); log('BT play',     'sent'); }
  function btPause()   { cmd(Action.PAUSE,        Target.BLUETOOTH); log('BT pause',    'sent'); }
  function btNext()    { cmd(Action.NEXT,         Target.BLUETOOTH); log('BT next',     'sent'); }
  function btPrev()    { cmd(Action.PREV,         Target.BLUETOOTH); log('BT prev',     'sent'); }
  function btVolUp()   { cmd(Action.VOLUME_UP,    Target.BLUETOOTH); log('BT vol+',     'sent'); }
  function btVolDown() { cmd(Action.VOLUME_DOWN,  Target.BLUETOOTH); log('BT vol-',     'sent'); }
  function btSetVolume(v){
    const data = new Uint8Array([v & 0x7F]);
    cmd(Action.SET_VOLUME, Target.BLUETOOTH, 0, 0, data, 0);
    log('BT volume ' + v, 'sent');
  }

  // ─── Conversation / AI ───────────────────────────────────────────────────
  function convAI()  { cmd(Action.START_AI,   Target.CONVERSATION); log('Conversation AI start',  'sent'); }
  function convStop(){ cmd(Action.OFF,         Target.CONVERSATION); log('Conversation stop',       'sent'); }

  function sendAI(){
    const input = document.getElementById('ai-text');
    const text  = input.value.trim();
    if(!text) return;
    const enc   = new TextEncoder();
    const data  = enc.encode(text);
    cmd(Action.PLAY, Target.CONVERSATION, 0, 0, data, 0);
    log('AI: ' + text, 'sent');
    input.value = '';
  }

  // ─── Text command terminal ────────────────────────────────────────────────
  function sendText(){
    const input = document.getElementById('cmd-input');
    const text  = input.value.trim();
    if(!text) return;

    const frame = parseTextCommand(text);
    if(!frame){
      log('Unknown command: ' + text, 'err');
      input.value = '';
      return;
    }

    if(send(frame)){
      log('> ' + text, 'sent');
    }
    input.value = '';
  }

  // ─── Keyboard shortcuts ───────────────────────────────────────────────────
  function initKeyboard(){
    document.getElementById('cmd-input').addEventListener('keydown', e => {
      if(e.key === 'Enter') sendText();
    });
    document.getElementById('ai-text').addEventListener('keydown', e => {
      if(e.key === 'Enter') sendAI();
    });
  }

  // ─── Init ─────────────────────────────────────────────────────────────────
  function init(){
    initDeviceCards();
    initKeyboard();
    connect();
  }

  document.addEventListener('DOMContentLoaded', init);

  return {
    pollAll, btOn, btOff, btPlay, btPause, btNext, btPrev,
    btVolUp, btVolDown, btSetVolume,
    convAI, convStop, sendAI, sendText
  };
})();
