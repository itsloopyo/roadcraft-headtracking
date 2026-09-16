// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The port is a shared resource and the mod is not always first to it. A player
// who launches RoadCraft while the previous game is still shutting down, or who
// quits that game a minute later, must get head tracking without relaunching
// anything. That recovery is timed here rather than reasoned about: each case
// holds the real port with a real socket, releases it, and measures how long the
// receiver takes to bind and publish a pose from a live sender.
//
// These are live-socket tests. They bind a port, so they are the one suite here
// that can fail for a reason outside the repo (something else already holding
// the test port); the first check reports that case as a failure rather than
// skipping, because a silently skipped recovery test is how a regression ships.

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/protocol/udp_socket.h"
#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using rc_test::Check;
using Clock = std::chrono::steady_clock;

// Not 4242: a developer running these with OpenTrack open would otherwise be
// testing against their own tracker. 5886 is the port the lab notes reserve for
// this game.
constexpr uint16_t kTestPort = 5886;

// Fast enough that the sender contributes at most a few ms to any measured
// recovery, so what the numbers below show is the receiver's retry cadence and
// not the tracker's sample rate.
constexpr int kSenderIntervalMs = 5;

constexpr float kSentYaw = 12.5f;
constexpr float kSentPitch = -4.25f;
constexpr float kSentRoll = 2.0f;

int64_t MillisSince(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void BuildPacket(char (&packet)[cameraunlock::OpenTrackPacket::kMinPacketSize]) {
    const double values[6] = {0.0, 0.0, 0.0, kSentYaw, kSentPitch, kSentRoll};
    std::memcpy(packet, values, sizeof(values));
}

// A stand-in for OpenTrack: keeps sending for the whole test so that "the port
// came free" and "a packet arrived" are separated only by the receiver's own
// latency.
class Sender {
public:
    bool Start() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) {
            WSACleanup();
            return false;
        }
        std::memset(&m_dest, 0, sizeof(m_dest));
        m_dest.sin_family = AF_INET;
        m_dest.sin_port = htons(kTestPort);
        inet_pton(AF_INET, "127.0.0.1", &m_dest.sin_addr);
        m_thread = std::thread([this] { Pump(); });
        return true;
    }

    void Stop() {
        m_stop.store(true);
        if (m_thread.joinable()) m_thread.join();
        if (m_socket != INVALID_SOCKET) closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        WSACleanup();
    }

private:
    void Pump() {
        char packet[cameraunlock::OpenTrackPacket::kMinPacketSize];
        BuildPacket(packet);
        while (!m_stop.load()) {
            sendto(m_socket, packet, sizeof(packet), 0,
                   reinterpret_cast<sockaddr*>(&m_dest), sizeof(m_dest));
            std::this_thread::sleep_for(std::chrono::milliseconds(kSenderIntervalMs));
        }
    }

    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_dest{};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

// Collects what the receiver logged, so the bind-failure message can be read
// back and checked against what the OS actually said.
class LogCapture {
public:
    std::function<void(const std::string&)> Sink() {
        return [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lines.push_back(line);
        };
    }

    std::vector<std::string> Lines() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lines;
    }

    bool Any(const std::string& needle) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& line : m_lines) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
};

// Blocks until the receiver publishes the sender's pose, or the deadline
// passes. Returns the elapsed milliseconds, or -1 on timeout.
int64_t WaitForPose(const cameraunlock::UdpReceiver& receiver, Clock::time_point releasedAt,
                    int64_t timeoutMs) {
    for (;;) {
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        if (receiver.GetRotation(yaw, pitch, roll) && yaw == kSentYaw) {
            return MillisSince(releasedAt);
        }
        if (MillisSince(releasedAt) > timeoutMs) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// One full cycle of the scenario the player hits: something else owns the port
// when the mod starts, it lets go `holdMs` later, and tracking has to come up on
// its own. Returns the milliseconds from release to the first published pose, or
// -1 if it never came up.
int64_t MeasureRecovery(int64_t holdMs) {
    cameraunlock::UdpSocket squatter;
    if (!squatter.Open(kTestPort)) return -2;

    cameraunlock::UdpReceiver receiver;
    LogCapture log;
    receiver.SetLog(log.Sink());
    if (receiver.Start(kTestPort)) return -3;  // bind must fail while held

    std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));

    squatter.Close();
    const auto releasedAt = Clock::now();

    const int64_t elapsed = WaitForPose(receiver, releasedAt, 10000);
    receiver.Stop();
    return elapsed;
}

void TestPortHeldAtStartup() {
    std::printf("\n-- the port is held when the mod starts --\n");

    cameraunlock::UdpSocket squatter;
    if (!Check(squatter.Open(kTestPort), "test port is free to begin with")) return;

    cameraunlock::UdpReceiver receiver;
    LogCapture log;
    receiver.SetLog(log.Sink());

    const bool started = receiver.Start(kTestPort);
    Check(!started, "Start reports failure while another socket holds the port");
    Check(receiver.IsFailed(), "IsFailed is set after the failed bind");
    Check(receiver.IsRetrying(), "IsRetrying is set, so the supervisor is looping");
    Check(!receiver.IsRunning(), "IsRunning is false - nothing pretends to be listening");

    // The message has to carry what the OS said. A sentence like "another
    // program is holding it" is only ever a guess: the same bind fails with
    // WSAEACCES on a port inside a Hyper-V reserved range, and no program holds
    // anything in that case.
    const auto lines = log.Lines();
    if (Check(!lines.empty(), "the failed bind is logged")) {
        const std::string& first = lines.front();
        std::printf("  bind failure logged as: %s\n", first.c_str());
        Check(first.find("bind failed with error ") != std::string::npos,
              "the log names the failing call and the OS error code");
        Check(first.find(std::to_string(WSAEADDRINUSE)) != std::string::npos,
              "the code is the real one for a port conflict (10048)");
        Check(first.find("retrying every") != std::string::npos,
              "the log says recovery is automatic, so nobody relaunches the game");
    }

    // Prove the text is the socket's own account rather than a phrase composed
    // here: a direct Open on the same held port must produce the same string.
    cameraunlock::UdpSocket probe;
    Check(!probe.Open(kTestPort), "a second bind on the held port fails too");
    if (!lines.empty()) {
        Check(lines.front().find(probe.LastError()) != std::string::npos,
              "the logged cause is exactly what the socket reported");
        std::printf("  socket reported: %s\n", probe.LastError().c_str());
    }

    receiver.Stop();
    squatter.Close();
}

void TestRecoveryLatency() {
    std::printf("\n-- the previous game closes and the port comes free --\n");

    Sender sender;
    if (!Check(sender.Start(), "test sender started")) return;

    // Releases are spread across the retry period so the samples land at
    // different phases of it. The spread of the results is the cadence: with a
    // 500ms retry and a 100ms supervisor tick, no release should wait longer
    // than about 600ms, and releases just before a retry should come up almost
    // at once.
    const int64_t holds[] = {250, 330, 410, 490, 570, 650, 730, 810};
    std::vector<int64_t> samples;

    for (int64_t hold : holds) {
        const int64_t elapsed = MeasureRecovery(hold);
        if (elapsed == -2) {
            Check(false, "test port was free for the squatter");
            break;
        }
        if (elapsed == -3) {
            Check(false, "bind failed while the port was held");
            break;
        }
        if (elapsed < 0) {
            Check(false, "tracking recovered after the port came free");
            break;
        }
        std::printf("  held %lldms -> first pose %lldms after release\n",
                    static_cast<long long>(hold), static_cast<long long>(elapsed));
        samples.push_back(elapsed);
    }

    sender.Stop();

    if (!Check(samples.size() == sizeof(holds) / sizeof(holds[0]),
               "every release recovered")) {
        return;
    }

    int64_t worst = 0;
    int64_t total = 0;
    for (int64_t s : samples) {
        if (s > worst) worst = s;
        total += s;
    }
    std::printf("  worst %lldms, mean %lldms over %zu releases\n",
                static_cast<long long>(worst),
                static_cast<long long>(total / static_cast<int64_t>(samples.size())),
                samples.size());

    // kRetryIntervalMs (500) plus one supervisor tick (100) is the ceiling the
    // design implies; the slack covers scheduler jitter on a loaded CI runner.
    // This is the number the player feels, so it is asserted rather than
    // printed.
    Check(worst <= 900, "worst-case recovery is inside one retry period plus a tick");
    Check(worst >= 1, "the measurement is real - recovery was not instantaneous");
}

void TestLongHoldStillRecovers() {
    std::printf("\n-- the port stays held for several seconds --\n");

    Sender sender;
    if (!Check(sender.Start(), "test sender started")) return;

    // A player who forgets the other game is running does not come back in half
    // a second. The retry has no attempt limit and no backoff, so a long hold
    // must recover just as fast as a short one.
    const int64_t elapsed = MeasureRecovery(6000);
    sender.Stop();

    if (!Check(elapsed >= 0, "tracking recovered after a 6s hold")) return;
    std::printf("  held 6000ms -> first pose %lldms after release\n",
                static_cast<long long>(elapsed));
    Check(elapsed <= 900, "a long wait does not slow the retry down (no backoff)");
}

void TestRetryLogIsNotSpam() {
    std::printf("\n-- the retry loop does not fill the log --\n");

    cameraunlock::UdpSocket squatter;
    if (!Check(squatter.Open(kTestPort), "test port is free to begin with")) return;

    cameraunlock::UdpReceiver receiver;
    LogCapture log;
    receiver.SetLog(log.Sink());
    Check(!receiver.Start(kTestPort), "bind fails while the port is held");

    // Six seconds is a dozen retries. kRetryLogIntervalMs is 30s, so the failure
    // line is the only one that should have been written.
    std::this_thread::sleep_for(std::chrono::milliseconds(6000));
    const size_t during = log.Lines().size();
    std::printf("  %zu log line(s) after 6s of retrying\n", during);
    Check(during == 1, "only the first failure is logged, not every retry");

    squatter.Close();
    const auto releasedAt = Clock::now();
    while (!receiver.IsRunning() && MillisSince(releasedAt) < 5000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(receiver.IsRunning(), "the receiver is listening once the port is free");
    Check(!receiver.IsFailed(), "IsFailed clears on the successful retry");
    Check(!receiver.IsRetrying(), "IsRetrying clears on the successful retry");
    Check(log.Any("Bound UDP port"), "the recovery is logged, so a log answers 'did it come back'");

    receiver.Stop();
}

}  // namespace

int main() {
    std::printf("udp_recovery_tests: port %u\n", static_cast<unsigned>(kTestPort));

    TestPortHeldAtStartup();
    TestRecoveryLatency();
    TestLongHoldStillRecovers();
    TestRetryLogIsNotSpam();

    return rc_test::Summary("udp_recovery_tests");
}
