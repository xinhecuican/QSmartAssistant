#include "Serialize.h"

void Serialize::serialize(QString path, Serializable* point)
{
    QString dir_path = path.mid(0, path.lastIndexOf("/")+1);
    QDir base_dir = QDir(dir_path);
    if(!base_dir.exists())
    {
        QDir dir;
        dir.mkdir(dir_path);
    }
    QFile file(path);
    QJsonObject jsonObject = QJsonObject();
    try
    {
        file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate);
    }
    catch (...)
    {
        qWarning("配置文件打开错误\n位置： Serialize::serialize");
        file.close();
    }
    point->serialized(&jsonObject);
    QJsonDocument jsonDoc;
    jsonDoc.setObject(jsonObject);
    file.write(jsonDoc.toJson());
    file.close();
}

bool Serialize::deserialize(QString path, Serializable* point)
{
    QFile file(path);
    if(!file.exists())
    {
        return false;
    }
    else
    {
        try
        {
            file.open(QIODevice::ReadOnly | QIODevice::Text);
        } catch (...) {
            qWarning("配置文件打开错误\n位置：Serialize::deserialize");
            file.close();
            return false;
        }

        QByteArray allData = file.readAll();
        file.close();

        QJsonParseError json_error;
           QJsonDocument jsonDoc(QJsonDocument::fromJson(allData, &json_error));
        if(json_error.error != QJsonParseError::NoError)
        {
            qWarning("配置文件打开错误\n位置：Serialize::deserialize");
            return false;
        }
        QJsonObject rootObj = jsonDoc.object();
        point->deserialized(&rootObj);
    }
    return true;
}

bool Serialize::deserialize_data(QByteArray data, Serializable* point)
{

    QJsonParseError json_error;
    QJsonDocument jsonDoc(QJsonDocument::fromJson(data, &json_error));
    if(json_error.error != QJsonParseError::NoError)
    {
        qWarning("配置文件打开错误\n位置: Serailize::deserialize_data");
        return false;
    }
    QJsonObject rootObj = jsonDoc.object();
    point->deserialized(&rootObj);
    return true;
}

void Serialize::append(QString path, Serializable* point)
{
    QString dir_path = path.mid(0, path.lastIndexOf("/")+1);
    QDir base_dir = QDir(dir_path);
    if(!base_dir.exists())
    {
        QDir dir;
        dir.mkdir(dir_path);
    }
    QFile file(path);
    try
    {
        file.open(QIODevice::ReadWrite | QIODevice::Text);
    }
    catch (...)
    {
        qWarning("配置文件打开错误\n位置Serialize::append");
        file.close();
    }
    QByteArray allData = file.readAll();
    file.close();

    QJsonParseError json_error;
    QJsonDocument jsonDoc(QJsonDocument::fromJson(allData, &json_error));
    if(json_error.error != QJsonParseError::NoError)
    {
        qWarning("配置文件打开错误\n位置Serialize::append");
    }
    QJsonObject rootObj = jsonDoc.object();
    point->serialized(&rootObj);
    jsonDoc.setObject(rootObj);
    try
    {
        file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate);
    }
    catch (...)
    {
        qWarning("配置文件打开错误\n位置Serialize::append");
        file.close();
    }
    file.write(jsonDoc.toJson());
    file.close();
}
