#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include "range_link.h"

// Upload the SAME code to both classic ESP32 development boards.
// D2 / GPIO 2 is the controllable LED on the intended 30-pin board.
constexpr uint8_t LED_PIN = 2;
constexpr bool LED_ACTIVE_LOW = false; // Set true for an inverted LED.
constexpr uint8_t WIFI_CHANNEL = 6; // Try 11 on BOTH boards if 6 is congested.
constexpr bool USE_LONG_RANGE = true;
static_assert(WIFI_CHANNEL >= 1 && WIFI_CHANNEL <= 11, "Use channel 1-11");
const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
using range_led::Heartbeat;

struct ReceivedHeartbeat {
  uint8_t senderMac[6];
  Heartbeat packet;
  uint32_t receivedAt;
};

QueueHandle_t receiveQueue = nullptr;
range_led::RangeLink rangeLink;
range_led::HeartbeatSender heartbeatSender;
uint32_t lastStatusAt = 0, transmitAttempts = 0;
bool previouslyInRange = false;

void setLed(bool on) {
  digitalWrite(LED_PIN, (on != LED_ACTIVE_LOW) ? HIGH : LOW);
}

void checkEsp(esp_err_t result, const char *operation) {
  if (result == ESP_OK) return;
  Serial.printf("%s failed: %s\n", operation, esp_err_to_name(result));
  setLed(false);
  while (true) delay(1000);
}

// Wi-Fi callbacks run in another task: queue the data and return quickly.
void receiveHeartbeat(const uint8_t *senderMac, const uint8_t *data, int length) {
  if (!senderMac || !data || length != static_cast<int>(sizeof(Heartbeat))) return;
  ReceivedHeartbeat event{};
  memcpy(event.senderMac, senderMac, sizeof(event.senderMac));
  memcpy(&event.packet, data, sizeof(event.packet));
  const Heartbeat &packet = event.packet;
  if (packet.magic != range_led::kMagic || packet.group != range_led::kGroup) return;
  event.receivedAt = millis();
  xQueueSend(receiveQueue, &event, 0);
}

// Receive callback compatibility with Arduino-ESP32 2.x and 3.x.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (info) receiveHeartbeat(info->src_addr, data, length);
}
#else
void onReceive(const uint8_t *senderMac, const uint8_t *data, int length) {
  receiveHeartbeat(senderMac, data, length);
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  setLed(false);
  delay(500);
  Serial.printf("\nESP-NOW range LED | GPIO %u | reset reason %d\n",
                LED_PIN, static_cast<int>(esp_reset_reason()));
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    Serial.println("Previous reset: supply voltage dropped (brownout).");
  }
  Serial.flush();
  receiveQueue = xQueueCreate(8, sizeof(ReceivedHeartbeat));
  if (!receiveQueue) {
    Serial.println("Could not create receive queue.");
    while (true) delay(1000);
  }
  Serial.println("Starting Wi-Fi radio...");
  Serial.flush();
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("Wi-Fi radio startup failed.");
    while (true) delay(1000);
  }
  checkEsp(esp_wifi_set_ps(WIFI_PS_NONE), "Disable Wi-Fi sleep");
  // Generate the boot identifier with the radio enabled for hardware entropy.
  uint32_t ownSession;
  do { ownSession = esp_random(); } while (ownSession == 0);
  uint8_t ownMac[6];
  checkEsp(esp_wifi_get_mac(WIFI_IF_STA, ownMac), "Read MAC address");
  rangeLink.begin(ownMac, ownSession);
  Serial.println("Wi-Fi radio started; initializing ESP-NOW...");
  Serial.flush();
  checkEsp(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE),
           "Set Wi-Fi channel");
  const uint8_t protocol = USE_LONG_RANGE ? WIFI_PROTOCOL_LR :
      (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  checkEsp(esp_wifi_set_protocol(WIFI_IF_STA, protocol), "Set radio protocol");
  uint8_t activeProtocol = 0, activeChannel = 0;
  wifi_second_chan_t secondaryChannel;
  checkEsp(esp_wifi_get_protocol(WIFI_IF_STA, &activeProtocol), "Read radio protocol");
  checkEsp(esp_wifi_get_channel(&activeChannel, &secondaryChannel), "Read radio channel");
  if (activeProtocol != protocol || activeChannel != WIFI_CHANNEL) {
    checkEsp(ESP_FAIL, "Verify radio configuration");
  }
  checkEsp(esp_now_init(), "ESP-NOW init");
  esp_now_peer_info_t broadcastPeer{};
  memcpy(broadcastPeer.peer_addr, BROADCAST_MAC, sizeof(BROADCAST_MAC));
  broadcastPeer.channel = WIFI_CHANNEL;
  broadcastPeer.ifidx = WIFI_IF_STA;
  broadcastPeer.encrypt = false;
  checkEsp(esp_now_add_peer(&broadcastPeer), "Add broadcast peer");
  // Arduino-ESP32 2.x / ESP-IDF 4.4: explicitly select the LR PHY rate after
  // radio and ESP-NOW initialization, before any packets are transmitted.
  checkEsp(esp_wifi_config_espnow_rate(WIFI_IF_STA,
      USE_LONG_RANGE ? WIFI_PHY_RATE_LORA_250K : WIFI_PHY_RATE_1M_L),
      "Set ESP-NOW PHY rate");
  checkEsp(esp_now_register_recv_cb(onReceive), "Register receive callback");
  Serial.printf("My MAC: %s | Channel: %u\n", WiFi.macAddress().c_str(),
                WIFI_CHANNEL);
  Serial.printf("Radio: %s | PHY rate: %s | Max ACK retries: %u\n",
                USE_LONG_RANGE ? "LR only" : "standard BGN",
                USE_LONG_RANGE ? "250 kbit/s" : "1 Mbit/s",
                range_led::kMaxRetries);
  Serial.println("Waiting for the other board...");
  heartbeatSender.begin(millis(), 100 + (esp_random() % 200));
}

void loop() {
  ReceivedHeartbeat event{};
  // Bounded work keeps LED and transmit timers responsive even with traffic.
  for (unsigned i = 0; i < 8 && xQueueReceive(receiveQueue, &event, 0) == pdTRUE; ++i) {
    rangeLink.receive(event.senderMac, event.packet, event.receivedAt);
  }

  const uint32_t now = millis();
  Heartbeat packet{};
  bool isRetry = false;
  if (heartbeatSender.prepare(now, rangeLink, esp_random(), packet, isRetry)) {
    ++transmitAttempts;
    const esp_err_t result = esp_now_send(
        BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
    if (result == ESP_OK) {
      rangeLink.recordSent(packet, now);
    } else {
      Serial.printf("Send failed: %s\n", esp_err_to_name(result));
    }
  }

  const bool inRange = rangeLink.inRange(now);
  if (inRange != previouslyInRange) {
    previouslyInRange = inRange;
    Serial.println(inRange ? "IN RANGE: D2 LED blinking"
                           : "CONTACT LOST: LED off");
  }
  if (rangeLink.updateLed(now)) setLed(rangeLink.ledOn());
  if (now - lastStatusAt >= 2000) {
    lastStatusAt = now;
    Serial.printf("%s | TX attempts=%lu RETRY=%lu RX=%lu ACK=%lu\n",
                  inRange ? "IN RANGE" : "WAITING / OUT OF RANGE",
                  static_cast<unsigned long>(transmitAttempts),
                  static_cast<unsigned long>(heartbeatSender.retryCount()),
                  static_cast<unsigned long>(rangeLink.receivedCount()),
                  static_cast<unsigned long>(rangeLink.confirmedCount()));
  }
  delay(1);
}
