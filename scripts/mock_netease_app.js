// mock_netease_app.js
// NeteaseCloudMusicApi 的最小替身，用于在没有装好真实 node 服务的机器上验证
// NeteaseMusic 插件的进程管理与 HTTP 调用链路。
//
// 用法：
//   PORT=34793 node scripts/mock_netease_app.js
//   （Windows: set PORT=34793 && node scripts\mock_netease_app.js）
//
// 注意：新版 NeteaseCloudMusicApi Enhanced 由模块文件名推导路由，会把下划线
// 换成斜杠（login_status.js -> /login/status）。插件也是按斜杠形式请求的，
// 因此这里以斜杠路由为准，同时保留旧的下划线路由方便对照。
//
// 每个请求都会打到 stdout，便于确认插件到底调了哪些接口。
'use strict';
const http = require('http');

const PORT = Number(process.env.PORT || 34793);

function json(res, obj) {
  const s = JSON.stringify(obj);
  res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' });
  res.end(s);
}

const server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://127.0.0.1');
  const p = u.pathname;
  console.log(`[mock-netease] ${req.method} ${p}${u.search ? '?' + u.search.slice(1, 200) : ''}`);
  switch (p) {
    // 新版斜杠路由
    case '/login/status':
    case '/login_status':
      // anonimousUser=true 让插件走手机号登录分支，从而覆盖 login/cellphone
      return json(res, { data: { code: 200, account: { anonimousUser: true } } });
    case '/login/cellphone':
    case '/login_cellphone':
      return json(res, { code: 200, cookie: 'MUSIC_U=mock-cookie; __csrf=mock' });
    case '/register/anonimous':
    case '/register_anonimous':
      return json(res, { code: 200, cookie: 'MUSIC_A_T=mock; __csrf=mock' });
    case '/captcha/sent':
    case '/captcha_sent':
      return json(res, { code: 200 });
    case '/style/preference':
    case '/style_preference':
      return json(res, { code: 200, data: { tagPreferenceVos: [], tags: [] } });
    case '/cloudsearch':
    case '/search':
      return json(res, {
        result: {
          songs: [
            { id: 1001, name: '测试歌曲', ar: [{ name: '测试歌手' }] },
            { id: 1002, name: '第二首歌', ar: [{ name: '另一位歌手' }] },
          ],
        },
        code: 200,
      });
    case '/song/url/v1':
    case '/song/url':
    case '/song_url':
      return json(res, { data: [{ id: 1001, url: 'http://127.0.0.1:1/mock.mp3' }], code: 200 });
    default:
      return json(res, { code: 200 });
  }
});

server.listen(PORT, () => {
  // 仅作可读性提示；插件实际是靠 HTTP 探测 /login/status 判断就绪的，
  // 并不解析这行输出。
  console.log(`Server started successfully @ http://localhost:${PORT}`);
});
