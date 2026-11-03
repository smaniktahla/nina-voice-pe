#pragma once
#include "esphome/core/component.h"
#include "esphome/components/microphone/microphone_source.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/ring_buffer/ring_buffer.h"
#include "esp_websocket_client.h"
#include "status_ring.h"
#include "volume_setting.h"
#include "ready_chime.h"
#include "esphome/core/preferences.h"
#include <atomic>
#include <string>

namespace esphome::nina_voice {
class NinaVoice : public Component {
 public:
  void set_microphone(microphone::MicrophoneSource *v) { mic_ = v; }
  void set_playback_microphone(microphone::MicrophoneSource *v) { playback_mic_ = v; }
  void set_speaker(speaker::Speaker *v) { speaker_ = v; }
  void set_url(const std::string &v) { url_ = v; }
  void set_token(const std::string &v) { headers_ = "X-Nina-Service: " + v + "\r\n"; }
  void set_certificate(const std::string &v) { certificate_ = v; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override { save_volume_(); }
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void set_muted(bool v) { muted_ = v; }
  void button();
  void volume_step(float delta);
  bool connected() const { return connected_; }
  bool ready() const { return connected_ && hello_received_ && session_received_ && !is_failed(); }
  RingFrame status_frame();
  bool muted() const { return muted_; }
  bool awake() const { return awake_; }
  bool showing_volume() const { return volume_changed_ != 0 && millis() - volume_changed_ < 1500; }
  float volume_fraction() const { return volume_setting_.fraction(); }
 protected:
  static void event(void *arg, esp_event_base_t base, int32_t id, void *data);
  void on_event(int32_t id, esp_websocket_event_data_t *data);
  void text_message();
  void clear_playback();
  void save_volume_();
  static void microphone_task(void *arg);
  TaskHandle_t microphone_task_{nullptr};
  microphone::MicrophoneSource *mic_{nullptr};
  microphone::MicrophoneSource *playback_mic_{nullptr};
  std::atomic<uint32_t> far_end_until_{0};
  bool using_playback_mic() const { return int32_t(far_end_until_.load() - millis()) > 0; }
  speaker::Speaker *speaker_{nullptr};
  esp_websocket_client_handle_t client_{nullptr};
  std::unique_ptr<ring_buffer::RingBuffer> tx_, rx_;
  SemaphoreHandle_t rx_mutex_{nullptr};
  std::string url_, headers_, certificate_, text_;
  std::atomic<bool> connected_{false}, muted_{false}, cancel_{false}, duck_{false}, awake_{false};
  std::atomic<bool> hello_received_{false}, session_received_{false}, wake_flash_{false}, ready_chime_requested_{false};
  StatusRing status_ring_;
  ReadyChime ready_chime_;
  std::atomic<uint32_t> input_bytes_{0}, output_bytes_{0}, dropped_bytes_{0};
  uint32_t last_stats_{0}, last_tx_{0}, start_time_{0};
  uint8_t rate_header_[4]{};
  bool accept_audio_{false};
  uint8_t message_opcode_{0};
  uint32_t message_bytes_{0};
  int32_t play_buffer_[2048]{};
  size_t pending_{0}, offset_{0};
  VolumeSetting volume_setting_;
  ESPPreferenceObject volume_preference_;
  uint32_t volume_changed_{0};
  uint32_t speaker_bytes_{0};
  uint16_t blip_left_{0};
  HighFrequencyLoopRequester playback_loop_;
};
}
