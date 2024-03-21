#ifndef CONVERSATION_H
#define CONVERSATION_H
#include <QObject>
#include <QEventLoop>
#include "ASR/ASRModel.h"
#include "TTS/TTSModel.h"
#include "NLU/nlumodel.h"
#include "../Plugins/pluginmanager.h"
#include "../Plugins/IPluginHelper.h"
#include "../Recorder/player.h"
#include <QThread>

class Conversation : public QObject, public IPluginHelper
{
    Q_OBJECT
public:
    Conversation(Player* player, QObject* parent=nullptr);
    void receiveData(const QByteArray& data);
    /**
     * @brief dialog
     * @param stop if true then stop dialog and clear data
     */
    void dialog(bool stop);
    void say(const QString& text, bool block=false) override;
    void stop();
    void quitImmersive(const QString& name) override;
    QString question(const QString& question) override;
    void onResponse();
    void exit() override;
    Player* getPlayer() override;
    Config* getConfig() override;
signals:
    void finish();
    void requestResponse();
    void exitSig();
public slots:
    void sayRawData(QByteArray data, int sampleRate);
private:
signals:
    void feedASR(const QByteArray& data, bool isLast=false);
    void feedTTS(const QString& text);
    void onRecognize(QString result);
    void clearASR();
private:
    Player* player;
    ASRModel* asr;
    TTSModel* tts;
    NLUModel* nlu;
    QByteArray cache;
    QString resultCache;
    PluginManager* pluginManager;
    QEventLoop eventLoop;
    qint64 index;
    qint64 endIndex;
    QEventLoop ttsEventLoop;
    bool isResponse;
    QThread asrThread;
    QEventLoop asrEventLoop;
    QThread ttsThread;
};

#endif // CONVERSATION_H
