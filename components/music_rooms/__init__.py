"""My plugin: the ESPHome side. This file stays this short: smart_display.register_plugin() reads the id, version,
tile memory and the screen's texts from the manifest and translations/ beside this folder (docs/FIRMWARE_API.md)."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

# The core component every Tessera screen has; the plugin API lives in it.
DEPENDENCIES = ["smart_display"]

music_rooms_ns = cg.esphome_ns.namespace("music_rooms")
MusicRooms = music_rooms_ns.class_("MusicRooms", cg.Component)

# The Music Assistant entry every tile of the screen browses (the input `library`), unless a tile names its own.
CONF_LIBRARY = "library"
# A Home Assistant script that plays an item whose address the app cut (a Spotify playlist's) exactly: it searches by
# name and plays the result whose address starts as the cut one (scripts/music_rooms_play.yaml). Empty: by name.
CONF_PLAY_SCRIPT = "play_script"
# A Home Assistant script that joins rooms to the lead of a player's group (scripts/music_rooms_join.yaml): Music
# Assistant ignores a join to a member of another player's group. Empty: join to the player itself.
CONF_JOIN_SCRIPT = "join_script"
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MusicRooms),
        cv.Optional(CONF_LIBRARY, default=""): cv.string,
        cv.Optional(CONF_PLAY_SCRIPT, default=""): cv.string,
        cv.Optional(CONF_JOIN_SCRIPT, default=""): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_library(config[CONF_LIBRARY]))
    cg.add(var.set_play_script(config[CONF_PLAY_SCRIPT]))
    cg.add(var.set_join_script(config[CONF_JOIN_SCRIPT]))
    await smart_display.register_plugin(var, __file__)
