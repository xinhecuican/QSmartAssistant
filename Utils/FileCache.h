#ifndef FILECACHE_H
#define FILECACHE_H
#include "AudioWriter.h"
#include "Template.h"
#include "../Utils/config.h"
#include <QDir>

class FileCache{
    DECLARE_INSTANCE(FileCache)
public:
    FileCache(){
        QDir tmpDir(Config::getDataPath("Tmp"));
        if(!tmpDir.exists()){
            tmpDir.mkpath(Config::getDataPath("Tmp"));
        }
        QFileInfoList files = tmpDir.entryInfoList(QDir::Files);
        for(QFileInfo& info : files){
            if(info.suffix() == "wav"){
                tmpDir.remove(info.fileName());
            }
        }
    }

    QString writeWav(const QByteArray& data, int sampleRate){
        AudioWriter::WAVHEADER header = AudioWriter::getHeader(sampleRate, data.size());
        QString fileName = Config::getDataPath("Tmp/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".wav");
        QFile file(fileName);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)){
            qWarning() << fileName << "create error";
            return fileName;
        }
        file.write((char*)&header, sizeof(header));
        file.write(data);
        file.close();
        files.append(fileName);
        if(files.size() > 30){
            QFile removeFile(files[0]);
            removeFile.remove();
            files.removeFirst();
        }
        return fileName;
    }
private:
    QList<QString> files;
};

#endif // FILECACHE_H
