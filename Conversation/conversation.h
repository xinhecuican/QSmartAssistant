#ifndef CONVERSATION_H
#define CONVERSATION_H
#include <QObject>
#include "ASR/ASRModel.h"
#include "TTS/TTSModel.h"
#include "NLU/nlumodel.h"
#include "../Plugins/pluginmanager.h"

class Conversation : public QObject
{
    Q_OBJECT
public:
    Conversation(QObject* parent=nullptr);
    void receiveData(const QByteArray& data);
    /**
     * @brief dialog
     * @param stop if true then stop dialog and clear data
     */
    void dialog(bool stop);
    void say(const QString& text);
    void stop();
signals:
    void finish();
public slots:
    void sayRawData(QByteArray data, int sampleRate);
private:
    ASRModel* asr;
    TTSModel* tts;
    NLUModel* nlu;
    QByteArray cache;
    QString resultCache;
    PluginManager* pluginManager;
};

#endif // CONVERSATION_H
