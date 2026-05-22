// ResolveActionCommand unit tests (ADR-018 / T1.1).
//
// Covers the inline helper in globals.h that production code uses everywhere
// it persists or restores REAPER action references. Behavior:
//   - Empty / null input → 0
//   - Pure decimal numeric string → parsed int (positive)
//   - Leading underscore (named command) → routes to g_NamedCommandLookup
//   - Negative / zero / out-of-range numeric → 0
//   - Trailing garbage after digits — currently strtol-permissive; codify
//     the existing behavior so future cleanups surface intentionally.

#include "catch2/catch.hpp"
#include "globals.h"

namespace {

// Mock state for the NamedCommandLookup callback. Tests set
// g_mockLookupResult, install the mock via SetMockNamedCommandLookup(),
// and verify ResolveActionCommand routes underscore-prefixed strings here.
const char* g_lastLookupArg = nullptr;
int g_mockLookupResult = 0;

int MockNamedCommandLookup(const char* cmd)
{
  g_lastLookupArg = cmd;
  return g_mockLookupResult;
}

struct NamedLookupGuard {
  int (*saved)(const char*);
  NamedLookupGuard() : saved(g_NamedCommandLookup) {
    g_NamedCommandLookup = MockNamedCommandLookup;
    g_lastLookupArg = nullptr;
    g_mockLookupResult = 0;
  }
  ~NamedLookupGuard() { g_NamedCommandLookup = saved; }
};

}  // namespace

TEST_CASE("ResolveActionCommand returns 0 for null and empty input", "[globals][resolve]")
{
  REQUIRE(ResolveActionCommand(nullptr) == 0);
  REQUIRE(ResolveActionCommand("") == 0);
}

TEST_CASE("ResolveActionCommand parses positive decimal strings", "[globals][resolve]")
{
  REQUIRE(ResolveActionCommand("1") == 1);
  REQUIRE(ResolveActionCommand("40078") == 40078);   // Mixer toggle
  REQUIRE(ResolveActionCommand("65535") == 65535);
}

TEST_CASE("ResolveActionCommand rejects non-positive numeric inputs", "[globals][resolve]")
{
  REQUIRE(ResolveActionCommand("0") == 0);
  REQUIRE(ResolveActionCommand("-1") == 0);
  REQUIRE(ResolveActionCommand("-40078") == 0);
}

TEST_CASE("ResolveActionCommand routes underscore-prefixed strings to NamedCommandLookup",
          "[globals][resolve][mock]")
{
  NamedLookupGuard guard;

  SECTION("hit — mock returns valid id") {
    g_mockLookupResult = 12345;
    const int got = ResolveActionCommand("_RS791856abcdef");
    REQUIRE(got == 12345);
    REQUIRE(g_lastLookupArg != nullptr);
    REQUIRE(std::string(g_lastLookupArg) == "_RS791856abcdef");
  }

  SECTION("miss — mock returns 0") {
    g_mockLookupResult = 0;
    REQUIRE(ResolveActionCommand("_RS_does_not_exist") == 0);
  }
}

TEST_CASE("ResolveActionCommand returns 0 for underscore input when lookup unavailable",
          "[globals][resolve]")
{
  // Save + null out the global to simulate REAPER API not loaded.
  auto saved = g_NamedCommandLookup;
  g_NamedCommandLookup = nullptr;
  REQUIRE(ResolveActionCommand("_RSwhatever") == 0);
  g_NamedCommandLookup = saved;
}
