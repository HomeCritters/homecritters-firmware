"""Base entity for the Ferret Ball integration."""

from __future__ import annotations

from homeassistant.core import callback
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity import Entity

from .const import DOMAIN
from .hub import FerretHub


class FerretEntity(Entity):
    """Push-based entity fed by the hub's WS state updates."""

    _attr_should_poll = False
    _attr_has_entity_name = True

    def __init__(self, hub: FerretHub, key: str) -> None:
        self._hub = hub
        self._attr_unique_id = f"{hub.mac}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, hub.mac)},
            name=hub.name,
            manufacturer="BrunoTCouto",
            model="Ferret Ball V2",
            sw_version=hub.fw,
            configuration_url=f"http://{hub.host}/",
        )

    @property
    def available(self) -> bool:
        return self._hub.available

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(self._hub.subscribe(self._on_update))

    @callback
    def _on_update(self) -> None:
        self.async_write_ha_state()
