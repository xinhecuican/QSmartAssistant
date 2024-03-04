#ifndef CONFIG_H
#define CONFIG_H
#include "Serializable.h"
#include "LPcommonGlobal.h"
#include "Template.h"

class LPCOMMON_EXPORT Config : public Serializable
{
    DECLARE_INSTANCE(Config)
public:
    Config();
    void serialized(QJsonObject* json) override;
    void deserialized(QJsonObject* json) override;
    void loadConfig();
    QJsonObject getConfig(const QString& name);
    void saveConfig(const QString& name, const QString& configName, const QVariant& value);
    static QString getDataPath(const QString& path);

private:
    QMap<QString, QJsonObject> configs;
};

#endif // CONFIG_H
