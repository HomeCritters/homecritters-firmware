"""Switches: sleep (tucks the pet in) and the idle clock mode."""

from __future__ import annotations

from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .entity import FerretEntity
from .hub import FerretHub


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    hub: FerretHub = entry.runtime_data
    async_add_entities([FerretSleepSwitch(hub), FerretClockSwitch(hub)])


class FerretSleepSwitch(FerretEntity, SwitchEntity):
    _attr_name = "Dormir"
    _attr_icon = "mdi:sleep"

    def __init__(self, hub: FerretHub) -> None:
        super().__init__(hub, "sleep")

    @property
    def is_on(self) -> bool:
        return bool(self._hub.data.get("sleeping"))

    async def async_turn_on(self, **kwargs) -> None:
        if not self.is_on:  # the firmware command is a toggle
            await self._hub.send("sleep")

    async def async_turn_off(self, **kwargs) -> None:
        if self.is_on:
            await self._hub.send("sleep")


class FerretClockSwitch(FerretEntity, SwitchEntity):
    _attr_name = "Relógio ocioso"
    _attr_icon = "mdi:clock-outline"

    def __init__(self, hub: FerretHub) -> None:
        super().__init__(hub, "clock")

    @property
    def is_on(self) -> bool:
        return bool(self._hub.data.get("clockOn"))

    async def async_turn_on(self, **kwargs) -> None:
        await self._hub.send("clock:on")

    async def async_turn_off(self, **kwargs) -> None:
        await self._hub.send("clock:off")
