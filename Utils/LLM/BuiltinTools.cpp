#include "BuiltinTools.h"
#include "LLMManager.h"
#include "SkillRegistry.h"
#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

QJsonObject makeStringProperty(const QString &description, bool required,
                              const QStringList &enumValues) {
    QJsonObject prop;
    prop.insert("type", "string");
    prop.insert("description", description);
    if (!enumValues.isEmpty()) {
        QJsonArray values;
        for (const QString &v : enumValues) {
            values.append(v);
        }
        prop.insert("enum", values);
    }
    Q_UNUSED(required);
    return prop;
}

void registerBuiltinTools(LLMManager *manager) {
    if (manager == nullptr) {
        return;
    }

    // ---- current_time ----
    {
        QJsonObject params;
        params.insert("type", "object");
        QJsonObject props;
        props.insert("timezone",
                     makeStringProperty("IANA timezone name, e.g. Asia/Shanghai. "
                                        "Omit for the device local timezone."));
        params.insert("properties", props);
        manager->registerTool(
            "current_time",
            "Get the current date and time. Call this before answering any "
            "question that depends on the current date, time of day or weekday.",
            params,
            [](const QJsonObject &arguments, QString *error) -> LLMToolResult {
                const QString tz = arguments.value("timezone").toString();
                QDateTime now = QDateTime::currentDateTime();
                if (!tz.isEmpty()) {
                    QTimeZone zone(tz.toUtf8());
                    if (zone.isValid()) {
                        now = now.toTimeZone(zone);
                    } else {
                        if (error) {
                            *error = "invalid timezone: " + tz;
                        }
                        return LLMToolResult(
                            "error: invalid timezone '" + tz +
                                "'. Use an IANA name like Asia/Shanghai.",
                            true);
                    }
                }
                const QString weekDay =
                    QLocale::system().dayName(now.date().dayOfWeek(),
                                               QLocale::LongFormat);
                // 用 IANA id 而不是本地化显示名：模型能识别的稳定标识是
                // "Asia/Shanghai" 这种 ASCII 串，本地化名（如"中国标准时间"）
                // 对模型没有意义。
                const QString zoneName =
                    tz.isEmpty() ? QTimeZone::systemTimeZone().id() : tz;
                return LLMToolResult(
                    QString("%1 %2 %3 (timezone: %4)")
                        .arg(now.toString("yyyy-MM-dd"), weekDay,
                             now.toString("HH:mm"), zoneName));
            });
    }

    // ---- list_skills ----
    {
        QJsonObject params;
        params.insert("type", "object");
        params.insert("properties", QJsonObject());
        manager->registerTool(
            "list_skills",
            "List available skills with a one-line description of each.",
            params,
            [manager](const QJsonObject &, QString *error) -> LLMToolResult {
                Q_UNUSED(error);
                const QString catalog = manager->skills()->catalogPrompt();
                return LLMToolResult(catalog.isEmpty()
                                         ? QString("no skills available")
                                         : catalog);
            });
    }
}