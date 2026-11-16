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
CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(MusicRooms), cv.Optional(CONF_LIBRARY, default=""): cv.string}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_library(config[CONF_LIBRARY]))
    await smart_display.register_plugin(var, __file__)
