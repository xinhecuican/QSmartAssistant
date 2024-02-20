#ifndef TST_SHERPA_H
#define TST_SHERPA_H
#include <QtTest/QtTest>
#include "Recorder/recorder.h"
#include "Conversation/ASR/sherpaasr.h"
#include "Conversation/ASR/duiliteasr.h"
#include "Utils/config.h"
#include "Conversation/TTS/sherpatts.h"
#include "Recorder/player.h"
#include "duilite.h"
#include <QLibrary>
#include "../Utils/wavfilereader.h"
using namespace AC;

class tst_sherpa: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase(){
        Config::instance()->loadConfig();
    }
    // void asr(){
    //     WavFileReader reader;
    //     reader.OpenWavFile("Data/short_test.wav");
    //     DuiliteASR* sherpa = new DuiliteASR(this);
    //     QByteArray* cache = new QByteArray;
    //     int size = 0;
    //     do{
    //         char buf[1024];
    //         size = reader.ReadData(buf, 640);
    //         if(size > 0){
    //             cache->append(buf, size);
    //         }
    //     }while(size);
    //     QString result = sherpa->detect(*cache);
    //     qDebug() << result;
    //     // QCOMPARE(result, "这是一个短的测试");
    //     QTest::qWait(4000);
    // }
    void testPlayer(){
        // QFile file("Tmp/1708062224936.wav");
        // file.open(QIODevice::ReadOnly);
        // QByteArray data = file.readAll();
        // Player::instance()->playRaw(data, 8000);
        // QBuffer buffer(&data);
        // QMediaPlayer* player = new QMediaPlayer;
        // QFileInfo fileInfo("Tmp/1708062224936.wav");
        // player->setMedia(QMediaContent(QUrl::fromLocalFile(fileInfo.absoluteFilePath())));
        Player::instance()->playSoundEffect("Data/short_test.wav");
        QTest::qWait(4000);
    }
    // void tts(){
    //     SherpaTTS* tts = new SherpaTTS();
    //     connect(tts, &SherpaTTS::dataArrive, this, [=](QByteArray data, int sampleRate){
    //         Player::instance()->playRaw(data, sampleRate, AudioPlaylist::NOTIFY);
    //     });
    //     tts->detect("这是一个短的测试");
    //     tts->detect("本系列主要目标初步完成一款智能音箱的基础功能，包括语音唤醒、语音识别(语音转文字)、处理用户请求（比如查天气等，主要通过rasa自己定义意图实现）、语音合成(文字转语音)功能。"
    //                 "语音识别、语音合成采用离线方式实现。"
    //                 "语音识别使用sherpa-onnx，可以实现离线中英文语音识别。");
    //     QTest::qWait(20000);
    // }
    // void duilite_gram(){
    //     QLibrary lib("libduilite.so");
    //     QJsonObject duiliteConfig = Config::instance()->getConfig("duilite");
    //     QString loginPath = Config::getDataPath(duiliteConfig.value("login").toString());
    //     QFile file(loginPath);
    //     if(!file.open(QIODevice::ReadOnly)){
    //         qCritical()<< "duilite login open error";
    //         return;
    //     }
    //     QByteArray loginData = file.readAll();
    //     file.close();
    //     int ret = ((int(*)(char*))lib.resolve("duilite_library_load"))(loginData.data());
    //     if(ret){
    //         qCritical() << "duilite library open error";
    //         return;
    //     }
    //     QString res = Config::getDataPath(duiliteConfig.value("asrRes").toString());
    //     QString gram = Config::getDataPath(duiliteConfig.value("gram").toString());
    //     QString cfg =   "{\"resBinPath\": \"" + res + "\"}";
    //     std::string resS = cfg.toStdString();
    //     char* resData = (char*)resS.c_str();
    //     struct duilite_gram* grammer;
    //     // ((struct duilite_gram*(*)(char*))lib.resolve("duilite_gram_new"))
    //     grammer = duilite_gram_new(resData);
    //     QString cfg2 = "{\"outputPath\": \"gram.gram\", \"ebnfFile\": \"" + gram + "\"}";
    //     std::string cfg2S = cfg2.toStdString();
    //     ((int(*)(struct duilite_gram*, char*))lib.resolve("duilite_gram_start"))(grammer, (char*)cfg2S.c_str());
    // }
};
#endif // TST_SHERPA_H
