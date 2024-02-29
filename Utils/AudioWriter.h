#ifndef AUDIOWRITER_H
#define AUDIOWRITER_H
#include <QString>
#include <QByteArray>
#include <QAudioFormat>
#include <QFile>
#include <QDebug>
#include <math.h>

class AudioWriter{
public:
    struct WAVHEADER{
        //RIFF
        char RiffName[4];
        quint32 nRiffLength;

        char WavName[4];
        char FmtName[4];
        quint32 nFmtLength;

        quint16 nAudioFormat;
        quint16 nChannelNumber;
        quint32 nSampleRate;
        quint32 nBytesPerSecond;
        quint16 nBytesPerSample;
        quint16 nBitsPerSample;

        char DATANAME[4];
        quint32 nDataLength;
    };

    static void writeWav(QString path, QByteArray array, QAudioFormat format){
        static WAVHEADER wavHeader;
        qstrcpy(wavHeader.RiffName, "RIFF");
        qstrcpy(wavHeader.WavName, "WAVE");
        qstrcpy(wavHeader.FmtName, "fmt ");
        qstrcpy(wavHeader.DATANAME, "data");
        wavHeader.nFmtLength = 16;
        wavHeader.nAudioFormat = 1;
        wavHeader.nSampleRate = format.sampleRate();
        wavHeader.nChannelNumber = format.channelCount();
        wavHeader.nBytesPerSample = format.channelCount() * format.sampleSize() / 8;
        wavHeader.nBytesPerSecond = format.sampleRate() * format.channelCount() * format.sampleSize() / 8;
        wavHeader.nBitsPerSample = format.sampleSize();
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)){
            qCritical() << path << "open error";
            return;
        }
        wavHeader.nRiffLength = array.length() - 8 + sizeof(wavHeader);
        wavHeader.nDataLength = array.length();
        file.write((char*)&wavHeader, sizeof(wavHeader));
        file.write(array);
        file.close();
    }

    static WAVHEADER getHeader(int sampleRate, int dataLength){
        WAVHEADER wavHeader;
        qstrcpy(wavHeader.RiffName, "RIFF");
        qstrcpy(wavHeader.WavName, "WAVE");
        qstrcpy(wavHeader.FmtName, "fmt ");
        qstrcpy(wavHeader.DATANAME, "data");
        wavHeader.nFmtLength = 16;
        wavHeader.nAudioFormat = 1;
        wavHeader.nSampleRate = sampleRate;
        wavHeader.nChannelNumber = 1;
        wavHeader.nBytesPerSample = 2;
        wavHeader.nBytesPerSecond = sampleRate * 2;
        wavHeader.nBitsPerSample = 16;
        wavHeader.nRiffLength = dataLength - 8 + sizeof(wavHeader);
        wavHeader.nDataLength = dataLength;
        return wavHeader;
    }

    static void changeVol(QByteArray& data, int db){
        char* rawData = const_cast<char*>(data.data());
        int16_t* intData = reinterpret_cast<int16_t*>(rawData);
        float multiplier = pow(10, db/20.);
        for(int i=0; i<data.size()/2; i++){
            int value = intData[i] * multiplier;
            if(value > 32767) value = 32767;
            else if(value < -32768) value = -32768;
            intData[i] = value;
        }
    }
};

#endif // AUDIOWRITER_H
