// action_list unit tests (audit M2.1 + M1.4).
//
// Pins the unified stale_toggle_actions parse/serialize behavior. The four
// hand-rolled copies this replaces diverged on capacity (reader 256 vs
// writer 512) and their serializers could overshoot the buffer via the
// unchecked `len += snprintf(...)` pattern — both are regression-pinned here.

#include "catch2/catch.hpp"
#include "action_list.h"

#include <cstdio>
#include <cstring>

TEST_CASE("ParseActionList handles normal, hostile, and degenerate input", "[action-list]")
{
  int out[8];
  CHECK(ParseActionList("41651,40078,123", out, 8) == 3);
  CHECK(out[0] == 41651);
  CHECK(out[2] == 123);

  // Zero entries are skipped, trailing comma tolerated.
  CHECK(ParseActionList("0,5,", out, 8) == 1);
  CHECK(out[0] == 5);

  // Parse stops at the first non-digit garbage.
  CHECK(ParseActionList("12,abc,34", out, 8) == 1);
  CHECK(ParseActionList("abc", out, 8) == 0);
  CHECK(ParseActionList("", out, 8) == 0);
  CHECK(ParseActionList(nullptr, out, 8) == 0);

  // >9-digit runs would overflow int (old code hit signed-overflow UB) —
  // the run terminates the parse instead.
  CHECK(ParseActionList("1234567890123,42", out, 8) == 0);

  // Capacity respected.
  CHECK(ParseActionList("1,2,3,4,5,6,7,8,9,10", out, 4) == 4);
}

TEST_CASE("AppendUniqueAction dedupes and respects capacity", "[action-list]")
{
  int arr[3];
  int cnt = 0;
  CHECK(AppendUniqueAction(arr, &cnt, 3, 10));
  CHECK_FALSE(AppendUniqueAction(arr, &cnt, 3, 10));   // duplicate
  CHECK_FALSE(AppendUniqueAction(arr, &cnt, 3, 0));    // non-positive
  CHECK_FALSE(AppendUniqueAction(arr, &cnt, 3, -4));
  CHECK(AppendUniqueAction(arr, &cnt, 3, 20));
  CHECK(AppendUniqueAction(arr, &cnt, 3, 30));
  CHECK_FALSE(AppendUniqueAction(arr, &cnt, 3, 40));   // full
  CHECK(cnt == 3);
  CHECK(ActionListContains(arr, cnt, 20));
  CHECK_FALSE(ActionListContains(arr, cnt, 40));
}

TEST_CASE("SerializeActionList round-trips and never overflows", "[action-list]")
{
  int in[5] = { 41651, 1, 999999999, 7, 7 };
  char buf[64];
  int len = SerializeActionList(in, 5, buf, sizeof(buf));
  CHECK(len == (int)strlen(buf));
  CHECK(strcmp(buf, "41651,1,999999999,7,7") == 0);

  int back[8];
  CHECK(ParseActionList(buf, back, 8) == 5);
  CHECK(back[2] == 999999999);

  // Truncation: entries that don't fit are dropped cleanly; the result is
  // still a valid, parseable list and len never exceeds bufSize-1.
  char tiny[8];
  len = SerializeActionList(in, 5, tiny, sizeof(tiny));
  CHECK(len <= (int)sizeof(tiny) - 1);
  CHECK(len == (int)strlen(tiny));
  CHECK(strcmp(tiny, "41651,1") == 0);  // third entry doesn't fit → dropped

  // Regression pin for the old `len += snprintf` OOB pattern: many large
  // IDs against an exact-capacity buffer must not write past the end.
  int big[400];
  for (int i = 0; i < 400; i++) big[i] = 2000000000 - i;
  char exact[2048];
  len = SerializeActionList(big, 400, exact, sizeof(exact));
  CHECK(len <= (int)sizeof(exact) - 1);
  CHECK(len == (int)strlen(exact));

  // Degenerate args.
  CHECK(SerializeActionList(nullptr, 3, buf, sizeof(buf)) == 0);
  CHECK(buf[0] == '\0');
  CHECK(SerializeActionList(in, 0, buf, sizeof(buf)) == 0);
  CHECK(SerializeActionList(in, 5, nullptr, 0) == 0);
}

TEST_CASE("Reader and writer capacities are unified", "[action-list]")
{
  // The class of bug this kills: writer persisted up to 512 entries while
  // readers silently truncated at 256. One constant now serves both.
  static_assert(ACTION_LIST_MAX >= 512, "capacity must cover the old writer cap");
  // 512 max-width IDs ("2147483647," = 11 chars) exceed 4096 chars — the
  // serialized form truncates cleanly rather than overflowing; verify the
  // buffer constant covers at least the realistic case (typical 5-6 digit
  // action IDs: 512 * 7 = 3584 < 4096).
  int ids[ACTION_LIST_MAX];
  for (int i = 0; i < ACTION_LIST_MAX; i++) ids[i] = 40000 + i;
  char buf[ACTION_LIST_BUF];
  int len = SerializeActionList(ids, ACTION_LIST_MAX, buf, sizeof(buf));
  int back[ACTION_LIST_MAX];
  CHECK(ParseActionList(buf, back, ACTION_LIST_MAX) == ACTION_LIST_MAX);
  CHECK(len < ACTION_LIST_BUF);
}
