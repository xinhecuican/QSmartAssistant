#ifndef QSA_LLM_BUILTIN_TOOLS_H
#define QSA_LLM_BUILTIN_TOOLS_H

#include "LLMTypes.h"
#include "LLMToolRegistry.h"

class LLMManager;

/**
 * @brief 注册框架自带的进程内工具。
 *
 * 这些工具不需要外部进程，是让 LLM「能感知到自己能做什么」的最小集合：
 *   - current_time：模型常常不知道真实时间，天气/日程类问题必需
 *   - list_skills ：技能清单（与 SkillRegistry 的 skill_list 同源）
 */
void registerBuiltinTools(LLMManager *manager);

/**
 * @brief 构造一个「字符串 + 可选枚举」参数的 JSON Schema，省去手写 JSON 的样板。
 */
QJsonObject makeStringProperty(const QString &description, bool required = false,
                              const QStringList &enumValues = QStringList());

#endif // QSA_LLM_BUILTIN_TOOLS_H