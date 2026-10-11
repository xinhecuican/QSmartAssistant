#ifndef YAML_H
#define YAML_H
#include <QByteArray>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include "LPcommonGlobal.h"

/**
 * @brief YAML 读写（基于 yaml-cpp 0.8.0 的薄封装）
 *
 * - 解析：YAML → QJsonValue（映射→Object、序列→Array、标量→对应的 bool/double/string）
 * - 序列化：QJsonValue → YAML，并支持携带注释
 *
 * 注释机制（本项目的约定）：
 *   程序写盘时用「注释表 + 仓库模板文件」还原注释：
 *   1) 先按模板(default_config.yaml)中的注释建立 key→comment 映射；
 *   2) dump() 时用 yaml-cpp 的 emitter 手工控制输出顺序，
 *      在写出每个键之前先把注释行原样写出去。
 *   yaml-cpp 本身会丢弃注释，所以这里不是「保留原文注释」，而是
 *   「按模板重新贴注释」——这也是运行期 config.yaml 始终保持说明可读的原因。
 *
 * 类型保真：
 *   旧 json 配置里的数字有整数与浮点两类，QJsonValue 只有 double。
 *   序列化时整数值若等于 floor 则写成不带小数点的形式，避免 66 → 66.0。
 */
class LPCOMMON_EXPORT Yaml {
public:
    /// 解析 YAML 文本为 QJsonValue（顶层通常是 Object）。
    /// 失败时 ok 置 false 并填写 errMsg。
    static QJsonValue parse(const QByteArray &data, bool *ok = nullptr,
                            QString *errMsg = nullptr);

    /// 从文件读取并解析。文件不存在/解析失败返回 Undefined 且 ok=false。
    static QJsonValue parseFile(const QString &path, bool *ok = nullptr,
                                QString *errMsg = nullptr);

    /// 序列化为 YAML 文本。
    /// @param comments   键 -> 注释；键用 "a:b:c" 路径定位，注释支持多行(\n)。
    /// @param header     文件头注释（多行用 \n）；为空时用内置默认头。
    static QByteArray dump(const QJsonValue &root,
                           const QMap<QString, QString> &comments = {},
                           const QString &header = QString());

    /// 序列化并写文件（自动建目录）。
    static bool dumpFile(const QString &path, const QJsonValue &root,
                         const QMap<QString, QString> &comments = {},
                         const QString &header = QString());

    /// 从另一份 YAML/JSON 文件抽取注释表（用于把模板注释贴回运行期文件）。
    static QMap<QString, QString> collectComments(const QString &filePath);

    /// 把 JSON 文本转成 YAML 文本。
    static QByteArray jsonToYaml(const QByteArray &json,
                                 const QMap<QString, QString> &comments = {},
                                 const QString &header = QString(),
                                 bool *ok = nullptr);
};

#endif // YAML_H
