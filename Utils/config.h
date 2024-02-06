#ifndef CONFIG_H
#define CONFIG_H
#include "Template.h"
#include <QMutex>
#include <QScopedPointer>
#include "Serializable.h"

class Config : public Serializable
{
    DECLARE_INSTANCE(Config)
public:
    Config();
    void serialized(QJsonObject* json) override;
    void deserialized(QJsonObject* json) override;
    void loadConfig();
    QJsonObject getConfig(const QString& name);
    static QString getDataPath(const QString& path);

private:
    QMap<QString, QJsonObject> configs;
};

#endif // CONFIG_H
