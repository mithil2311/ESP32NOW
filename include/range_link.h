#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace range_led {

constexpr uint32_t kMagic = 0x45534E31;
constexpr uint32_t kGroup = 0x32B11A01; // Keep identical on the two boards.
constexpr uint32_t kTimeoutMs = 3000;
constexpr uint32_t kBlinkMs = 250;

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
    const bool recentPeer = havePeer_ && now - lastReceivedAt_ < kTimeoutMs;
    return {kMagic, kGroup, session_, sequence_,
            recentPeer ? peerSession_ : 0, recentPeer ? peerSequence_ : 0};
  }

  // Call only when esp_now_send accepts the packet. Acceptance alone does
  // not turn on the LED; a fresh reply from the other board is required.
  void recordSent(const Heartbeat &packet, uint32_t now) {
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

    if (!samePeer || packet.session != peerSession_) {
      std::memcpy(peerMac_, senderMac, 6);
      peerSession_ = packet.session;
      havePeer_ = true;
      haveConfirmation_ = false;
      haveConfirmedSequence_ = false;
    } else if (!isNewer(packet.sequence, peerSequence_)) {
      return false; // Duplicate or reordered packet: no fresh contact.
    }
    peerSequence_ = packet.sequence;
    lastReceivedAt_ = receivedAt;
    ++receivedCount_;

    if (packet.acknowledgedSession != session_ ||
        packet.acknowledgedSequence == 0 ||
        (haveConfirmedSequence_ &&
         !isNewer(packet.acknowledgedSequence, lastConfirmedSequence_))) {
      return true;
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
    return true;
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

} // namespace range_led
