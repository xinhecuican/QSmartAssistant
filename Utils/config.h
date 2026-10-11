#ifndef CONFIG_H
#define CONFIG_H
#include "LPcommonGlobal.h"
#include "Serializable.h"
#include "Template.h"

/**
 * @brief 运行期配置（单例）
 *
 * 文件位置： ~/.config/QSmartAssistant/Data/config.yaml
 *
 * 兼容策略（json → yaml 自动迁移）：
 *   - 优先读 config.yaml；存在即用。
 *   - 若只有老版本 config.json，则读入后立即写出 config.yaml，
 *     之后一律以 yaml 为准（json 保持原样不删，便于回滚排查）。
 *   - 保存时写 yaml，并从模板 default_config.yaml 回贴注释，
 *     保证运行期配置始终带说明、可读。
 */
class LPCOMMON_EXPORT Config : public Serializable {
    DECLARE_INSTANCE(Config)
public:
    Config();
    void serialized(QJsonObject *json) override;
    void deserialized(QJsonObject *json) override;

    /// 载入配置（yaml 优先，兼容老 json 并自动生成 yaml）
    void loadConfig();

    QJsonObject getConfig(const QString &name);

    void saveConfig(const QString &name, const QString &configName,
                    const QVariant &value);

    /// 解析配置中的路径为绝对路径（跨平台）：
    ///   - 绝对路径（"/xx" 或 "D:/xx"）原样规范化返回
    ///   - "~" / "~/xx" 展开为用户主目录
    ///   - 其余相对路径基于数据根（dataRoot()）解析
    static QString getDataPath(const QString &path);

    /// 数据根目录（可用环境变量 QSA_DATA_DIR 覆盖）
    static QString dataRoot();

    /// 运行期配置文件绝对路径（yaml）
    static QString configFilePath();
    /// 老版本配置文件绝对路径（json，仅用于迁移/兜底）
    static QString legacyConfigFilePath();
    /// 模板文件绝对路径（用于回贴注释）
    static QString templateFilePath();

private:
    QMap<QString, QJsonObject> configs;
};

#endif // CONFIG_H
