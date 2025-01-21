Date.prototype.Format = function (fmt) { //author: meizz
    var o = {
        "M+": this.getMonth() + 1, //月份
        "d+": this.getDate(), //日
        "h+": this.getHours(), //小时
        "m+": this.getMinutes(), //分
        "s+": this.getSeconds(), //秒
        "q+": Math.floor((this.getMonth() + 3) / 3), //季度
        "S": this.getMilliseconds() //毫秒
    };
    if (/(y+)/.test(fmt)) fmt = fmt.replace(RegExp.$1, (this.getFullYear() + "").substr(4 - RegExp.$1.length));
    for (var k in o)
    if (new RegExp("(" + k + ")").test(fmt)) fmt = fmt.replace(RegExp.$1, (RegExp.$1.length == 1) ? (o[k]) : (("00" + o[k]).substr(("" + o[k]).length)));
    return fmt;
}

function getQueryString(name){
    var reg = new RegExp("(^|&)"+ name +"=([^&]*)(&|$)");
    var r = window.location.search.substr(1).match(reg);
    if (r!=null) return r[2]; return '';
}

$('#btSend').click(()=>{
    let content = $('#content').val()
    console.log(content)
    let html = ''
    html += 
`<div class="media">
    <div class="media-left media-top">
        <img src="src/human.png" class="media-object" style="width:90px"/>
    </div>
    <div class="media-body">
        <h5>${content}</h5>
    </div>
</div>
`;
    $('#contentList').append(html)
    socket.send(`{"op": "conversation", "question": "${content}"}`);
});

var socket = null;

window.onload = function() {
    let autoHeight = 0;
    //函数：获取尺寸    //获取浏览器窗口高度
    if (window.innerHeight)
      autoHeight = window.innerHeight;
    else if ((document.body) && (document.body.clientHeight))
      autoHeight = document.body.clientHeight;    //通过深入Document内部对body进行检测，获取浏览器窗口高度
    if (document.documentElement && document.documentElement.clientHeight)
      autoHeight = document.documentElement.clientHeight;    //DIV高度为浏览器窗口的高度
    autoHeight = autoHeight - ($('#content').height() + 80);
    if(document.body.offsetHeight < autoHeight){
      $('#contentList').css({"height":autoHeight,"overflow-y":"scroll"})
    }
    initWebSocket();
}

function onSocketMessage(msgEvent) {
    let msg = msgEvent.data;
    $('#content').val('')
    let html = ''
    html += 
`<div class="media">
    <div class="media-body" style="text-align: right;">
        <h5>${msg}</h5>
    </div>
    <div class="media-right">
        <img src="src/robot.png" class="media-object" style="width:90px"/>
    </div>
</div>
`
    $('#contentList').append(html)
}

function onSocketError() {
    socket = null;
    console.log("error");
}

function onSocketClose() {
    socket = null;
    console.log("close");
}

function onSocketOpen() {
    // 定期发送ping消息
    setInterval(() => {
        if (socket.readyState === WebSocket.OPEN) {
            const pingMessage = JSON.stringify({ type: 'ping', timestamp: Date.now() });
            socket.send(pingMessage);
        }
    }, 30000); // 每 30 秒发送一次
}

function initWebSocket(){
    if(socket == null){
        console.log("building websocket");
        const host = window.location.host;
        const [domain, port] = host.includes(":") ? host.split(":") : [host, "default"];
        let portInt = parseInt(port);
        portInt = portInt + 1
        var ws_addr = "wss://" + host;
        console.log(ws_addr);
        socket = new WebSocket(ws_addr);
        socket.onopen = onSocketOpen;
        socket.onmessage = onSocketMessage;
        socket.onerror = onSocketError;
        socket.onclose = onSocketClose;
    }
}