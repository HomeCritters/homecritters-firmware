"""Media player: HTTP MP3 streaming on the ball's ES8311 speaker.

Plays anything that resolves to an http:// MP3 URL: TTS announcements,
HA media sources (radio browser etc.) and Music Assistant (enable the
"Home Assistant MediaPlayers" provider and pick this entity; set the
player's stream codec to MP3).
"""

from __future__ import annotations

from typing import Any

from homeassistant.components import media_source
from homeassistant.components.media_player import (
    BrowseMedia,
    MediaPlayerDeviceClass,
    MediaPlayerEntity,
    MediaPlayerEntityFeature,
    MediaPlayerState,
    async_process_play_media_url,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .entity import FerretEntity
from .hub import FerretHub


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    hub: FerretHub = entry.runtime_data
    async_add_entities([FerretMediaPlayer(hub)])


class FerretMediaPlayer(FerretEntity, MediaPlayerEntity):
    # name=None -> the entity takes the device name (it IS the device speaker)
    _attr_name = None
    _attr_device_class = MediaPlayerDeviceClass.SPEAKER
    _attr_supported_features = (
        MediaPlayerEntityFeature.PLAY_MEDIA
        | MediaPlayerEntityFeature.STOP
        | MediaPlayerEntityFeature.VOLUME_SET
        | MediaPlayerEntityFeature.BROWSE_MEDIA
    )

    def __init__(self, hub: FerretHub) -> None:
        super().__init__(hub, "media")

    @property
    def state(self) -> MediaPlayerState:
        if self._hub.data.get("media") == "play":
            return MediaPlayerState.PLAYING
        return MediaPlayerState.IDLE

    @property
    def volume_level(self) -> float | None:
        vol = self._hub.data.get("volume")
        return None if vol is None else vol / 100

    async def async_set_volume_level(self, volume: float) -> None:
        await self._hub.send(f"vol:{round(volume * 100)}")

    async def async_media_stop(self) -> None:
        await self._hub.send("media:stop")

    async def async_play_media(
        self, media_type: str, media_id: str, **kwargs: Any
    ) -> None:
        if media_source.is_media_source_id(media_id):
            play_item = await media_source.async_resolve_media(
                self.hass, media_id, self.entity_id
            )
            media_id = play_item.url
        media_id = async_process_play_media_url(self.hass, media_id)
        await self._hub.send(f"media:play:{media_id}")

    async def async_browse_media(
        self,
        media_content_type: str | None = None,
        media_content_id: str | None = None,
    ) -> BrowseMedia:
        return await media_source.async_browse_media(
            self.hass,
            media_content_id,
            content_filter=lambda item: item.media_content_type is not None
            and item.media_content_type.startswith("audio/"),
        )
