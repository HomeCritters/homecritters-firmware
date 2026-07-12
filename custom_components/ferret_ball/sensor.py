"""Sensors: the pet's stats, mood, battery and current screen."""

from __future__ import annotations

from dataclasses import dataclass

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import PERCENTAGE
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .entity import FerretEntity
from .hub import FerretHub


@dataclass(frozen=True, kw_only=True)
class FerretSensorDescription(SensorEntityDescription):
    """Describes a Ferret Ball sensor (state JSON key = description key)."""

    round_value: bool = False


SENSORS: tuple[FerretSensorDescription, ...] = (
    FerretSensorDescription(
        key="hunger", name="Fome", icon="mdi:food-apple",
        native_unit_of_measurement=PERCENTAGE, round_value=True,
    ),
    FerretSensorDescription(
        key="energy", name="Energia", icon="mdi:lightning-bolt",
        native_unit_of_measurement=PERCENTAGE, round_value=True,
    ),
    FerretSensorDescription(
        key="joy", name="Alegria", icon="mdi:emoticon-happy",
        native_unit_of_measurement=PERCENTAGE, round_value=True,
    ),
    FerretSensorDescription(
        key="hygiene", name="Higiene", icon="mdi:shower",
        native_unit_of_measurement=PERCENTAGE, round_value=True,
    ),
    FerretSensorDescription(
        key="battery", name="Bateria",
        device_class=SensorDeviceClass.BATTERY,
        native_unit_of_measurement=PERCENTAGE,
    ),
    FerretSensorDescription(key="mood", name="Humor", icon="mdi:emoticon"),
    FerretSensorDescription(key="screen", name="Tela", icon="mdi:monitor"),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    hub: FerretHub = entry.runtime_data
    async_add_entities(FerretSensor(hub, d) for d in SENSORS)


class FerretSensor(FerretEntity, SensorEntity):
    entity_description: FerretSensorDescription

    def __init__(self, hub: FerretHub, description: FerretSensorDescription) -> None:
        super().__init__(hub, description.key)
        self.entity_description = description

    @property
    def native_value(self):
        value = self._hub.data.get(self.entity_description.key)
        if value is None:
            return None
        if self.entity_description.round_value:
            return round(float(value))
        return value
