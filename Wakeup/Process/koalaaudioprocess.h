#ifndef KOALAAUDIOPROCESS_H
#define KOALAAUDIOPROCESS_H
#include "audioprocess.h"
#include "pv_koala.h"

class KoalaAudioProcess : public AudioProcess
{
public:
    KoalaAudioProcess(QObject* parent=nullptr);
    ~KoalaAudioProcess();
    void preProcess(QByteArray& data) override;
    void stop()override;
private:
    pv_koala_t* koala;
    int16_t* enhancedData;
    char splitData[513];
    int chunkSize;
};

#endif // KOALAAUDIOPROCESS_H
