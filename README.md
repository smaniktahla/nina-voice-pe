# Nina Voice PE

Experimental ESPHome firmware turning a **Home Assistant Voice Preview Edition** into a direct client for [Nina](https://github.com/smaniktahla/nina-full-duplex-voice-AI).

Audio travels over authenticated WSS, bypassing Home Assistant's Assist pipeline. Nina handles recognition, conversation, synthesis, wake/session lifecycle, and tools.

## Features and status

- Continuous 16 kHz mono microphone streaming, including during replies.
- 24 kHz mono Nina/Kokoro playback through the built-in speaker.
- Hardware microphone mute and volume dial with a temporary LED arc and quiet per-step blips.
- Center button to wake a dormant session or stop speech.
- Status LEDs: amber disconnected, purple dormant, blue awake, red muted.
- Verified TLS, service-token authentication, a bounded 2 MiB response queue, and independent microphone transmission.

Complete responses and dial feedback have been tested on hardware. This is an early integration. Listening range, echo cancellation, channel switching, barge-in, and long-run reconnect behavior still need room testing.

## Tested hardware and dependencies

Voice Preview Edition: ESP32-S3, 16 MB flash, 8 MB PSRAM, XMOS 1.3.1, AIC3204 codec.

- ESPHome 2026.9.1
- ESP-IDF 5.5.5
- Espressif esp_websocket_client 1.6.1
- Upstream Voice PE revision d4e6fa43d6a1a7d342b7d4d403567e06516719de

The example initializes the existing XMOS processor. XMOS firmware update data is deliberately omitted.

## Windows setup

Connect using a data-capable cable and identify the COM port in Device Manager.

    python -m venv .venv
    .\.venv\Scripts\python.exe -m pip install -r requirements.txt
    Copy-Item voice.example.yaml voice.yaml
    Copy-Item secrets.example.yaml secrets.yaml

Edit secrets.yaml with your Nina WebSocket URL and service token. Nina accepts the token in the X-Nina-Service header; see its live/auth.py for the existing token mechanism and configured storage location. No browser login password is required.

Copy the server's **public certificate** here as nina-server-cert.pem. Its subject alternative name must match the hostname or IP in nina_url. Do not copy the private key.

The local voice.yaml, secrets.yaml, certificates, firmware, backups, and logs are ignored. Generated firmware contains the token and must remain private.

The example uses previously provisioned Wi-Fi settings in NVS. For a fresh device, add Wi-Fi settings to your local voice.yaml using ESPHome secrets. NTP must synchronize before verified WSS starts.

## Back up before flashing

Close serial monitors and substitute the actual COM port:

    .\.venv\Scripts\python.exe -m esptool --port COM3 flash-id
    .\.venv\Scripts\python.exe -m esptool --port COM3 read-flash 0 0x1000000 voice-pe-original.bin
    Get-FileHash .\voice-pe-original.bin -Algorithm SHA256

Confirm 16 MB flash first. The backup should contain 16,777,216 bytes. It contains private provisioned settings and covers the ESP32, not XMOS.

The supplied partition table matches the tested factory layout, including NVS at 0xf90000. Verify it against your own backup before replacing firmware; other hardware/revisions are unvalidated.

## Validate, compile, and install

    .\.venv\Scripts\python.exe -m esphome config voice.yaml
    .\.venv\Scripts\python.exe -m esphome compile voice.yaml
    .\.venv\Scripts\python.exe -m esphome upload voice.yaml --device COM3

A short local esphome.build_path can help with Windows path-length issues. Keep local configuration untracked.

Stock HA Assist, media-player features, and vendor OTA are absent from this minimal build.

## Diagnostics

    .\.venv\Scripts\python.exe capture_serial.py --port COM3 --seconds 90 --output device.log

Use --reset only when intentionally capturing a fresh boot. Passive capture keeps DTR inactive. Close serial monitors before flashing.

Logs report microphone, received PCM, speaker bytes, queue length, drops, server duck/cancel events, and queue overflow. Counters alone cannot verify acoustic quality.

## Audio design

Wake/normal listening uses XMOS channel 0 with AGC and gain 1. During playback and its 400 ms echo tail, transmission selects channel 1 (AEC/noise suppression without final AGC) at fixed gain 16. Both share the microphone hardware; only one source is transmitted at a time.

Nina binary audio contains a four-byte little-endian sample rate followed by signed 16-bit mono PCM. Playback currently supports **24 kHz only**. Samples are duplicated into 48 kHz stereo, 32-bit I2S; filtered resampling is future work.

Server cancel flushes playback; duck lowers playback gain. The 2 MiB queue holds about 43 seconds of speech. Larger responses may still overflow and are logged explicitly.

## Restore

Close monitors and use your own verified full-flash backup:

    .\.venv\Scripts\python.exe -m esptool --port COM3 write-flash 0 voice-pe-original.bin

A whole-chip erase is unnecessary. The tested factory bootloader lacked automatic OTA rollback, so retain the backup and USB access.

## Attribution

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for upstream configuration and dependency notices.
