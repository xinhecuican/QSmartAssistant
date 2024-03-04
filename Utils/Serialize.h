#ifndef SERIALIZE_H
#define SERIALIZE_H
#include<QString>
#include "Serializable.h"
#include<QDir>
#include<QJsonObject>
#include<QJsonDocument>
#include<QDebug>
#include "LPcommonGlobal.h"

class LPCOMMON_EXPORT Serialize
{
public:
    static void serialize(QString path, Serializable* point);

    static bool deserialize(QString path, Serializable* point);

    static bool deserialize_data(QByteArray data, Serializable* point);

    static void append(QString path, Serializable* point);
};

#endif // SERIALIZE_H
