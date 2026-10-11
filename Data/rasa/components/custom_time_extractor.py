#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""custom_time_extractor.py —— 把 Recognizers-Text 的时间结果**补充**到消息上，而非**替换**。

原实现（备份见 `_orig_backup/custom_time_extractor.py`）有三个问题：

1) **会删掉 DIETClassifier 抽出的所有 `time` 实体**，再塞自己的结果 —— DIET 抽对了也被覆盖。
   而 `nlu.yml` 里 `time` 标的就是**原文**（`[今天](time)`、`[明天](time)`、
   `[今天晚上](time)`、`[今后三天](time)`），DIET 本来抽的就是对的口径。

2) **中文的字符偏移是错的**（`tools/nlu_llm_finetune/probe_extractors.py` 实测）：

       深圳今天热不热   -> (2, 3, '今',     'date', '2026-10-10')  ← 只要了「今」
       明后天天气如何   -> (1, 2, '后',     'date', '2026-10-12')  ← 只要了「后」
       音量调到三十     -> (4, 5, '三',     'date', '2026-09-30')  ← 只要了「三」
       把声音调低一点   -> (5, 6, '一',     'time', '01:00:00')    ← 「一点」不是时间
       今天晚上会下雨吗 -> (0, 3, '今天晚', 'datetimerange', ...)   ← span 残缺

   偏移本身就不可用，所以「靠它识别任意时间表达」这个卖点在本语料上是负收益。

3) 输出的是**解析后的绝对日期**（`2026-10-10`），与 `nlu.yml` 的原文标注约定冲突。

现行为：只在该 span **本身就是一个完整的时间表达**（白名单正则）时才补充，
且**绝不删除 DIET 的结果**；`value` 默认用 span 原文。
偏移残缺 / 根本不是时间表达的（「今」「后」「三」「一」）一律丢弃 —— 交给 DIET。

另外原文件里 `datetime1.hour()` 是错的（`hour` 是属性不是方法，走到就抛
TypeError），以及模块级留了一句 `recognize_datetime("一个半小时", ...)` 的调试调用，
都一并清掉。
"""

import re
from typing import Any, Dict, List, Text

from rasa.engine.graph import ExecutionContext, GraphComponent
from rasa.engine.recipes.default_recipe import DefaultV1Recipe
from rasa.engine.storage.resource import Resource
from rasa.engine.storage.storage import ModelStorage
from rasa.shared.nlu.constants import ENTITIES, TEXT
from rasa.nlu.extractors.extractor import EntityExtractorMixin
from rasa.shared.nlu.training_data.message import Message
from recognizers_date_time import DateTimeRecognizer
from recognizers_text import Culture

# 一个「完整的」时间表达。必须整体匹配，用来拒绝 Recognizers 在中文上产出的残片
# （「今」「后」「三」「一」「今天晚」都会被拒掉）。
TEMPORAL_RE = re.compile(
    r"^(?:"
    # 今天 / 明天 / 后天 / 大后天 / 昨天 / 前天（+ 时段）
    r"(?:今天|明天|后天|大后天|昨天|前天)"
    r"(?:早上|上午|中午|下午|晚上|夜里|凌晨|清晨|傍晚|半夜)?|"
    # 今晚 / 明早 / 昨天晚上 ...
    r"(?:今|明|昨)(?:晚|早)(?:上|晨)?|"
    # 单独时段
    r"(?:早上|上午|中午|下午|晚上|夜里|凌晨|清晨|傍晚|半夜)|"
    # 星期
    r"星期[一二三四五六日天末]|周[一二三四五六日天末]|礼拜[一二三四五六日天末]|周末|"
    # 相对区间
    r"(?:今|明|未)?后\d+天|未来\d+天|"
    # 口语
    r"现在|此刻|马上|待会儿|一会儿|"
    # 时刻
    r"\d{1,2}[点时](?:钟|半|\d{1,2}分)?|"
    # 年月日
    r"\d{1,2}月\d{1,2}[日号]|\d{4}年(?:\d{1,2}月)?"
    r")$"
)


@DefaultV1Recipe.register(
    DefaultV1Recipe.ComponentType.ENTITY_EXTRACTOR,
    is_trainable=False
)
class CustomTimeExtractor(GraphComponent, EntityExtractorMixin):
    """Entity extractor which uses regular expressions to find time expressions."""

    @staticmethod
    def get_default_config() -> Dict[Text, Any]:
        """The component's default config."""
        return {
            # True  -> value 用 span 原文（对齐 nlu.yml 的标注约定：[今天](time)）
            # False -> value 用 Recognizers 解析出的绝对日期（旧行为）
            "value_as_text": True,
        }

    def __init__(self, config: Dict[Text, Any]) -> None:
        """Initialize CustomTimeExtractor."""
        self._config = config
        recognizer = DateTimeRecognizer(Culture.Chinese)
        self.model = recognizer.get_datetime_model()

    @classmethod
    def create(
        cls,
        config: Dict[Text, Any],
        model_storage: ModelStorage,
        resource: Resource,
        execution_context: ExecutionContext,
    ) -> GraphComponent:
        """Creates a new component."""
        return cls(config)

    @staticmethod
    def _resolve(result) -> Text:
        """把 Recognizers 的 resolution 拍成一个字符串（仅 value_as_text=False 时用）。"""
        first = result.resolution["values"][0]
        if first["type"] in ("datetimerange", "daterange"):
            return "%s|%s" % (first.get("start", ""), first.get("end", ""))
        return first.get("value", "")

    def process(self, messages: List[Message]) -> List[Message]:
        """Extract time expressions, keeping whatever DIET already found."""
        as_text = self._config.get("value_as_text", True)

        for message in messages:
            text = message.get(TEXT) or ""
            found = []                        # type: List[Dict[Text, Any]]

            for result in self.model.parse(text):
                start, end = result.start, result.end
                if start is None or end is None or end <= start:
                    continue                  # 零宽 / 空 span
                span = text[start:end]
                if not TEMPORAL_RE.match(span):
                    continue                  # 残缺或根本不是时间表达（「今」「后」「三」）
                found.append({
                    "entity": "time",
                    "value": span if as_text else self._resolve(result),
                    "text": span,
                    "start": start,
                    "end": end,
                    "confidence": 0,
                    "extractor": "CustomTimeExtractor",
                })

            # ★ 不再删除 DIET 的结果：DIET 优先，规则结果只补空缺
            entities = list(message.get(ENTITIES) or [])
            for entity in found:
                overlapped = any(
                    e.get("start") is not None and e.get("end") is not None
                    and entity["start"] < e["end"] and e["start"] < entity["end"]
                    for e in entities
                )
                if not overlapped:
                    entities.append(entity)
            message.set(ENTITIES, entities, add_to_output=True)

        return messages
