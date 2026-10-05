#include "range_link.h"

#include <array>
#include <iostream>
#include <stdexcept>

using range_led::Heartbeat;
using range_led::RangeLink;
using range_led::kTimeoutMs;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

struct Node {
  std::array<uint8_t, 6> mac;
  RangeLink link;
  Node(uint8_t id, uint32_t session, uint32_t initialSequence = 0)
      : mac{{0x02, 0x00, 0x00, 0x00, 0x00, id}} {
    link.begin(mac.data(), session, initialSequence);
  }
};

Heartbeat send(Node &from, Node &to, uint32_t now, bool deliver = true) {
  const Heartbeat packet = from.link.nextHeartbeat(now);
  from.link.recordSent(packet, now);
  if (deliver) to.link.receive(from.mac.data(), packet, now);
  return packet;
}

struct Pair {
  Node a, b;
  Pair(uint32_t aSession = 100, uint32_t bSession = 200,
       uint32_t initialSequence = 0)
      : a(1, aSession, initialSequence), b(2, bSession, initialSequence) {}
  void connect(uint32_t start = 0) {
    send(a, b, start);
    send(b, a, start + 10);
    send(a, b, start + 20);
    require(a.link.inRange(start + 20), "A did not connect");
    require(b.link.inRange(start + 20), "B did not connect");
  }
};

void noPeer() {
  Node a(1, 100);
  for (uint32_t now = 0; now < 10000; now += 500) {
    const auto packet = a.link.nextHeartbeat(now);
    a.link.recordSent(packet, now);
    a.link.updateLed(now);
    require(!a.link.inRange(now) && !a.link.ledOn(), "Lone board blinked");
  }
}

void receiveAloneIsNotContact() {
  Pair p;
  send(p.a, p.b, 0);
  require(p.b.link.receivedCount() == 1, "No heartbeat received");
  require(!p.b.link.inRange(0), "One-way reception enabled blinking");
}

void discoveryAndBlinking() {
  Pair p;
  p.connect();
  for (Node *node : {&p.a, &p.b}) {
    require(node->link.updateLed(30) && node->link.ledOn(), "LED did not start");
    require(!node->link.updateLed(279), "LED toggled early");
    require(node->link.updateLed(280) && !node->link.ledOn(), "LED did not turn off");
    require(node->link.updateLed(530) && node->link.ledOn(), "LED did not turn on");
  }
}

void continuousContact() {
  Pair p;
  p.connect();
  for (uint32_t now = 500; now <= 10000; now += 500) {
    send(p.a, p.b, now);
    send(p.b, p.a, now + 10);
    require(p.a.link.inRange(now + 10) && p.b.link.inRange(now + 10),
            "Continuous contact timed out");
  }
}

void timeoutTurnsLedOff() {
  Pair p;
  p.connect();
  p.a.link.updateLed(30);
  require(p.a.link.inRange(3009), "Contact timed out early");
  require(!p.a.link.inRange(3010), "Contact survived timeout boundary");
  p.a.link.updateLed(3010);
  require(!p.a.link.ledOn(), "LED stayed on after timeout");
  require(!p.b.link.inRange(3020), "B survived timeout boundary");
}

void recoveryAfterSeparation() {
  Pair p;
  p.connect();
  require(!p.a.link.inRange(5000) && !p.b.link.inRange(5000), "No timeout");
  p.connect(6000);
}

void oneDirectionLost() {
  Pair p;
  p.connect();
  for (uint32_t now = 500; now <= 5000; now += 500) {
    send(p.a, p.b, now);
    send(p.b, p.a, now + 10, false);
  }
  require(!p.a.link.inRange(5010) && !p.b.link.inRange(5010),
          "One-way link was treated as two-way contact");
}

void packetLoss() {
  Pair p;
  p.connect();
  for (uint32_t now = 500; now <= 15000; now += 500) {
    const unsigned step = now / 500;
    send(p.a, p.b, now, step % 3 != 0);
    send(p.b, p.a, now + 10, step % 4 != 0);
    require(p.a.link.inRange(now + 10) && p.b.link.inRange(now + 10),
            "Moderate packet loss broke contact");
  }
}

void peerReboot() {
  Pair p;
  p.connect();
  p.b.link.begin(p.b.mac.data(), 201);
  send(p.b, p.a, 500);
  require(!p.a.link.inRange(500), "Peer reboot kept old confirmation");
  send(p.a, p.b, 510);
  send(p.b, p.a, 520);
  require(p.a.link.inRange(520) && p.b.link.inRange(520), "Reboot recovery failed");
}

void equalBootIdentifiers() {
  Pair p(77, 77);
  p.connect(); // Distinct MAC addresses still identify the other board.
}

void invalidPackets() {
  Pair p;
  auto packet = p.a.link.nextHeartbeat(0);
  auto invalid = packet;
  invalid.magic ^= 1;
  require(!p.b.link.receive(p.a.mac.data(), invalid, 0), "Wrong magic accepted");
  invalid = packet;
  invalid.group ^= 1;
  require(!p.b.link.receive(p.a.mac.data(), invalid, 0), "Wrong group accepted");
  invalid = packet;
  invalid.session = 0;
  require(!p.b.link.receive(p.a.mac.data(), invalid, 0), "Zero session accepted");
  invalid = packet;
  invalid.sequence = 0;
  require(!p.b.link.receive(p.a.mac.data(), invalid, 0), "Zero sequence accepted");
  require(!p.a.link.receive(p.a.mac.data(), packet, 0), "Own packet accepted");
  require(p.b.link.receivedCount() == 0, "Invalid packet changed counters");
}

void duplicateAndReorderedPackets() {
  Pair p;
  p.connect();
  const auto first = send(p.b, p.a, 500);
  const auto second = send(p.b, p.a, 1000);
  const auto before = p.a.link.receivedCount();
  require(!p.a.link.receive(p.b.mac.data(), second, 3000), "Duplicate accepted");
  require(!p.a.link.receive(p.b.mac.data(), first, 3001), "Reordered packet accepted");
  require(p.a.link.receivedCount() == before, "Replay changed counter");
  require(!p.a.link.inRange(3500), "Replay refreshed contact");
}

void staleOrUnsentAcknowledgement() {
  Pair p;
  const auto request = p.a.link.nextHeartbeat(0);
  p.a.link.recordSent(request, 0);
  auto reply = p.b.link.nextHeartbeat(4000);
  reply.acknowledgedSession = request.session;
  reply.acknowledgedSequence = request.sequence;
  p.a.link.receive(p.b.mac.data(), reply, 4000);
  require(!p.a.link.inRange(4000), "Stale acknowledgement enabled contact");

  const auto unsent = p.a.link.nextHeartbeat(4010); // esp_now_send failed.
  reply = p.b.link.nextHeartbeat(4020);
  reply.acknowledgedSequence = unsent.sequence;
  p.a.link.receive(p.b.mac.data(), reply, 4020);
  require(!p.a.link.inRange(4020), "Unsent heartbeat was acknowledged");
}

void localRebootRejectsOldAcknowledgement() {
  Pair p;
  const auto request = send(p.a, p.b, 0);
  auto delayedReply = p.b.link.nextHeartbeat(10);
  p.a.link.begin(p.a.mac.data(), 101);
  auto newRequest = p.a.link.nextHeartbeat(20);
  p.a.link.recordSent(newRequest, 20); // Same sequence, different boot session.
  p.a.link.receive(p.b.mac.data(), delayedReply, 30);
  require(request.sequence == newRequest.sequence, "Test needs equal sequences");
  require(!p.a.link.inRange(30), "Old boot acknowledgement enabled contact");
  p.connect(500);
}

void clockAndSequenceWraparound() {
  Pair p(100, 200, UINT32_MAX - 2);
  const uint32_t start = UINT32_MAX - 1000;
  p.connect(start);
  send(p.b, p.a, start + 30);
  for (uint32_t offset = 500; offset <= 2500; offset += 500) {
    send(p.a, p.b, start + offset);
    send(p.b, p.a, start + offset + 10);
    require(p.a.link.inRange(start + offset + 10) &&
            p.b.link.inRange(start + offset + 10), "Wraparound broke contact");
  }
  require(!p.a.link.inRange(start + 5510), "Wraparound broke timeout");
  Pair ledPair;
  const uint32_t ledStart = UINT32_MAX - 100;
  ledPair.connect(ledStart);
  ledPair.a.link.updateLed(ledStart + 20);
  require(ledPair.a.link.ledOn(), "LED did not start before clock wrap");
  ledPair.a.link.updateLed(ledStart + 270);
  require(!ledPair.a.link.ledOn(), "LED timer broke at wraparound");
}

void unrelatedThirdBoard() {
  Pair p;
  p.connect();
  Node third(3, 300);
  auto packet = third.link.nextHeartbeat(500);
  require(!p.a.link.receive(third.mac.data(), packet, 500), "Active peer replaced");
  packet = third.link.nextHeartbeat(4000);
  require(p.a.link.receive(third.mac.data(), packet, 4000), "Timed-out peer locked forever");
  require(!p.a.link.inRange(4000), "New peer reused old confirmation");
}

int main() {
  struct Test { const char *name; void (*run)(); };
  const Test tests[] = {
      {"one board: LED remains off", noPeer},
      {"one-way reception is not contact", receiveAloneIsNotContact},
      {"two-board discovery and D2 blink timing", discoveryAndBlinking},
      {"continuous two-way contact", continuousContact},
      {"three-second timeout and LED off", timeoutTurnsLedOff},
      {"automatic recovery after separation", recoveryAfterSeparation},
      {"loss of one direction stops both LEDs", oneDirectionLost},
      {"packet loss tolerance", packetLoss},
      {"peer reboot recovery", peerReboot},
      {"equal random boot IDs on different boards", equalBootIdentifiers},
      {"invalid and self-originated packets", invalidPackets},
      {"duplicate and reordered packets", duplicateAndReorderedPackets},
      {"stale and unsent acknowledgements", staleOrUnsentAcknowledgement},
      {"local reboot rejects old acknowledgements", localRebootRejectsOldAcknowledgement},
      {"millis and sequence counter wraparound", clockAndSequenceWraparound},
      {"unrelated third board", unrelatedThirdBoard},
  };
  unsigned passed = 0;
  for (const Test &test : tests) {
    try {
      test.run();
      std::cout << "PASS: " << test.name << '\n';
      ++passed;
    } catch (const std::exception &error) {
      std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
      return 1;
    }
  }
  std::cout << passed << " scenarios passed\n";
}
