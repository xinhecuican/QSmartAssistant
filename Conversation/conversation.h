#ifndef CONVERSATION_H
#define CONVERSATION_H
#include "../Plugins/IPluginHelper.h"
#include "../Plugins/pluginmanager.h"
#include "../Recorder/player.h"
#include "ASR/ASRModel.h"
#include "NLU/nlumodel.h"
#include "TTS/TTSModel.h"
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QObject>
#include <QThread>

class Conversation : public QObject, public IPluginHelper {
    Q_OBJECT
public:
    Conversation(Player *player, QObject *parent = nullptr);
    void receiveData(const QByteArray &data);
    /**
     * @brief dialog
     * @param stop if true then stop dialog and clear data
     */
    void dialog(bool stop);
    void say(const QString &text, int id = 0, bool block = false,
             const QString &type = "") override;
    void stopSay(const QString &type, AudioPlaylist::AudioPriority priority =
                                          AudioPlaylist::NOTIFY) override;
    void stop();
    void quitImmersive(const QString &name) override;
    QString question(const QString &question) override;
    void onResponse();
    void exit() override;
    Player *getPlayer() override;
    Config *getConfig() override;
    LLMManager *getLLMManager() override;
    ParsedIntent parse(const QString &text) override;
    QList<QString> intentRequest(const QString& text, int id);
    void handlePlugin(const QString& text, const ParsedIntent& intent, int id);
signals:
    void finish();
    void requestResponse();
    void exitSig();
    void feedASR(const QByteArray &data, bool isLast = false, int id = 0);
    void feedTTS(const QString &text, const QString &type, int id = 0);
    void sayText(const QString& text, int id);

// for server
    void asrRecognize(QString result, int id);
public slots:
    void sayRawData(QByteArray data, int sampleRate, const QString &type, int id);

// for server
    void onASRRequest(const QByteArray& data, int id);

private:
signals:
    void clearASR();

private:
    Player *player;
    ASRModel *asr;
    TTSModel *tts;
    NLUModel *nlu;
    QByteArray cache;
    QString resultCache;
    PluginManager *pluginManager;
    QEventLoop eventLoop;
    qint64 index;
    qint64 endIndex;
    QEventLoop ttsEventLoop;
    bool isResponse;
    QThread asrThread;
    QEventLoop asrEventLoop;
    QThread ttsThread;
    bool asrProcessing;
    QMutex asrMutex;
    QWaitCondition asrCond;
    QMap<uint32_t, QList<QString>> ttsTexts;
};

#endif // CONVERSATION_H
