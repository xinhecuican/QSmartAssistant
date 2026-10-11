#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""custom_number_extractor.py —— 把 Recognizers-Text 的数字结果**补充**到消息上，而非**替换**。

原实现（备份见 `_orig_backup/custom_number_extractor.py`）有两个硬伤：

1) **会删掉 DIETClassifier 抽出的所有 `number` 实体**
       exclude_entities = [e for e in entities if e['entity'] != 'number']
   然后把自己算出来的塞进去 —— 也就是说 DIET 抽对了也会被覆盖。

2) **对中文的判定过于激进**（`tools/nlu_llm_finetune/probe_extractors.py` 实测）：

       换一首         -> (1, 1, '', '1')      ← 零宽 span（start == end），值为 1
       下一首         -> (1, 1, '', '1')
       报一下机器状态   -> (1, 1, '', '1')
       帮我把灯开一下   -> (5, 5, '', '1')
       音量调到三十     -> (4, 5, '三', '30')  ← 截断，只取到「三」

   这些会被当成真实槽位发给插件（`number=1`），是纯粹的误报。

3) `nlu.yml` 的标注约定是**原文**（`[50](number)`、`[百分之七十](number)`），
   而这里输出的是**解析值**（`'70%'`）。本组件现在默认按约定输出 span 原文。

现行为：过滤掉「零宽 / 截断 / 量词搭配 / 半截表达」四类误报，
再与 DIET 的结果**合并去重**（DIET 优先，重叠不加）。
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
from recognizers_number import NumberRecognizer
from recognizers_text import Culture

# 数字类字符：span 后面紧跟这些字符，说明 span 被截断了（「三十」只取到「三」）
NUMERAL_CHARS = "一二三四五六七八九十百千万亿零壹贰叁肆伍陆柒捌玖拾佰仟廿两0123456789."

# 量词 / 动作补语：数词后面紧跟它们时，数词修饰的是「动作或名词的量」，不是「取值」。
# 例：「一**首**」「一**下**」「一**个**」不是 number；「调到**三十**」才是。
MEASURE_WORDS = (
    "首|下|个|点|遍|次|会|阵|句|把|些|趟|场|番|回|层|片|块|条|根|张|只|件|杯|瓶|碗|份|"
    "岁|年|月|日|天|周|小时|分钟|秒|米|公里|公斤|斤|度|步|声|口|眼|手|脚|丝|毫|点儿|下子|会儿"
)
MEASURE_RE = re.compile(r"^(?:%s)" % MEASURE_WORDS)

# 一个「完整的」数字表达：可选「百分之」前缀 + 中文/阿拉伯数字 + 可选百分号
FULL_NUMBER_RE = re.compile(
    r"^(?:百分之)?[一二三四五六七八九十百千万亿零壹贰叁肆伍陆柒捌玖拾佰仟廿两\d]+"
    r"(?:\.\d+)?\s*%?$"
)


@DefaultV1Recipe.register(
    DefaultV1Recipe.ComponentType.ENTITY_EXTRACTOR,
    is_trainable=False
)
class CustomNumberExtractor(GraphComponent, EntityExtractorMixin):
    """Entity extractor which uses regular expressions to find numbers."""

    @staticmethod
    def get_default_config() -> Dict[Text, Any]:
        """The component's default config."""
        return {
            # 去掉「一首 / 一下 / 一个」这类量词搭配产生的伪数字
            "drop_measure_word_numbers": True,
            # True  -> value 用 span 原文（对齐 nlu.yml 的标注约定）
            # False -> value 用 Recognizers 的解析值（旧行为，如 '70%'）
            "value_as_text": True,
        }

    def __init__(self, config: Dict[Text, Any]) -> None:
        """Initialize CustomNumberExtractor."""
        self._config = config
        recognizer = NumberRecognizer(Culture.Chinese)
        self.num_model = recognizer.get_number_model()
        self.percentage_model = recognizer.get_percentage_model()

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

    # -- 守卫 ---------------------------------------------------------------
    @staticmethod
    def _span_is_usable(text: Text, start: int, end: int) -> bool:
        """span 必须非空、且没有被截断。"""
        if start is None or end is None or end <= start:
            return False                      # 零宽 / 空 span
        if end < len(text) and text[end] in NUMERAL_CHARS:
            return False                      # 多位数被截断（「三十」-> 「三」）
        return True

    @staticmethod
    def _looks_like_full_number(span: Text) -> bool:
        return bool(FULL_NUMBER_RE.match(span.replace(" ", "")))

    def _is_measure_word_usage(self, text: Text, end: int) -> bool:
        if not self._config.get("drop_measure_word_numbers", True):
            return False
        return bool(MEASURE_RE.match(text[end:]))

    # -- 主流程 -------------------------------------------------------------
    def process(self, messages: List[Message]) -> List[Message]:
        """Extract numbers using regular expressions."""
        as_text = self._config.get("value_as_text", True)

        for message in messages:
            text = message.get(TEXT) or ""
            found = []                        # type: List[Dict[Text, Any]]

            for model in (self.num_model, self.percentage_model):
                for result in model.parse(text):
                    start, end = result.start, result.end
                    if not self._span_is_usable(text, start, end):
                        continue
                    if self._is_measure_word_usage(text, end):
                        continue
                    # 百分号常落在 span 之外（`25%` 的 span 只是 `25`），并进来
                    if end < len(text) and text[end] == "%":
                        end += 1
                    span = text[start:end]
                    if not self._looks_like_full_number(span):
                        continue              # 半截表达（如 `百分之七`）交给 DIET
                    found.append({
                        "entity": "number",
                        "value": span if as_text else result.resolution["value"],
                        "start": start,
                        "end": end,
                        "confidence": 75,
                        "extractor": "CustomNumberExtractor",
                    })

            # ★ 不再删除 DIET 的结果：DIET 优先，规则结果按 span 去重后追加
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
