import re
from typing import Any, Dict, List, Text, Type

from rasa.engine.graph import ExecutionContext, GraphComponent
from rasa.engine.recipes.default_recipe import DefaultV1Recipe
from rasa.engine.storage.resource import Resource
from rasa.engine.storage.storage import ModelStorage
from rasa.shared.nlu.constants import ENTITIES, TEXT
from rasa.nlu.extractors.extractor import EntityExtractorMixin
from rasa.shared.nlu.training_data.message import Message
from recognizers_number import NumberRecognizer
from recognizers_text import Culture
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

    def process(self, messages: List[Message]) -> List[Message]:
        """Extract numbers using regular expressions.

        Args:
            messages: List of messages to process.

        Returns: The processed messages.
        """

        for message in messages:
            text = message.get(TEXT)
            num_result = self.num_model.parse(text)
            percentage_result = self.percentage_model.parse(text)

            extracted_entities = []
            for result in num_result:
                entity = {
                    "entity": "number",
                    "value": result.resolution["value"],
                    "start": result.start,
                    "confidence": 75,
                    "end": result.end,
                    "extractor": "CustomNumberExtractor",
                }
                extracted_entities.append(entity)
            for result in percentage_result:
                entity = {
                    "entity": "number",
                    "value": result.resolution["value"],
                    "start": result.start,
                    "confidence": 75,
                    "end": result.end,
                    "extractor": "CustomNumberExtractor",
                }
                extracted_entities.append(entity)
            entities = message.get(ENTITIES, [])
            exclude_entities = []
            for entity in entities:
                if entity['entity'] != 'number':
                    exclude_entities.append(entity)
            message.set(
                ENTITIES, exclude_entities + extracted_entities, add_to_output=True
            )
        return messages
