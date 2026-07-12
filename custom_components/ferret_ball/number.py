"""Numbers: LED and screen brightness sliders."""

from __future__ import annotations

from dataclasses import dataclass

from homeassistant.components.number import NumberEntity, NumberEntityDescription
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import PERCENTAGE
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .entity import FerretEntity
from .hub import FerretHub


@dataclass(frozen=True, kw_only=True)
class FerretNumberDescription(NumberEntityDescription):
    """A brightness slider: state key + WS command prefix."""

    command: str = ""
    state_key: str = ""


NUMBERS: tuple[FerretNumberDescription, ...] = (
    FerretNumberDescription(
        key="led_bright", name="Brilho do LED", icon="mdi:led-on",
        native_min_value=0, native_max_value=100, native_step=5,
        native_unit_of_measurement=PERCENTAGE,
        command="led", state_key="ledBright",
    ),
    FerretNumberDescription(
        key="scr_bright", name="Brilho da tela", icon="mdi:brightness-6",
        native_min_value=20, native_max_value=100, native_step=5,
        native_unit_of_measurement=PERCENTAGE,
        command="scr", state_key="scrBright",
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    hub: FerretHub = entry.runtime_data
    async_add_entities(FerretNumber(hub, d) for d in NUMBERS)


class FerretNumber(FerretEntity, NumberEntity):
    entity_description: FerretNumberDescription

    def __init__(self, hub: FerretHub, description: FerretNumberDescription) -> None:
        super().__init__(hub, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> float | None:
        return self._hub.data.get(self.entity_description.state_key)

    async def async_set_native_value(self, value: float) -> None:
        await self._hub.send(f"{self.entity_description.command}:{round(value)}")
