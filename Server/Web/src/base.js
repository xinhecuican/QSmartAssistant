/**
 * QSmartAssistant 网页对话客户端。
 *
 * 与服务端的约定见 Server/server.h：
 *   发送 {"op":"conversation","question":"..."}
 *   收到 {"op":"accepted","id":n}                       受理确认
 *   收到 {"op":"response","id":n,"text":"...","source":""} 一条回复（source 空 = 主程序）
 *   收到 {"op":"ask","id":n,"text":"...","source":""}      反问：需要再给一个输入
 *   发送 {"op":"answer","id":n,"text":"..."}               回答上面的反问
 *   ping/pong 用于保活
 *
 * 回复由插件异步产生，因此一律以服务端推送为准，客户端不等待请求返回值。
 *
 * ask 是「一次提问中途反过来问用户」用的（例如网易云要短信验证码）。服务端
 * 同时会把问题念出来，也就是说**用嘴回答也行**；哪种方式先到就听哪种，所以
 * 输入框不再是必答项 —— 这里只负责把它显眼地摆出来、并把用户敲的下一条消息
 * 当成对它的回答发回去。
 */
(function () {
  'use strict';

  var MAX_MESSAGES = 200;      // DOM 中最多保留的消息条数
  var PING_INTERVAL = 25000;   // 保活间隔
  var REPLY_TIMEOUT = 30000;   // 超过该时间没有任何回复则提示

  var els = {
    chat: document.getElementById('chat'),
    welcome: document.getElementById('welcome'),
    input: document.getElementById('input'),
    sendBtn: document.getElementById('sendBtn'),
    clearBtn: document.getElementById('clearBtn'),
    status: document.getElementById('status'),
    statusText: document.getElementById('statusText'),
  };

  var socket = null;
  var pingTimer = null;
  var reconnectTimer = null;
  var reconnectDelay = 1000;
  var pendingByConv = new Map(); // 会话 id -> {node, timer} 等待中的气泡
  // 正在等用户作答的反问，按到达顺序排队：{id, node}。
  // 一次只可能有一条真正待答（服务端是同步等的），用队列只是为了兜住乱序。
  var awaitingAsks = [];
  var defaultPlaceholder = els.input ? els.input.placeholder : '';

  /* ---------------- 工具 ---------------- */

  function now() {
    var d = new Date();
    return ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2);
  }

  function scrollToBottom() {
    els.chat.scrollTop = els.chat.scrollHeight;
  }

  function isNearBottom() {
    var el = els.chat;
    return el.scrollHeight - el.scrollTop - el.clientHeight < 120;
  }

  function hideWelcome() {
    if (els.welcome && els.welcome.parentNode) {
      els.welcome.parentNode.removeChild(els.welcome);
      els.welcome = null;
    }
  }

  function trimMessages() {
    var nodes = els.chat.querySelectorAll('.msg');
    for (var i = 0; i < nodes.length - MAX_MESSAGES; i++) {
      nodes[i].parentNode.removeChild(nodes[i]);
    }
  }

  function setStatus(text, kind) {
    els.statusText.textContent = text;
    els.status.className = 'status' + (kind ? ' ' + kind : '');
  }

  /**
   * 组装一条消息。
   * @param {'user'|'bot'} role
   * @param {string} text
   * @param {string} [source] 产生回复的插件名，空表示主程序
   */
  function buildMessage(role, text, source) {
    var row = document.createElement('div');
    row.className = 'msg ' + role;

    var avatar = document.createElement('img');
    avatar.className = 'avatar';
    avatar.alt = '';
    avatar.src = role === 'user' ? 'src/human.png' : 'src/robot.png';
    row.appendChild(avatar);

    var bubble = document.createElement('div');
    bubble.className = 'bubble';

    var meta = document.createElement('div');
    meta.className = 'meta';

    if (role === 'bot') {
      var badge = document.createElement('span');
      var isPlugin = !!source;
      badge.className = 'badge ' + (isPlugin ? 'plugin' : 'core');
      badge.textContent = isPlugin ? source : '主程序';
      badge.title = isPlugin ? '由插件「' + source + '」处理' : '由主程序生成';
      meta.appendChild(badge);
    }

    var time = document.createElement('span');
    time.className = 'time';
    time.textContent = now();
    meta.appendChild(time);
    bubble.appendChild(meta);

    var body = document.createElement('div');
    body.className = 'text';
    body.textContent = text;
    bubble.appendChild(body);

    row.appendChild(bubble);
    return row;
  }

  function appendMessage(role, text, source) {
    hideWelcome();
    var node = buildMessage(role, text, source);
    els.chat.appendChild(node);
    trimMessages();
    scrollToBottom();
    return node;
  }

  function appendHint(text) {
    hideWelcome();
    var node = document.createElement('div');
    node.className = 'hint';
    node.textContent = text;
    els.chat.appendChild(node);
    scrollToBottom();
    return node;
  }

  function addPending(convId) {
    hideWelcome();
    var row = document.createElement('div');
    row.className = 'msg bot typing';

    var avatar = document.createElement('img');
    avatar.className = 'avatar';
    avatar.alt = '';
    avatar.src = 'src/robot.png';
    row.appendChild(avatar);

    var bubble = document.createElement('div');
    bubble.className = 'bubble';
    var dots = document.createElement('span');
    dots.className = 'dots';
    for (var i = 0; i < 3; i++) {
      dots.appendChild(document.createElement('i'));
    }
    bubble.appendChild(dots);
    row.appendChild(bubble);

    els.chat.appendChild(row);
    scrollToBottom();

    var timer = setTimeout(function () {
      var pending = pendingByConv.get(convId);
      if (pending && pending.node === row) {
        pendingByConv.delete(convId);
        row.remove();
        appendHint('（没有收到回复，换个说法再试试）');
      }
    }, REPLY_TIMEOUT);

    pendingByConv.set(convId, { node: row, timer: timer });
  }

  function settlePending(convId) {
    var pending = pendingByConv.get(convId);
    if (!pending) {
      return false;
    }
    clearTimeout(pending.timer);
    pendingByConv.delete(convId);
    pending.node.remove();
    return true;
  }

  /* ---------------- 反问（ask / answer） ---------------- */

  function markAsk(ask, answered) {
    ask.node.classList.remove('ask');
    ask.node.classList.add(answered ? 'ask-done' : 'ask-expired');
    refreshAskHint();
  }

  function takeAsk(convId) {
    for (var i = 0; i < awaitingAsks.length; i++) {
      if (awaitingAsks[i].id === convId) {
        return awaitingAsks.splice(i, 1)[0];
      }
    }
    return null;
  }

  function finishAsk(convId, answered) {
    var ask = takeAsk(convId);
    if (ask) {
      markAsk(ask, answered);
    }
  }

  function refreshAskHint() {
    if (!els.input) {
      return;
    }
    els.input.placeholder = awaitingAsks.length
      ? '回答上面的问题…'
      : defaultPlaceholder;
  }

  function beginAsk(convId, text, source) {
    // 反问本身就是这一轮的「回复」，别再挂着「正在输入…」的气泡
    settlePending(convId);
    var node = appendMessage('bot', text, source);
    node.classList.add('ask');
    var tip = document.createElement('div');
    tip.className = 'ask-tip';
    // 服务端同时会把问题念出来，所以两种回答方式都行，这里要说清楚
    tip.textContent = '↑ 请回答这个问题：直接在这里输入，或直接用语音说';
    var bubble = node.querySelector('.bubble');
    if (bubble) {
      bubble.appendChild(tip);
    }
    awaitingAsks.push({ id: convId, node: node });
    refreshAskHint();
  }

  /* ---------------- 发送 ---------------- */

  function send() {
    var text = els.input.value.trim();
    if (!text) {
      return;
    }
    var canSend = socket && socket.readyState === WebSocket.OPEN;
    if (!canSend) {
      appendHint('（连接已断开，正在重连，请稍后再试）');
      ensureConnected();
      return;
    }

    els.input.value = '';
    autoGrow();
    appendMessage('user', text);

    // 有未作答的反问时，这一条就是对它的回答 —— 必须带上那个会话 id，
    // 否则服务端只会当成一次全新的提问（反问那边会一直等到超时）。
    var ask = awaitingAsks.length ? awaitingAsks[0] : null;
    if (ask) {
      takeAsk(ask.id);
      markAsk(ask, true);
      socket.send(JSON.stringify({ op: 'answer', id: ask.id, text: text }));
      return;
    }
    socket.send(JSON.stringify({ op: 'conversation', question: text }));
  }

  function autoGrow() {
    els.input.style.height = 'auto';
    els.input.style.height = Math.min(els.input.scrollHeight, window.innerHeight * 0.4) + 'px';
  }

  /* ---------------- WebSocket ---------------- */

  function wsUrl() {
    var scheme = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    // 与页面同源同端口：服务端把 HTTP 与 WebSocket 绑在同一个 TCP 端口上，
    // 因此不能像以前那样改写端口，也不能无条件用 wss。
    return scheme + window.location.host;
  }

  function onMessage(event) {
    var data;
    try {
      data = JSON.parse(event.data);
    } catch (e) {
      appendMessage('bot', String(event.data), '');
      return;
    }

    switch (data.op) {
      case 'pong':
        break;
      case 'accepted':
        if (typeof data.id === 'number') {
          addPending(data.id);
        }
        break;
      case 'ask':
        if (typeof data.id === 'number') {
          beginAsk(data.id, data.text || '', data.source || '');
        }
        break;
      case 'response':
        if (typeof data.id === 'number') {
          settlePending(data.id);
          // 这一轮结束了：还在等作答的反问说明是被语音答掉的（用户在网页上
          // 没有动作），把它收掉，免得输入框一直提示「回答上面的问题」。
          finishAsk(data.id, true);
        }
        appendMessage('bot', data.text || '', data.source || '');
        break;
      default:
        break;
    }
  }

  function ensureConnected() {
    if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
      return;
    }
    setStatus('连接中…', '');
    try {
      socket = new WebSocket(wsUrl());
    } catch (e) {
      scheduleReconnect();
      return;
    }

    socket.onopen = function () {
      reconnectDelay = 1000;
      setStatus('已连接', 'ok');
      if (pingTimer) {
        clearInterval(pingTimer);
      }
      pingTimer = setInterval(function () {
        if (socket && socket.readyState === WebSocket.OPEN) {
          socket.send(JSON.stringify({ op: 'ping', timestamp: Date.now() }));
        }
      }, PING_INTERVAL);
    };

    socket.onmessage = onMessage;

    socket.onerror = function () {
      setStatus('连接异常', 'err');
    };

    socket.onclose = function () {
      socket = null;
      if (pingTimer) {
        clearInterval(pingTimer);
        pingTimer = null;
      }
      // 断线后服务端那边这些会话就作废了，再让用户去回答也没有意义
      while (awaitingAsks.length) {
        markAsk(awaitingAsks.shift(), false);
      }
      setStatus('已断开，重连中…', 'err');
      scheduleReconnect();
    };
  }

  function scheduleReconnect() {
    if (reconnectTimer) {
      return;
    }
    reconnectTimer = setTimeout(function () {
      reconnectTimer = null;
      ensureConnected();
    }, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 1.8, 15000);
  }

  /* ---------------- 事件绑定 ---------------- */

  els.sendBtn.addEventListener('click', send);

  els.input.addEventListener('input', autoGrow);

  els.input.addEventListener('keydown', function (e) {
    // 中文输入法组合中的回车不能当发送
    if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
      e.preventDefault();
      send();
    }
  });

  els.clearBtn.addEventListener('click', function () {
    pendingByConv.forEach(function (pending) {
      clearTimeout(pending.timer);
    });
    pendingByConv.clear();
    awaitingAsks = [];
    refreshAskHint();
    els.chat.innerHTML = '';
    appendHint('对话已清空');
  });

  document.addEventListener('visibilitychange', function () {
    if (!document.hidden) {
      ensureConnected();
    }
  });

  window.addEventListener('online', ensureConnected);

  // 移动端软键盘弹出会改变视口高度：跟随输入框高度，并按需保持滚动到底部
  window.addEventListener('resize', function () {
    var stick = isNearBottom();
    autoGrow();
    if (stick) {
      scrollToBottom();
    }
  });

  autoGrow();
  ensureConnected();
})();
