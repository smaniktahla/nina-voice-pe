from pathlib import Path
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32, microphone, speaker
from esphome.const import CONF_ID, CONF_MICROPHONE, CONF_SPEAKER

DEPENDENCIES = ["esp32", "wifi"]
AUTO_LOAD = ["ring_buffer"]
ns = cg.esphome_ns.namespace("nina_voice")
NinaVoice = ns.class_("NinaVoice", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(NinaVoice),
    cv.Required(CONF_MICROPHONE): microphone.microphone_source_schema(),
    cv.Required("playback_microphone"): microphone.microphone_source_schema(),
    cv.Required(CONF_SPEAKER): cv.use_id(speaker.Speaker),
    cv.Required("service_token"): cv.string_strict,
    cv.Required("certificate"): cv.file_,
    cv.Required("url"): cv.string_strict,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    mic = await microphone.microphone_source_to_code(config[CONF_MICROPHONE])
    cg.add(var.set_microphone(mic))
    echo_mic = await microphone.microphone_source_to_code(config["playback_microphone"], passive=True)
    cg.add(var.set_playback_microphone(echo_mic))
    cg.add(var.set_speaker(await cg.get_variable(config[CONF_SPEAKER])))
    cg.add(var.set_url(config["url"]))
    cg.add(var.set_token(config["service_token"]))
    cg.add(var.set_certificate(Path(config["certificate"]).read_text()))
    esp32.add_idf_component(name="espressif/esp_websocket_client", ref="1.6.1")
    esp32.include_builtin_idf_component("json")
