#ifndef TST_SHERPA_H
#define TST_SHERPA_H
#if defined(ASR_SHERPA)
#include "../Conversation/ASR/sherpaasr.h"
#endif
#if defined(TTS_SHERPA)
#include "../Conversation/TTS/sherpatts.h"
#endif
#if defined(NLU_RASA)
#include "../Conversation/NLU/rasanlu.h"
#endif
#if defined(NLU_LLM)
#include "../Conversation/NLU/llmnlu.h"
#endif
#include "../Utils/LLM/LLMManager.h"
#include "../Utils/LLM/MCPClient.h"
#if defined(QSA_HAVE_LLAMA)
#include "../Utils/LLM/LlamaApi.h"
#include "../Utils/LLM/LlamaCppBackend.h"
#endif
#if defined(ASR_FUN)
#include "../Conversation/ASR/funasr.h"
#endif
#include "../Plugins/systeminfo/systeminfo.h"
#include "../Recorder/player.h"
#include "../Recorder/recorder.h"
#include "../Utils/config.h"
#include "../Utils/wavfilereader.h"
#if defined(VAD_SILERO)
#include "../Wakeup/Vad/silerovad.h"
#endif
#if defined(VAD_SHERPA)
#include "../Wakeup/Vad/sherpavad.h"
#endif
#include "../Plugins/neteasemusic/neteaseofficialclient.h"
#include "../Plugins/neteasemusic/qrcode.h"
#include "TestPluginHelper.h"
#include <QLibrary>
#include <QtTest/QtTest>
#include <QRegularExpression>
using namespace AC;

class tst_sherpa : public QObject {
    Q_OBJECT
private:
    Player *mplayer;
private slots:
    void initTestCase() {
        Config::instance()->loadConfig();
        mplayer = new Player(this);
    }

#if defined(VAD_SILERO) || defined(VAD_SHERPA)
    void vad() {
#if defined(VAD_SILERO)
        SileroVad *vad = new SileroVad(this);
#endif
#if defined(VAD_SHERPA)
        SherpaVad *vad = new SherpaVad(this);
#endif

        QFile file(Config::getDataPath("short_test.wav"));
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();
        file.close();
        bool detected = false;
        for (int i = 0; i < data.size(); i += vad->getChunkSize()) {
            if (data.size() - i < vad->getChunkSize())
                break;
            QByteArray testData = data.mid(i, vad->getChunkSize());
            bool detect = vad->detectVoice(testData);
            if (detect) {
                detected = true;
                printf("-");
            } else {
                printf("_");
            }
        }
        printf("\n");
        QCOMPARE(detected, true);
    }
#endif
#if defined(ASR_FUN)
    void asrFun() {
        WavFileReader reader;
        reader.OpenWavFile(Config::getDataPath("test2.wav").toStdString());
        Funasr *funasr = new Funasr(this);
        QByteArray *cache = new QByteArray;
        int size = 0;
        do {
            char buf[1024];
            size = reader.ReadData(buf, 640);
            if (size > 0) {
                cache->append(buf, size);
            }
        } while (size);
        connect(funasr, &Funasr::recognized, this, [=](QString result, int id) {
            qDebug() << result;
            QCOMPARE(result, "深入的分析这一次全球金融动荡背后的根源");
        });
        funasr->detect(*cache);
        QTest::qWait(4000);
    }
#endif
#if defined(ASR_SHERPA)
    void asrSherpa() {
        WavFileReader reader;
        reader.OpenWavFile(Config::getDataPath("test2.wav").toStdString());
        SherpaASR *sherpa = new SherpaASR(this);
        QByteArray *cache = new QByteArray;
        int size = 0;
        do {
            char buf[1024];
            size = reader.ReadData(buf, 640);
            if (size > 0) {
                cache->append(buf, size);
            }
        } while (size);
        connect(sherpa, &SherpaASR::recognized, this, [=](QString result, int id) {
            qDebug() << result;
            QCOMPARE(result, "深入的分析这一次全球金融动荡背后的根源");
        });
        sherpa->detect(*cache);
        QTest::qWait(4000);
    }
#endif
    void testPlayer() {
        QFile file(Config::getDataPath("short_test.wav"));
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();
        file.close();
        mplayer->playRaw(data, 8000);
        QBuffer buffer(&data);
        Player *player = new Player;
        QFileInfo fileInfo(Config::getDataPath("short_test.wav"));
        mplayer->playSoundEffect(Config::getDataPath("start.wav"), false);
        QTest::qWait(4000);
    }
#if defined(TTS_SHERPA)
    void ttsSherpa() {
        SherpaTTS *tts = new SherpaTTS();
        connect(tts, &SherpaTTS::dataArrive, this,
                [=](QByteArray data, int sampleRate, const QString &type) {
                    mplayer->playRaw(data, sampleRate, AudioPlaylist::NOTIFY);
                });
        tts->detect("这是一个短的测试", "", 0);
        tts->detect("本系列主要目标初步完成一款智能音箱的基础功能，包括语音唤醒"
                    "、语音识别(语音转文字)"
                    "、处理用户请求（比如查天气等，主要通过rasa自己定义意图实现"
                    "）、语音合成(文字转语音)功能。"
                    "语音识别、语音合成采用离线方式实现。"
                    "语音识别使用sherpa-onnx，可以实现离线中英文语音识别。",
                    "", 0);
        QTest::qWait(20000);
    }
#endif
#if defined(NLU_RASA)
    // 需要本地已有 rasa 服务（真机: rasa run --enable-api；离线验证可用等价的
    // mock 服务 python scripts/mock_rasa.py，见 doc/windows.md 第七节）。
    // 校验 /status 与 /model/parse 的调用链路，以及 entity2Slot 的槽位映射。
    void rasaNlu() {
        RasaNLU *nlu = new RasaNLU(this);
        QTest::qWait(1500); // 等构造中的 /status 请求返回
        ParsedIntent parsed = nlu->parseIntent("把音量调大一点");
        QVERIFY2(parsed.hasIntent("CHANGE_VOL"), "rasa did not return CHANGE_VOL");
        bool ok = false;
        IntentSlot slot = parsed.intents["CHANGE_VOL"].getSlot("user_d", ok);
        QVERIFY2(ok, "slot user_d missing");
        QCOMPARE(slot.value, QString("--HIGHER--"));
    }
#endif
    // void duilite_gram(){
    //     QLibrary lib("libduilite.so");
    //     QJsonObject duiliteConfig = Config::instance()->getConfig("duilite");
    //     QString loginPath =
    //     Config::getDataPath(duiliteConfig.value("login").toString()); QFile
    //     file(loginPath); if(!file.open(QIODevice::ReadOnly)){
    //         qCritical()<< "duilite login open error";
    //         return;
    //     }
    //     QByteArray loginData = file.readAll();
    //     file.close();
    //     int ret =
    //     ((int(*)(char*))lib.resolve("duilite_library_load"))(loginData.data());
    //     if(ret){
    //         qCritical() << "duilite library open error";
    //         return;
    //     }
    //     QString res =
    //     Config::getDataPath(duiliteConfig.value("asrRes").toString());
    //     QString gram =
    //     Config::getDataPath(duiliteConfig.value("gram").toString()); QString
    //     cfg =   "{\"resBinPath\": \"" + res + "\"}"; std::string resS =
    //     cfg.toStdString(); char* resData = (char*)resS.c_str(); struct
    //     duilite_gram* grammer;
    //     // ((struct duilite_gram*(*)(char*))lib.resolve("duilite_gram_new"))
    //     grammer = duilite_gram_new(resData);
    //     QString cfg2 = "{\"outputPath\": \"gram.gram\", \"ebnfFile\": \"" +
    //     gram + "\"}"; std::string cfg2S = cfg2.toStdString();
    //     ((int(*)(struct duilite_gram*,
    //     char*))lib.resolve("duilite_gram_start"))(grammer,
    //     (char*)cfg2S.c_str());
    // }
    void pluginSystemInfo() {
        QString pluginName = "SystemInfo";
        // 插件名随平台不同：Linux 为 libXxx.so，Windows 为 Xxx.dll。
        // 注意必须有路径分隔符，否则会拼成 .../libSystemInfo.so。
#if defined(Q_OS_WIN)
        const QString pluginSuffix = ".dll";
#else
        const QString pluginSuffix = ".so";
#endif
        QPluginLoader loader(QDir::homePath() +
                             "/.config/QSmartAssistant/plugins/lib/" +
                             pluginName + pluginSuffix);
        QObject *plugin = loader.instance();
        if (plugin) {
            auto info = qobject_cast<Plugin *>(plugin);
            if (info) {
                connect(plugin, SIGNAL(sendMessage(PluginMessage)), this,
                        SLOT(handleMessage(PluginMessage)));
                info->setPluginHelper(new TestPluginHelper);
                qInfo() << "load plugin" << pluginName << "success";
                ParsedIntent parsedIntent;
                Intent intent;
                intent.name = "SYS_INFO";
                parsedIntent.append(intent);
                bool immersive = false;
                info->handle("系统信息", parsedIntent, 0, immersive);
            } else {
                qInfo() << "load Plugin" << pluginName << "cast fail";
            }
        } else {
            qInfo() << "load plugin" << pluginName << "fail";
        }
    }
    void volume() {
        WavFileReader reader;
        reader.OpenWavFile(Config::getDataPath("short_test.wav").toStdString());
        QByteArray cache;
        int size = 0;
        do {
            char buf[1024];
            size = reader.ReadData(buf, 640);
            if (size > 0) {
                cache.append(buf, size);
            }
        } while (size);
        AudioWriter::changeVol(cache, 16);
        QAudioFormat format;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        format.setSampleFormat(QAudioFormat::Int16);
        format.setChannelConfig(QAudioFormat::ChannelConfigMono);
#else
        format.setByteOrder(QAudioFormat::LittleEndian);
        format.setCodec("audio/pcm");
        format.setSampleSize(16);
        format.setSampleType(QAudioFormat::SignedInt);
#endif
        format.setChannelCount(1);
        format.setSampleRate(8000);
        AudioWriter::writeWav("short_test16.wav", cache, format);
    }

    void hassRegex() {
        Intent intent;
        intent.appendSlot(IntentSlot("trend", "增大", 0));
        intent.appendSlot(IntentSlot("number", "30", 0));
        QString value = "test{trend|--}有多少{number|--}";
        QRegularExpression keyFinder(
            "\\{(.*)\\|(.*)\\}", QRegularExpression::InvertedGreedinessOption);
        QString result = "";
        int pos = 0;
        int lastPos = 0;

        while (pos < value.size()) {
            QRegularExpressionMatch match = keyFinder.match(value, pos);
            if (match.hasMatch()) {
                QString key = match.captured(1);
                bool success = false;
                IntentSlot slot = intent.getSlot(key, success);
                if (success) {
                    result += value.mid(lastPos,
                                        match.capturedStart(1) - lastPos - 1);
                    result += slot.value;
                } else {
                    QString defaultValue = match.captured(2);
                    if (defaultValue != "--") {
                        result += defaultValue;
                    }
                }
                pos = match.capturedEnd();
                lastPos = pos;
            } else
                break;
        }
        if (lastPos < value.size()) {
            result += value.mid(lastPos);
        }
        qDebug() << result;
        QCOMPARE(result, "test增大有多少30");
    }

    void hassParam() {
        QString pluginName = "Hass";
        // 插件名随平台不同：Linux 为 libXxx.so，Windows 为 Xxx.dll。
        // 注意必须有路径分隔符，否则会拼成 .../libSystemInfo.so。
#if defined(Q_OS_WIN)
        const QString pluginSuffix = ".dll";
#else
        const QString pluginSuffix = ".so";
#endif
        QPluginLoader loader(QDir::homePath() +
                             "/.config/QSmartAssistant/plugins/lib/" +
                             pluginName + pluginSuffix);
        QObject *plugin = loader.instance();
        if (plugin) {
            auto info = qobject_cast<Plugin *>(plugin);
            if (info) {
                connect(plugin, SIGNAL(sendMessage(PluginMessage)), this,
                        SLOT(handleMessage(PluginMessage)));
                info->setPluginHelper(new TestPluginHelper);
                qInfo() << "load plugin" << pluginName << "success";
                ParsedIntent parsedIntent;
                Intent intent;
                intent.name = "OPEN_FURNITURE";
                intent.appendSlot(IntentSlot("furniture", "热水器", 0));
                intent.appendSlot(IntentSlot("number", "20", 0));
                parsedIntent.append(intent);
                bool immersive = false;
                info->handle("热水器开20分钟", parsedIntent, 0, immersive);
            } else {
                qInfo() << "load Plugin" << pluginName << "cast fail";
            }
        } else {
            qInfo() << "load plugin" << pluginName << "fail";
        }
        QTest::qWait(3000);
    }

    // -----------------------------------------------------------------
    // LLM NLU 的输出解析
    //
    // 这是整个 LLM NLU 链路里**唯一有确定性**的环节：模型输出是短 DSL，
    // 解析规则完全由我们自己定义。微调后的模型准确率好不好先不管，
    // 解析器必须先是对的 —— 解析错了会把正确的分类结果变成插件拿不到的
    // 脏数据（槽位名对不上、意图漏发），而且这种 bug 在现场极难排查。
    // 所以这里逐条覆盖容错分支。
    // -----------------------------------------------------------------
#if defined(NLU_LLM)
    void llmNluParseOutput() {
        LLMNLU nlu;
        // 注入最小 schema：只保留解析逻辑需要的字段，不依赖运行期文件
        QJsonObject intents;
        intents.insert("0", QJsonObject{{"name", "FALLBACK"}});
        intents.insert("2", QJsonObject{{"name", "CHANGE_TO_NEXT"}});
        intents.insert("3",
                       QJsonObject{{"name", "CHANGE_VOL"},
                                   {"entities", QJsonArray{"n", "t"}}});
        intents.insert("10", QJsonObject{{"name", "MUSICINFO"},
                                        {"entities", QJsonArray{"s", "g"}}});
        intents.insert("11", QJsonObject{{"name", "OPEN_FURNITURE"},
                                         {"entities", QJsonArray{"f", "l"}}});
        QJsonObject entities;
        entities.insert("n", "number");
        entities.insert("t", "trend");
        entities.insert("s", "song");
        entities.insert("g", "human");
        entities.insert("f", "furniture");
        entities.insert("l", "location");
        entities.insert("zz", "unknown_entity");

        QJsonObject root;
        root.insert("intents", intents);
        root.insert("entities", entities);
        QVERIFY(nlu.loadSchemaFromJson(root));

        // 1) 纯意图，无槽位
        ParsedIntent p = nlu.parseOutput("2", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));
        QCOMPARE(p.intents["CHANGE_TO_NEXT"].intentSlots.size(), 0);

        // 2) 单槽位。MUSICINFO 的 song 要映射成插件读的用户槽位
        p = nlu.parseOutput("10|s=孤勇者", "播放孤勇者");
        QVERIFY(p.hasIntent("MUSICINFO"));
        QCOMPARE(p.getSlotValue("MUSICINFO", "user_music_name"),
                 QStringList{"孤勇者"});

        // 3) 多槽位 + 房间。location 要保留原名（hass/weather 直接读它）
        p = nlu.parseOutput("11|f=灯,l=客厅", "打开客厅的灯");
        QVERIFY(p.hasIntent("OPEN_FURNITURE"));
        QCOMPARE(p.getSlotValue("OPEN_FURNITURE", "furniture"),
                 QStringList{"灯"});
        QCOMPARE(p.getSlotValue("OPEN_FURNITURE", "location"),
                 QStringList{"客厅"});

        // 4) singer 要映射成 user_singer_name
        p = nlu.parseOutput("10|s=袖手旁观,g=林志炫", "播放林志炫的袖手旁观");
        QCOMPARE(p.getSlotValue("MUSICINFO", "user_singer_name"),
                 QStringList{"林志炫"});

        // 5) 趋势映射成语音控制插件认的 --HIGHER--/--SMALLER--
        p = nlu.parseOutput("3|t=大", "音量大一点");
        QCOMPARE(p.getSlotValue("CHANGE_VOL", "user_d"),
                 QStringList{"--HIGHER--"});
        p = nlu.parseOutput("3|t=减小", "音量减小");
        QCOMPARE(p.getSlotValue("CHANGE_VOL", "user_d"),
                 QStringList{"--SMALLER--"});
        // 绝对音量保留数值
        p = nlu.parseOutput("3|n=50", "音量调到50");
        QCOMPARE(p.getSlotValue("CHANGE_VOL", "user_v"), QStringList{"50"});

        // 6) FALLBACK(0) 不应产生任何意图 —— 让它落到 Fallback 插件
        p = nlu.parseOutput("0", "嗯嗯啊啊");
        QVERIFY(!p.hasIntent("FALLBACK"));
        QCOMPARE(p.intents.size(), 0);

        // 7) 容错：模型带了解释文字，只取第一行
        p = nlu.parseOutput("2\n这句话表示用户想切歌", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));

        // 8) 容错：markdown 反引号包裹
        p = nlu.parseOutput("`10|s=晴天`", "播放晴天");
        QVERIFY(p.hasIntent("MUSICINFO"));

        // 9) 容错：直接回意图名而非编号（小模型常见）
        p = nlu.parseOutput("CHANGE_TO_NEXT", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));

        // 10) 容错：未知实体码必须丢弃，不能让脏 slot 进插件
        p = nlu.parseOutput("11|f=灯,zz=乱七八糟", "打开灯");
        QVERIFY(p.hasIntent("OPEN_FURNITURE"));
        QCOMPARE(p.intents["OPEN_FURNITURE"].intentSlots.size(), 1);

        // 11) 容错：实体码不属于该意图时过滤（CHANGE_TO_NEXT 不该有槽位）
        p = nlu.parseOutput("2|f=灯", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));
        QCOMPARE(p.intents["CHANGE_TO_NEXT"].intentSlots.size(), 0);

        // 12) 完全无法识别的编号：不应崩，也不应产生意图
        p = nlu.parseOutput("999", "???");
        QCOMPARE(p.intents.size(), 0);

        // 13) 空输出
        p = nlu.parseOutput("", "");
        QCOMPARE(p.intents.size(), 0);
    }
#endif

    // -----------------------------------------------------------------
    // MCP client：对接 scripts/mock_mcp_server.py
    //
    // MCP 是「框架与外部进程之间」的边界，也是最容易出静默错误的地方
    // （分帧方式、isError vs JSON-RPC error、超时…）。这里用真实的
    // 子进程 + 真实管道验证，而不是 mock 掉传输层。
    // mock server 自身的协议正确性由 scripts/test_mcp_server.py 保证。
    // -----------------------------------------------------------------
    void mcpClient() {
        // 运行期 cwd 是构建输出目录，拿不到源码树。优先读环境变量
        // QSA_SOURCE_DIR，否则回退到相对路径探测。
        QString scriptPath;
        const QString sourceDir = qEnvironmentVariable("QSA_SOURCE_DIR");
        if (!sourceDir.isEmpty()) {
            scriptPath =
                QDir(sourceDir).absoluteFilePath("scripts/mock_mcp_server.py");
        }
        if (scriptPath.isEmpty() || !QFileInfo::exists(scriptPath)) {
            const QStringList candidates = {
                QDir::current().absoluteFilePath("scripts/mock_mcp_server.py"),
                QDir::current().absoluteFilePath("../scripts/mock_mcp_server.py"),
                QDir::current().absoluteFilePath("../../scripts/mock_mcp_server.py"),
            };
            for (const QString &c : candidates) {
                if (QFileInfo::exists(c)) {
                    scriptPath = c;
                    break;
                }
            }
        }
        if (scriptPath.isEmpty()) {
            qWarning() << "mock_mcp_server.py not found. Set QSA_SOURCE_DIR to "
                          "the repo root to run this test.";
            QSKIP("mock mcp server script not found");
        }

        // Windows 上 "python" 是 MicrosoftApps 的 App Execution Alias 存根，
        // 部分环境无法直接启动。测试用绝对路径更接近真实部署
        // （配置文件里本来就该写绝对路径）。
        QString command = qEnvironmentVariable("QSA_PYTHON");
        if (command.isEmpty()) {
            command = QStringLiteral("python");
        }

        // 先探测本环境能否拉起子进程：某些沙箱/受限环境禁止任何
        // CreateProcess，此时应该 skip 而不是 fail —— 那不是代码缺陷。
        {
            QProcess probe;
            probe.setProgram(command);
            probe.setArguments({"--version"});
            probe.start();
            const bool ok = probe.waitForStarted(8000);
            probe.kill();
            probe.waitForFinished(1000);
            if (!ok) {
                qWarning() << "QProcess cannot spawn child processes in this"
                              " environment; skipping mcpClient test.";
                QSKIP("child process creation not permitted here");
            }
        }

        MCPClient client(QStringLiteral("mock"));
        MCPClient::ServerConfig config;
        config.command = command;
        config.args = QStringList{scriptPath};
        client.setConfig(config);

        QVERIFY2(client.start(), "mcp client failed to start");
        QVERIFY(client.isRunning());
        QCOMPARE(client.toolCount(), 5);
        QCOMPARE(client.protocolVersion(), QStringLiteral("2024-11-05"));

        // 1) 正常调用：content 数组应被压成纯文本
        QJsonObject args;
        args.insert("text", "你好");
        LLMToolResult r = client.callTool("echo", args);
        QVERIFY(!r.isError);
        QCOMPARE(r.content, QStringLiteral("你好"));

        // 2) 业务错误：MCP 用 result.isError 表达，不是 JSON-RPC error
        QJsonObject div;
        div.insert("a", 1);
        div.insert("b", 0);
        r = client.callTool("divide", div);
        QVERIFY(r.isError);
        QVERIFY(r.content.contains("division by zero"));

        // 3) 未知工具：server 返回 isError
        r = client.callTool("does_not_exist", QJsonObject());
        QVERIFY(r.isError);

        // 4) server 抛异常：JSON-RPC error 路径
        r = client.callTool("explode", QJsonObject());
        QVERIFY(r.isError);
        QVERIFY(r.content.contains("explode"));

        // 5) 工具注册：名字带 mcp_mock_ 前缀；本地名与远端名不同，
        //    handler 必须把本地名映射回远端名（这里 echo 能通说明映射正确）
        LLMToolRegistry registry;
        client.registerTools(&registry);
        QVERIFY(registry.contains("mcp_mock_echo"));
        QVERIFY(registry.contains("mcp_mock_add"));
        QCOMPARE(registry.count(), 5);

        LLMToolCall call;
        call.id = "call_1";
        call.name = "mcp_mock_echo";
        call.arguments = args;
        LLMToolResult viaRegistry = registry.call(call);
        QVERIFY(!viaRegistry.isError);
        QCOMPARE(viaRegistry.content, QStringLiteral("你好"));

        // 6) 未注册的工具名：应返回可读的错误并列出可用工具
        LLMToolCall missing;
        missing.name = "no_such_tool";
        LLMToolResult missResult = registry.call(missing);
        QVERIFY(missResult.isError);
        QVERIFY(missResult.content.contains("no_such_tool"));

        // 7) 注销来源后工具应全部消失
        registry.unregisterSource("mcp:mock");
        QCOMPARE(registry.count(), 0);

        // 8) 关闭后不应残留子进程
        client.stop();
        QVERIFY(!client.isRunning());
    }

#if defined(QSA_HAVE_LLAMA)
    // -----------------------------------------------------------------
    // llama.cpp 运行期绑定
    //
    // 这里只验证「DLL 能加载 + 符号能绑上 + 结构体默认值合理」，
    // 不加载 GGUF 模型（1.5 GB 且要几秒，不适合放进单测）。
    // 真正的推理正确性靠 tools/nlu_llm_finetune/eval_lora.py 验证。
    //
    // 这层必须测：绑定失败会在**第一次 NLU 请求**才暴露，
    // 而那时现场早就跑起来了，排查成本极高。
    // -----------------------------------------------------------------
    void llamaApiBinding() {
        auto *api = qsa::LlamaApi::instance();
        if (!api->load()) {
            // 没装 llama.cpp 的环境（如只跑 CI 的 Linux）跳过，
            // 这不是失败 —— 可选依赖。
            qWarning() << "llama.dll not available, skip:" << api->lastError();
            QSKIP("llama.cpp not installed");
        }
        QVERIFY(api->loaded());

        // 必需符号一个都不能少（load() 内部已检查，这里再确认非空，
        // 防止将来有人改 bind() 的 required 参数把它降级成可选）
        QVERIFY(api->llama_backend_init != nullptr);
        QVERIFY(api->llama_model_load_from_file != nullptr);
        QVERIFY(api->llama_init_from_model != nullptr);
        QVERIFY(api->llama_tokenize != nullptr);
        QVERIFY(api->llama_token_to_piece != nullptr);
        QVERIFY(api->llama_decode != nullptr);
        QVERIFY(api->llama_sampler_sample != nullptr);
        QVERIFY(api->llama_batch_get_one != nullptr);
        QVERIFY(api->llama_vocab_is_eog != nullptr);

        // 参数结构能正常取到 —— 这本身就是在验证结构体偏移/大小正确。
        // 注意别断言「默认值安全」：llama.cpp 的 n_gpu_layers 默认是 -1
        // （= 全部层交给 GPU），不是 0。真正保证不乱抢显存的是
        // LlamaCppBackend::load() 把配置里的 gpuLayers 写进去。
        llama_model_params mparams = api->llama_model_default_params();
        QVERIFY(mparams.n_gpu_layers == -1 || mparams.n_gpu_layers >= 0);

        llama_context_params cparams = api->llama_context_default_params();
        QVERIFY(cparams.n_ctx > 0);
        QVERIFY(cparams.n_batch > 0);
        // abort_callback 默认必须是 NULL，否则 load() 里挂的回调会失效，
        // 「中止」功能静默失灵
        QVERIFY(cparams.abort_callback == nullptr);

        // 搜索路径应当至少包含 exe 同级（构建期刚拷过去的 DLL）
        QVERIFY(!qsa::LlamaApi::searchPaths().isEmpty());

        // 不存在的目录不能让已加载的 api 变成未加载
        api->load("/definitely/not/here");
        QVERIFY(api->loaded());
    }

    // 模型缺失时的行为：必须干净地失败，而不是崩溃或卡住。
    // 这条路径在部署初期（还没 fetch 模型）就会被走到。
    void llamaBackendMissingModel() {
        QJsonObject cfg;
        cfg.insert("type", "llamacpp");
        cfg.insert("model", "definitely-not-here.gguf");
        cfg.insert("modelDir", "no_such_dir");
        LlamaCppBackend backend("test-missing", cfg);
        // 加载失败是预期结果；关键是 load() 返回 false 而不是崩溃
        QVERIFY(!backend.load());
        QVERIFY(!backend.isReady());
        // complete() 在未就绪时应返回空串而不是抛异常/死循环
        QVERIFY(backend.complete("hello", "sys", 0, 16, 100).isEmpty());
    }
#endif
};
#endif // TST_SHERPA_H
