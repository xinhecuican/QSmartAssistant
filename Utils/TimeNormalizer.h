#ifndef TIMENORMALIZER_H
#define TIMENORMALIZER_H

#include <QDateTime>
#include <QString>

#include "LPcommonGlobal.h"

/**
 * TimeNormalizer —— 把 NLU 抽出的 time 跨度**原文**归一化成「相对 now 的秒偏移」。
 *
 * 背景
 *   time 槽位历来存的是跨度的原文（「今天」「明天早上」「30分钟后」），
 *   但这个口径两个消费者都吃不下：
 *     - hass    —— `{time|--}` 会把槽位值**原样**塞进 HA 脚本变量（例如 duration），
 *                  HA 的 delay 需要的是秒数；中文原文进去等于没传。
 *     - Weather —— 需要 `yyyy-MM-dd[ hh:mm:ss]`。
 *   `Plugins/hass/hass.cpp` 的 parseValue() 只是把槽位字符串填进占位符，
 *   所以「原文 → 秒」这一步必须在本层完成。
 *
 * 为什么放在这里
 *   entity2Slot() 本来就是语义归一化层 —— 它已经把 trend 的原文跨度
 *   （「调大点」/「太小了」）翻译成 `--HIGHER--` / `--SMALLER--`。
 *   time 只是当年漏掉的那一个。放在这一层，rasa 与 llm 两个后端一次改完，
 *   且不需要重新训练（训练语料仍按原文跨度标注，DIET/LLM 学的是跨度本身）。
 *
 * 输出约定
 *   单值   "1800"          距 now 的秒数，可为负（表示过去）
 *   区间   "259200|432000" 形如 start|end 的秒偏移
 *   无法识别 空串           由调用方决定回退（hass 会丢弃该键，用 `--` 默认值）
 *
 * 取值约定（供聚合型消费者推断粒度）
 *   只到「日」的表达（今天 / 明天 / 周末 / 今后三天）落在目标日 00:00:00；
 *   带时段或时刻的表达（今晚 / 明天早上 / 明早8点）落在具体钟点。
 *   因此下游可以按「是否恰好 00:00:00」判断该按天还是按小时处理，
 *   不需要额外的类型标记。
 *
 * 「顺延到未来」的三条规则（都只在**没写明确日子**时生效）
 *   1. 只给钟点/时段（「8点」「中午」），而今天该时刻已过 → 顺延到明天；
 *   2. 只给月日、没写年份（「3月5日」），而今年该日已过   → 顺延到明年；
 *   3. 显式写了日子（今天 / 明天 / 今晚 / 周三）           → 不顺延，
 *      即使算出来是过去时刻（「今天早上」在傍晚说就是今天早上）。
 */
class LPCOMMON_EXPORT TimeNormalizer {
public:
    /// 归一化。`now` 显式传入以便单元测试注入固定时刻。
    static QString normalize(const QString &raw,
                             const QDateTime &now = QDateTime::currentDateTime());
};

#endif // TIMENORMALIZER_H
