"""Config flow for Ferret Ball (manual host or zeroconf discovery)."""

from __future__ import annotations

import asyncio
from typing import Any

import aiohttp
import voluptuous as vol

from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_HOST
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.device_registry import format_mac

try:  # HA 2025.1+ location, with a fallback for slightly older cores
    from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo
except ImportError:  # pragma: no cover
    from homeassistant.components.zeroconf import ZeroconfServiceInfo

from .const import CONF_FW, CONF_MAC, CONF_NAME, DOMAIN


class FerretBallConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle the config flow."""

    VERSION = 1

    def __init__(self) -> None:
        self._host: str | None = None
        self._info: dict[str, Any] | None = None

    async def _fetch_info(self, host: str) -> dict[str, Any]:
        """GET /info from the device (identity: name, mac, fw)."""
        session = async_get_clientsession(self.hass)
        async with asyncio.timeout(10):
            resp = await session.get(f"http://{host}/info")
            resp.raise_for_status()
            return await resp.json(content_type=None)

    def _entry_data(self, host: str, info: dict[str, Any]) -> dict[str, Any]:
        return {
            CONF_HOST: host,
            CONF_MAC: format_mac(info["mac"]),
            CONF_NAME: info.get("name", "Ferret Ball"),
            CONF_FW: info.get("fw", "unknown"),
        }

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            try:
                info = await self._fetch_info(host)
            except (aiohttp.ClientError, TimeoutError, KeyError, ValueError):
                errors["base"] = "cannot_connect"
            else:
                await self.async_set_unique_id(format_mac(info["mac"]))
                self._abort_if_unique_id_configured(updates={CONF_HOST: host})
                return self.async_create_entry(
                    title=info.get("name", "Ferret Ball"),
                    data=self._entry_data(host, info),
                )
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Required(CONF_HOST): str}),
            errors=errors,
        )

    async def async_step_zeroconf(
        self, discovery_info: ZeroconfServiceInfo
    ) -> ConfigFlowResult:
        host = discovery_info.host
        try:
            info = await self._fetch_info(host)
        except (aiohttp.ClientError, TimeoutError, KeyError, ValueError):
            return self.async_abort(reason="cannot_connect")
        await self.async_set_unique_id(format_mac(info["mac"]))
        self._abort_if_unique_id_configured(updates={CONF_HOST: host})
        self._host = host
        self._info = info
        self.context["title_placeholders"] = {"name": info.get("name", "Ferret Ball")}
        return await self.async_step_confirm()

    async def async_step_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        assert self._host is not None and self._info is not None
        if user_input is not None:
            return self.async_create_entry(
                title=self._info.get("name", "Ferret Ball"),
                data=self._entry_data(self._host, self._info),
            )
        return self.async_show_form(
            step_id="confirm",
            description_placeholders={"name": self._info.get("name", "Ferret Ball")},
        )
