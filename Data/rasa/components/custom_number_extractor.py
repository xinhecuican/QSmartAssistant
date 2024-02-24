import re
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
class CustomNumberExtractor(GraphComponent, EntityExtractorMixin):
    """Entity extractor which uses regular expressions to find numbers."""

    @staticmethod
    def get_default_config() -> Dict[Text, Any]:
        """The component's default config."""
        return {
            "number_pattern": r'\b\d+'
        }

    def __init__(self, config: Dict[Text, Any]) -> None:
        """Initialize CustomNumberExtractor."""
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
        """Extract numbers using regular expressions.

        Args:
            messages: List of messages to process.

        Returns: The processed messages.
        """
        number_pattern = re.compile(self._config["number_pattern"])

        for message in messages:
            text = message.get(TEXT)
            matches = number_pattern.finditer(text)

            extracted_entities = []
            for match in matches:
                start, end = match.span()
                value = match.group()

                entity = {
                    "entity": "number",
                    "value": value,
                    "start": start,
                    "confidence": 75,
                    "end": end,
                    "extractor": "CustomNumberExtractor",

                }
                extracted_entities.append(entity)

            message.set(
                ENTITIES, message.get(ENTITIES, []) + extracted_entities, add_to_output=True
            )

        return messages
