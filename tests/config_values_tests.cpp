// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The numeric and boolean half of HeadTracking.ini, read through the real
// loader. Windows' INI reader hands back everything after the '=' including an
// inline comment, so what each key does with that trailing text is a boundary
// decision a user hits the first time they annotate their own file - and it
// differs between the number keys and the flag keys. The hotkey half has its
// own suite; this covers the port, the two smoothing values, the position
// limits and the flags.

#include "config.h"

#include "ini_fixture.h"
#include "test_support.h"

#include <cstdio>
#include <string>

using namespace rc_ht;
using rc_test::Check;
using rc_test::CheckClose;

namespace {

std::string g_dir;

// Loads `body` as the whole INI over a Config seeded with the shipped defaults.
Config Load(const char* body) {
    if (!rc_test::WriteIni(g_dir, body)) return Config{};

    Config cfg;
    LoadConfig(g_dir, cfg);
    return cfg;
}

// Loads over a Config whose every value is off its default, so a key the loader
// never reaches shows up as the poisoned value surviving rather than as a
// default that was never written.
Config LoadOverPoisoned(const char* body) {
    if (!rc_test::WriteIni(g_dir, body)) return Config{};

    Config cfg;
    cfg.local_smoothing = 0.99f;
    cfg.remote_smoothing = 0.99f;
    cfg.limit_x = 0.45f;
    LoadConfig(g_dir, cfg);
    return cfg;
}

void UdpPortTests() {
    std::printf("UdpPort\n");
    const Config defaults;

    Check(Load("[Network]\nUdpPort=5886\n").udp_port == 5886, "a port in range is taken");
    Check(Load("[Network]\nUdpPort=5886 ; the test port\n").udp_port == 5886,
          "a trailing ; comment is not junk, and the port is still read");
    Check(Load("[Network]\nUdpPort=5886 # the test port\n").udp_port == 5886,
          "and neither is a trailing # comment");

    Check(Load("[Network]\nUdpPort=nope\n").udp_port == defaults.udp_port,
          "a value that is not a number keeps the previous port");
    Check(Load("[Network]\nUdpPort=5886abc\n").udp_port == defaults.udp_port,
          "trailing junk that is not a comment is refused rather than half-read");
    Check(Load("[Network]\nUdpPort=80\n").udp_port == defaults.udp_port,
          "a privileged port is refused");
    Check(Load("[Network]\nUdpPort=70000\n").udp_port == defaults.udp_port,
          "a port past 65535 is refused rather than truncated into range");
    Check(Load("[Network]\nUdpPort=\n").udp_port == defaults.udp_port,
          "an empty value keeps the previous port");
}

void SmoothingTests() {
    std::printf("Smoothing\n");
    const Config defaults;

    CheckClose(Load("[Smoothing]\nLocalSmoothing=0.5\n").local_smoothing, 0.5f,
               "a value in 0..1 is taken");
    CheckClose(Load("[Smoothing]\nLocalSmoothing=0.5 ; steady\n").local_smoothing, 0.5f,
               "a trailing ; comment is not junk");
    CheckClose(Load("[Smoothing]\nLocalSmoothing=0.5 # steady\n").local_smoothing, 0.5f,
               "nor is a trailing # comment");

    // A comma-decimal machine writes 0,5 without being asked. Read as a prefix
    // that would be 0, which is a legal smoothing value and silently wrong.
    CheckClose(LoadOverPoisoned("[Smoothing]\nLocalSmoothing=0,5\n").local_smoothing, 0.99f,
               "a comma decimal is refused rather than read as 0");
    CheckClose(LoadOverPoisoned("[Smoothing]\nLocalSmoothing=lots\n").local_smoothing, 0.99f,
               "a value that is not a number keeps what was already there");

    CheckClose(Load("[Smoothing]\nLocalSmoothing=2.0\n").local_smoothing, 1.0f,
               "past the top of the range clamps to 1.0");
    CheckClose(Load("[Smoothing]\nRemoteSmoothing=-1.0\n").remote_smoothing, 0.0f,
               "below the bottom clamps to 0.0");

    // strtod parses "nan" and "inf", so the sanitizer is what stops either
    // reaching the processor - and it lands on the key's OWN shipped default,
    // not on whatever the other key defaults to.
    CheckClose(LoadOverPoisoned("[Smoothing]\nLocalSmoothing=nan\n").local_smoothing,
               defaults.local_smoothing, "NaN falls back to LocalSmoothing's shipped default");
    CheckClose(LoadOverPoisoned("[Smoothing]\nRemoteSmoothing=inf\n").remote_smoothing,
               defaults.remote_smoothing, "and infinity to RemoteSmoothing's own");
}

void PositionLimitTests() {
    std::printf("Position limits\n");

    CheckClose(Load("[Position]\nLimitX=0.45\n").limit_x, 0.45f, "a limit in range is taken");
    CheckClose(Load("[Position]\nLimitZ=0.25 ; forward\n").limit_z, 0.25f,
               "a trailing comment is not junk");
    CheckClose(Load("[Position]\nLimitX=0.9\n").limit_x, 0.5f,
               "past the half-metre ceiling clamps to 0.5");
    CheckClose(Load("[Position]\nLimitY=-0.1\n").limit_y, 0.0f,
               "a negative limit clamps to 0 rather than inverting the clamp");
    CheckClose(LoadOverPoisoned("[Position]\nLimitX=wide\n").limit_x, 0.45f,
               "a value that is not a number keeps what was already there");
    CheckClose(LoadOverPoisoned("[Position]\nLimitX=nan\n").limit_x, 0.45f,
               "and so does NaN");
}

void FlagTests() {
    std::printf("Flags\n");
    const Config defaults;

    Check(Load("[General]\nEnableOnStartup=0\n").enable_on_startup == false, "0 is false");
    Check(Load("[General]\nEnableOnStartup=false\n").enable_on_startup == false,
          "and so is false");
    Check(Load("[General]\nEnableOnStartup=no\n").enable_on_startup == false, "and no");
    Check(Load("[Position]\nEnabled=off\n").position_enabled == false, "and off");
    Check(Load("[Position]\nEnabled=1\n").position_enabled == true, "1 is true");

    Check(Load("[General]\nEnableOnStartup=maybe\n").enable_on_startup
              == defaults.enable_on_startup,
          "a word that is neither keeps the previous setting");

    // The documented trap, locked so it cannot change silently: the whole value
    // has to be the word, so an inline comment on a FLAG is part of it and the
    // edit is refused. The log line says so; the value does not move.
    Check(Load("[General]\nEnableOnStartup=0 ; off for now\n").enable_on_startup
              == defaults.enable_on_startup,
          "an inline comment on a flag is part of the value, so the edit is refused");
    Check(Load("[General]\nEnableOnStartup=\n").enable_on_startup == defaults.enable_on_startup,
          "an empty value keeps the previous setting");
}

// The unknown-key warning reads the whole section back through
// GetPrivateProfileSectionA, which truncates at the buffer it is given. A
// section bigger than that used to abandon the check silently; now the buffer
// grows. What a user can see either way is that the keys in the section still
// load, and that is what this pins - the growth loop has to terminate and hand
// back the whole section, not just stop hanging.
void OversizedSectionTests() {
    std::printf("A section too big for the first read buffer\n");

    std::string body = "[Position]\n";
    for (int i = 0; i < 600; ++i) {
        body += "Padding" + std::to_string(i) + "=0\n";  // ~10 bytes each, well past 4096
    }
    body += "LimitX=0.25\n";

    Check(body.size() > 4096, "the fixture really is larger than the first buffer");
    CheckClose(LoadOverPoisoned(body.c_str()).limit_x, 0.25f,
               "a key at the far end of an oversized section is still read");
}

void MissingFileTests() {
    std::printf("No file at all\n");
    Config cfg;
    cfg.udp_port = 5555;
    LoadConfig(g_dir + "\\does-not-exist", cfg);
    Check(cfg.udp_port == 5555, "an unreadable directory leaves the Config untouched");
}

}  // namespace

int main() {
    std::printf("RoadCraft head tracking - config value tests\n");
    std::printf("===========================================\n");
    g_dir = rc_test::MakeTempDir("values");
    if (g_dir.empty()) return rc_test::Summary("config values");
    UdpPortTests();
    SmoothingTests();
    PositionLimitTests();
    FlagTests();
    OversizedSectionTests();
    MissingFileTests();
    rc_test::RemoveTempDir(g_dir);
    return rc_test::Summary("config values");
}
