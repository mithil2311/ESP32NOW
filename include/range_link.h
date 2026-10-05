#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace range_led {

constexpr uint32_t kMagic = 0x45534E31;
constexpr uint32_t kGroup = 0x32B11A01; // Keep identical on the two boards.
constexpr uint32_t kTimeoutMs = 3000;
constexpr uint32_t kBlinkMs = 250;
constexpr uint32_t kHeartbeatMinMs = 400;
constexpr uint32_t kHeartbeatJitterMs = 200;
constexpr uint32_t kRetryBaseMs = 300;
constexpr uint32_t kRetryJitterMs = 100;
constexpr uint8_t kMaxRetries = 2;

// Same 24-byte wire format as the original firmware.
struct Heartbeat {
  uint32_t magic;
  uint32_t group;
  uint32_t session;
  uint32_t sequence;
  uint32_t acknowledgedSession;
  uint32_t acknowledgedSequence;
};
static_assert(sizeof(Heartbeat) == 24, "Heartbeat format changed");

// Radio-independent contact logic, used by both firmware and host tests.
class RangeLink {
 public:
  void begin(const uint8_t ownMac[6], uint32_t session,
             uint32_t initialSequence = 0) {
    *this = RangeLink{};
    std::memcpy(ownMac_, ownMac, 6);
    session_ = session;
    sequence_ = initialSequence;
  }

  Heartbeat nextHeartbeat(uint32_t now) {
    if (++sequence_ == 0) ++sequence_; // Zero means "no acknowledgement".
    Heartbeat packet{kMagic, kGroup, session_, sequence_, 0, 0};
    refreshAcknowledgement(packet, now);
    return packet;
  }

  // A retry keeps the same probe sequence while carrying our latest reply.
  void refreshAcknowledgement(Heartbeat &packet, uint32_t now) const {
    const bool recentPeer = havePeer_ && now - lastReceivedAt_ < kTimeoutMs;
    packet.acknowledgedSession = recentPeer ? peerSession_ : 0;
    packet.acknowledgedSequence = recentPeer ? peerSequence_ : 0;
  }

  // Call only when esp_now_send accepts the packet. Acceptance alone does
  // not turn on the LED; a fresh reply from the other board is required.
  void recordSent(const Heartbeat &packet, uint32_t now) {
    for (Sent &sent : sent_) {
      if (sent.sequence == packet.sequence) {
        sent.at = now; // Keep retries from consuming the unique-probe history.
        return;
      }
    }
    sent_[sentIndex_] = {packet.sequence, now};
    sentIndex_ = (sentIndex_ + 1) % kHistorySize;
  }

  bool receive(const uint8_t senderMac[6], const Heartbeat &packet,
               uint32_t receivedAt) {
    if (packet.magic != kMagic || packet.group != kGroup ||
        packet.session == 0 || packet.sequence == 0 ||
        std::memcmp(senderMac, ownMac_, 6) == 0) return false;

    const bool samePeer = havePeer_ &&
                          std::memcmp(senderMac, peerMac_, 6) == 0;
    // Stay with the discovered board while it is sending valid heartbeats.
    if (havePeer_ && !samePeer &&
        receivedAt - lastReceivedAt_ < kTimeoutMs) return false;

    bool freshHeartbeat = true;
    if (!samePeer || packet.session != peerSession_) {
      std::memcpy(peerMac_, senderMac, 6);
      peerSession_ = packet.session;
      havePeer_ = true;
      haveConfirmation_ = false;
      haveConfirmedSequence_ = false;
    } else if (!isNewer(packet.sequence, peerSequence_)) {
      if (packet.sequence != peerSequence_) return false; // Reordered packet.
      freshHeartbeat = false;
      // A retransmission can carry a NEW acknowledgement even though its
      // probe sequence is unchanged. Its heartbeat is not counted twice.
    }
    if (freshHeartbeat) {
      peerSequence_ = packet.sequence;
      lastReceivedAt_ = receivedAt;
      ++receivedCount_;
    }

    if (packet.acknowledgedSession != session_ ||
        packet.acknowledgedSequence == 0 ||
        (haveConfirmedSequence_ &&
         !isNewer(packet.acknowledgedSequence, lastConfirmedSequence_))) {
      return freshHeartbeat;
    }
    for (const Sent &sent : sent_) {
      if (sent.sequence != 0 &&
          sent.sequence == packet.acknowledgedSequence &&
          receivedAt - sent.at < kTimeoutMs) {
        lastConfirmedSequence_ = sent.sequence;
        haveConfirmedSequence_ = true;
        lastConfirmedAt_ = receivedAt;
        haveConfirmation_ = true;
        ++confirmedCount_;
        break;
      }
    }
    return freshHeartbeat;
  }

  bool inRange(uint32_t now) const {
    return haveConfirmation_ && now - lastConfirmedAt_ < kTimeoutMs;
  }

  // Returns true if GPIO output should be refreshed. LEDs on the two boards
  // blink independently; no delay blocks sending or receiving heartbeats.
  bool updateLed(uint32_t now) {
    const bool contact = inRange(now);
    if (contact != blinking_) {
      blinking_ = contact;
      ledOn_ = contact;
      lastToggleAt_ = now;
      return true;
    }
    if (blinking_ && now - lastToggleAt_ >= kBlinkMs) {
      const uint32_t toggles = (now - lastToggleAt_) / kBlinkMs;
      lastToggleAt_ += toggles * kBlinkMs;
      if (toggles % 2 != 0) ledOn_ = !ledOn_;
      return true;
    }
    return false;
  }

  bool ledOn() const { return ledOn_; }
  uint32_t receivedCount() const { return receivedCount_; }
  uint32_t confirmedCount() const { return confirmedCount_; }
  bool acknowledged(uint32_t sequence) const {
    return haveConfirmedSequence_ && lastConfirmedSequence_ == sequence;
  }

 private:
  static bool isNewer(uint32_t candidate, uint32_t previous) {
    const uint32_t difference = candidate - previous;
    return difference != 0 && difference < 0x80000000U;
  }
  struct Sent { uint32_t sequence; uint32_t at; };
  static constexpr size_t kHistorySize = 8;
  Sent sent_[kHistorySize]{};
  size_t sentIndex_ = 0;
  uint8_t ownMac_[6]{}, peerMac_[6]{};
  uint32_t session_ = 0, sequence_ = 0;
  uint32_t peerSession_ = 0, peerSequence_ = 0, lastReceivedAt_ = 0;
  uint32_t lastConfirmedSequence_ = 0, lastConfirmedAt_ = 0;
  uint32_t receivedCount_ = 0, confirmedCount_ = 0, lastToggleAt_ = 0;
  bool havePeer_ = false, haveConfirmation_ = false;
  bool haveConfirmedSequence_ = false, blinking_ = false, ledOn_ = false;
};

// One outstanding heartbeat at a time, with bounded nonblocking ACK retries.
// This is tested on the host and used unchanged by the ESP32 firmware.
class HeartbeatSender {
 public:
  void begin(uint32_t now, uint32_t startupDelay) {
    *this = HeartbeatSender{};
    cycleStartedAt_ = now;
    cycleInterval_ = startupDelay;
  }

  bool prepare(uint32_t now, RangeLink &link, uint32_t random,
               Heartbeat &packet, bool &isRetry) {
    if (pending_) {
      if (link.acknowledged(pendingPacket_.sequence)) {
        pending_ = false;
      } else {
        if (now - lastAttemptAt_ < retryWait_) return false;
        if (retries_ < kMaxRetries) {
          ++retries_;
          ++retryCount_;
          lastAttemptAt_ = now;
          retryWait_ = kRetryBaseMs + random % (kRetryJitterMs + 1);
          link.refreshAcknowledgement(pendingPacket_, now);
          packet = pendingPacket_;
          isRetry = true;
          return true;
        }
        pending_ = false; // Retry budget exhausted; discovery will continue.
      }
    }
    if (now - cycleStartedAt_ < cycleInterval_) return false;
    cycleStartedAt_ = now;
    cycleInterval_ = kHeartbeatMinMs + random % (kHeartbeatJitterMs + 1);
    pendingPacket_ = link.nextHeartbeat(now);
    pending_ = true;
    retries_ = 0;
    lastAttemptAt_ = now;
    retryWait_ = kRetryBaseMs + (random >> 8) % (kRetryJitterMs + 1);
    packet = pendingPacket_;
    isRetry = false;
    return true;
  }

  uint32_t retryCount() const { return retryCount_; }

 private:
  Heartbeat pendingPacket_{};
  uint32_t cycleStartedAt_ = 0, cycleInterval_ = 0;
  uint32_t lastAttemptAt_ = 0, retryWait_ = 0, retryCount_ = 0;
  uint8_t retries_ = 0;
  bool pending_ = false;
};

} // namespace range_led
