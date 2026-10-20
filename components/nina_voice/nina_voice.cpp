#include "nina_voice.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"
#include "cJSON.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <cmath>

namespace esphome::nina_voice {
static const char *const TAG = "nina_voice";

void NinaVoice::setup() {
  tx_ = ring_buffer::RingBuffer::create(32000);
  rx_ = ring_buffer::RingBuffer::create(2 * 1024 * 1024);
  rx_mutex_ = xSemaphoreCreateMutex();
  if (!tx_ || !rx_ || !rx_mutex_) { mark_failed(); return; }
  speaker_->set_audio_stream_info(audio::AudioStreamInfo(32, 2, 48000));
  speaker_->set_volume(volume_);
  mic_->add_data_callback([this](const std::vector<uint8_t> &data) {
    if (connected_ && !muted_ && !using_playback_mic()) {
      const size_t n = tx_->write_without_replacement(data.data(), data.size(), 0, false);
      dropped_bytes_ += data.size() - n;
    }
  });
  playback_mic_->add_data_callback([this](const std::vector<uint8_t> &data) {
    if (connected_ && !muted_ && using_playback_mic()) {
      const size_t n = tx_->write_without_replacement(data.data(), data.size(), 0, false);
      dropped_bytes_ += data.size() - n;
    }
  });
  start_time_ = millis();
  if (xTaskCreate(microphone_task, "nina_mic_tx", 4096, this, 3, &microphone_task_) != pdPASS)
    mark_failed();
}

void NinaVoice::microphone_task(void *arg) {
  auto *self = static_cast<NinaVoice *>(arg);
  uint8_t packet[1280];
  for (;;) {
    if (!self->connected_ || self->muted_) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (self->tx_->available() < sizeof(packet)) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    size_t n = self->tx_->read(packet, sizeof(packet));
    if (!self->connected_ || self->muted_) continue;
    int sent = esp_websocket_client_send_bin(self->client_, reinterpret_cast<const char *>(packet), n,
                                             pdMS_TO_TICKS(1000));
    if (sent > 0) self->input_bytes_ += sent;
    if (sent < static_cast<int>(n)) self->dropped_bytes_ += n - std::max(sent, 0);
  }
}

void NinaVoice::dump_config() {
  ESP_LOGCONFIG(TAG, "Nina endpoint: %s; verified TLS; mic=16k mono; playback=24k mono -> 48k stereo", url_.c_str());
}

void NinaVoice::event(void *arg, esp_event_base_t, int32_t id, void *data) {
  static_cast<NinaVoice *>(arg)->on_event(id, static_cast<esp_websocket_event_data_t *>(data));
}

void NinaVoice::on_event(int32_t id, esp_websocket_event_data_t *data) {
  if (id == WEBSOCKET_EVENT_CONNECTED) {
    connected_ = true;
    ESP_LOGI(TAG, "Authenticated WSS connected");
    return;
  }
  if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED) {
    connected_ = false; cancel_ = true; awake_ = false;
    ESP_LOGW(TAG, "WSS disconnected; waiting for reconnect");
    return;
  }
  if (id != WEBSOCKET_EVENT_DATA || !data || data->data_len == 0) return;
  if (data->op_code >= 8) return;  // Ping/pong/close must not disturb fragmented PCM.
  // Track fragments and transport buffer subdivisions independently.
  if (data->payload_offset == 0 && data->op_code != 0) {
    message_opcode_ = data->op_code;
    message_bytes_ = 0;
    text_.clear();
    accept_audio_ = true;
  }
  if (message_opcode_ == 1) {
    if (text_.size() + data->data_len <= 8192) text_.append(data->data_ptr, data->data_len);
    else { text_.clear(); message_opcode_ = 0xff; }
    if (data->fin && data->payload_offset + data->data_len == data->payload_len) text_message();
  } else if (message_opcode_ == 2) {
    size_t consumed = 0;
    while (message_bytes_ < 4 && consumed < static_cast<size_t>(data->data_len)) {
      rate_header_[message_bytes_++] = data->data_ptr[consumed++];
    }
    if (message_bytes_ == 4) {
      const uint32_t rate = uint32_t(rate_header_[0]) | uint32_t(rate_header_[1]) << 8 |
                            uint32_t(rate_header_[2]) << 16 | uint32_t(rate_header_[3]) << 24;
      if (rate != 24000) {
        accept_audio_ = false;
        ESP_LOGE(TAG, "Unsupported playback sample rate: %u", static_cast<unsigned>(rate));
      }
    }
    size_t length = data->data_len - consumed;
    if (length && accept_audio_) {
      xSemaphoreTake(rx_mutex_, portMAX_DELAY);
      size_t written = rx_->write_without_replacement(data->data_ptr + consumed, length, 0, false);
      xSemaphoreGive(rx_mutex_);
      if (written != length) {
        dropped_bytes_ += length; accept_audio_ = false; cancel_ = true;
        ESP_LOGE(TAG, "Playback queue overflow: incoming speech exceeds the 2 MiB queue");
      }
      else { output_bytes_ += length; far_end_until_ = millis() + 400; }
    }
    message_bytes_ += length;
  }
}

void NinaVoice::text_message() {
  cJSON *root = cJSON_Parse(text_.c_str());
  if (!root) return;
  cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
  if (cJSON_IsString(type)) {
    if (strcmp(type->valuestring, "cancel") == 0) {
      cancel_ = true;
      ESP_LOGI(TAG, "Server cancelled playback");
    }
    else if (strcmp(type->valuestring, "duck") == 0) {
      duck_ = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "on"));
      ESP_LOGI(TAG, "Server duck: %s", duck_ ? "on" : "off");
    }
    else if (strcmp(type->valuestring, "mgr") == 0) {
      auto *state = cJSON_GetObjectItemCaseSensitive(root, "state");
      if (cJSON_IsString(state)) {
        awake_ = strcmp(state->valuestring, "ACTIVE") == 0 || strcmp(state->valuestring, "IDLE") == 0;
        ESP_LOGI(TAG, "Session state: %s", state->valuestring);
      }
    } else if (strcmp(type->valuestring, "hello") == 0) ESP_LOGI(TAG, "Nina hello received");
  }
  cJSON_Delete(root);
}

void NinaVoice::clear_playback() {
  xSemaphoreTake(rx_mutex_, portMAX_DELAY);
  rx_->reset();
  xSemaphoreGive(rx_mutex_);
  pending_ = offset_ = 0;
  speaker_->stop();
  duck_ = false;
}

void NinaVoice::button() {
  if (!connected_) return;
  const char *message = awake_ ? "{\"t\":\"stop\"}" : "{\"t\":\"wake\"}";
  if (awake_) clear_playback();
  esp_websocket_client_send_text(client_, message, strlen(message), pdMS_TO_TICKS(20));
}

void NinaVoice::volume_step(float delta) {
  volume_ = std::clamp(volume_ + delta, 0.0f, 0.70f);
  speaker_->set_volume(volume_);
  volume_changed_ = millis();
  blip_left_ = 240;  // 10 ms, with an envelope to avoid a harsh click.
}

void NinaVoice::loop() {
  // Wait for time sync so the certificate's validity period is checked.
  if (!client_ && wifi::global_wifi_component->is_connected() && std::time(nullptr) > 1700000000) {
    esp_websocket_client_config_t cfg{};
    cfg.uri = url_.c_str(); cfg.headers = headers_.c_str();
    cfg.cert_pem = certificate_.c_str();
    cfg.task_stack = 6144; cfg.buffer_size = 4096;
    cfg.reconnect_timeout_ms = 5000; cfg.network_timeout_ms = 5000;
    cfg.enable_close_reconnect = true;
    cfg.user_agent = "Nina-Voice-PE/0.1";
    client_ = esp_websocket_client_init(&cfg);
    if (!client_) { mark_failed(); return; }
    esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY, event, this);
    esp_websocket_client_start(client_);
  }
  if (cancel_.exchange(false)) clear_playback();
  if (connected_ && !muted_) {
    if (!mic_->is_running()) mic_->start();
  } else {
    if (mic_->is_running()) mic_->stop();
    tx_->reset();
  }
  // Keep the audio queue fed independently of the ordinary ~16 ms ESPHome loop.
  // One 512-sample block lasts only 21 ms; network writes can otherwise starve it.
  const bool playing = pending_ || rx_->available() || blip_left_ || speaker_->has_buffered_data();
  if (playing) far_end_until_ = millis() + 400;
  if (playing) playback_loop_.start();
  else playback_loop_.stop();
  for (unsigned batch = 0; batch < 4; ++batch) {
  if (pending_ == 0 && (rx_->available() >= 2 || blip_left_)) {
    int16_t samples[512];
    xSemaphoreTake(rx_mutex_, portMAX_DELAY);
    size_t n = rx_->read(samples, std::min<size_t>(sizeof(samples), rx_->available() & ~size_t(1)));
    xSemaphoreGive(rx_mutex_);
    size_t frames = n / 2;
    if (frames == 0) {
      frames = std::min<size_t>(512, blip_left_);
      std::fill_n(samples, frames, 0);
    }
    // Nina currently supplies 24 kHz. Duplicate each sample twice, into both 32-bit channels.
    // Gain ducking is applied on playback, without modifying the queued original PCM.
    const int32_t gain = duck_ ? 8192 : 65536;
    for (size_t i = 0; i < frames; ++i) {
      int32_t pcm = samples[i];
      if (blip_left_) {
        const float phase = (240 - blip_left_) / 240.0f;
        pcm += int32_t(1800.0f * std::sin(2.0f * 3.14159265f * 1600.0f * (240 - blip_left_) / 24000.0f)
                      * std::sin(3.14159265f * phase));
        --blip_left_;
      }
      int32_t sample = std::clamp(pcm, int32_t(-32768), int32_t(32767)) * gain;
      for (size_t j = 0; j < 4; ++j) play_buffer_[4 * i + j] = sample;
    }
    pending_ = frames * 16; offset_ = 0;
  }
  if (pending_) {
    size_t n = speaker_->play(reinterpret_cast<uint8_t *>(play_buffer_) + offset_, pending_, 0);
    offset_ += n; pending_ -= n;
    speaker_bytes_ += n;
    if (pending_) break;  // Speaker queue is full; don't spin here.
  }
  if (pending_ == 0 && rx_->available() < 2 && !blip_left_) break;
  }
  if (millis() - last_stats_ >= 10000) {
    ESP_LOGI(TAG, "connected=%d muted=%d mic_bytes=%u playback_bytes=%u speaker_bytes=%u dropped=%u queued=%u",
             bool(connected_), bool(muted_), unsigned(input_bytes_), unsigned(output_bytes_),
             unsigned(speaker_bytes_), unsigned(dropped_bytes_), unsigned(rx_->available()));
    last_stats_ = millis();
  }
}
}
