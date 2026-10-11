#ifndef QSA_LLM_SKILL_REGISTRY_H
#define QSA_LLM_SKILL_REGISTRY_H

#include "LLMTypes.h"
#include "LLMToolRegistry.h"
#include "../LPcommonGlobal.h"
#include <QMap>
#include <QObject>
#include <QStringList>

/**
 * @brief 技能（Skill）注册中心。
 *
 * 技能是「按需加载的操作知识」：一个技能 = 一个 markdown 文件，
 * 文件头是 YAML frontmatter，正文是给模型看的操作说明。
 *
 * 采用渐进式披露（progressive disclosure）避免把所有技能正文都塞进 prompt：
 *   1. 常驻 prompt 的只有 name + description（一行一条，很便宜）
 *   2. 模型认为需要时，调用 skill_read(name) 读取该技能的完整正文
 *
 * 目录结构（运行期 ~/.config/QSmartAssistant/Data/skills/）：
 *   skills/
 *     weather.md
 *     music.md
 *   每个文件形如：
 *     ---
 *     name: weather
 *     description: 查询指定城市的天气，支持今天/明天/未来三天
 *     ---
 *     # 天气查询
 *     1. 解析城市与时间槽位 ...
 */
class LPCOMMON_EXPORT SkillRegistry : public QObject {
    Q_OBJECT
public:
    explicit SkillRegistry(QObject *parent = nullptr);
    ~SkillRegistry() override;

    struct Skill {
        QString name;
        QString description;
        QString body;      // frontmatter 之后的正文
        QString filePath;  // 来源文件，便于排障
    };

    /**
     * @brief 从 config.json 的 "skills" 节配置的目录扫描技能。
     *
     *   "skills": {
     *     "enabled": true,
     *     "dir": "skills"          // 相对 Data/ 的目录名
     *   }
     */
    void loadFromConfig();

    /**
     * @brief 直接指定目录加载（测试 / 多目录场景）
     */
    void loadFromDir(const QString &dirPath);

    /**
     * @brief 注册 skill_read 工具，使模型可以按需拉取技能正文。
     *        同时把技能清单以「可用技能」形式追加到 systemPrompt。
     */
    void registerTools(LLMToolRegistry *registry);

    /**
     * @brief 生成「技能清单」文本，用于拼进 system prompt。
     *        没有技能时返回空串。
     */
    QString catalogPrompt() const;

    QStringList skillNames() const { return skills.keys(); }
    int count() const { return skills.size(); }
    bool contains(const QString &name) const { return skills.contains(name); }
    Skill skill(const QString &name) const { return skills.value(name); }

signals:
    void skillLoaded(const QString &name, const QString &filePath);

private:
    /**
     * @brief 解析单个 markdown 文件。缺失/格式错误的 frontmatter 返回 false。
     */
    static bool parseSkillFile(const QString &filePath, Skill *out);

    /**
     * @brief frontmatter 里 name: xxx 的极简 YAML 字段读取。
     *        刻意不引入 YAML 依赖——技能头只需要几个标量字段。
     */
    static QString frontmatterField(const QString &text, const QString &field);

private:
    QMap<QString, Skill> skills;
    QJsonArray catalogCache;
};

#endif // QSA_LLM_SKILL_REGISTRY_H