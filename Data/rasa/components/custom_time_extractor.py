# HH:mm
# hh:mm A/AM/PM
# hh:mm A/AM/PM (with optional seconds)
# ISO 8601 time format with an optional timezone (e.g., "12:34:56+08:00")


import re
import typing
from typing import Any, Dict, List, Text, Type

from rasa.engine.graph import ExecutionContext, GraphComponent
from rasa.engine.recipes.default_recipe import DefaultV1Recipe
from rasa.engine.storage.resource import Resource
from rasa.engine.storage.storage import ModelStorage
from rasa.shared.nlu.constants import ENTITIES, TEXT
from rasa.nlu.extractors.extractor import EntityExtractorMixin
from rasa.shared.nlu.training_data.message import Message

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
            "time_pattern": r'\b(?:(?:2[0-3]|[01]?[0-9]):(?:[0-5]?[0-9])(?::(?:[0-5]?[0-9]))?(?:\s?(?:A|P)M)?(?:[+-](?:2[0-3]|[01]?[0-9]):(?:[0-5]?[0-9]))?)\b'
        }

    def __init__(self, config: Dict[Text, Any]) -> None:
        """Initialize CustomTimeExtractor."""
        self._config = config

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

    def process(self, messages: List[Message]) -> List[Message]:
        """Extract time expressions using regular expressions.

        Args:
            messages: List of messages to process.

        Returns: The processed messages.
        """
        time_pattern = re.compile(self._config["time_pattern"])

        for message in messages:
            text = message.get(TEXT)
            matches = time_pattern.finditer(text)

            extracted_entities = []
            for match in matches:
                start, end = match.span()
                value = match.group()

                entity = {
                    "entity": "time",
                    "value": value,
                    "start": start,
                    "confidence": None,
                    "end": end,
                    "extractor": "CustoTimeExtractor",
                }
                extracted_entities.append(entity)

            message.set(
                ENTITIES, message.get(ENTITIES, []) + extracted_entities, add_to_output=True
            )

        return messages
