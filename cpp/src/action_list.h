// action_list.h — single source of truth for the comma-separated action-ID
// lists persisted in ExtState (`stale_toggle_actions`).
//
// Before the audit (2026-06-10) this parse/serialize logic was hand-rolled
// four times across container_state.cpp with divergent capacities: readers
// capped at 256 entries / 2048 bytes while writers went to 512 / 4096 — so a
// legally-written list could be silently truncated by the next startup's
// reader, leaking exactly the ghost windows the list exists to prevent. The
// inline serializers also used the `len += snprintf(...)` pattern without a
// truncation check: snprintf returns the WOULD-BE length, so a truncated
// write pushed len past the buffer and the next iteration computed
// sizeof(buf)-len as a huge size_t — an out-of-bounds stack write.
//
// Pure logic, no SWELL — unit-tested in tests/test_action_list.cpp.
#pragma once

static const int ACTION_LIST_MAX = 512;   // entries — reader == writer
static const int ACTION_LIST_BUF = 4096;  // serialized form incl. NUL

// Parses "123,456,..." into out[maxOut]. Non-positive entries are ignored;
// runs longer than 9 digits are invalid (would overflow int — the old code
// hit signed-overflow UB there) and terminate the parse. Returns count.
int ParseActionList(const char* s, int* out, int maxOut);

// Appends `action` if positive, not already present, and capacity remains.
// Returns true if appended.
bool AppendUniqueAction(int* arr, int* count, int maxCount, int action);

// True if arr[0..count) contains action.
bool ActionListContains(const int* arr, int count, int action);

// Serializes count IDs into buf as "a,b,c". Truncation-safe: entries that
// don't fit are dropped cleanly, buf stays NUL-terminated, and the return
// value never exceeds bufSize-1.
int SerializeActionList(const int* arr, int count, char* buf, int bufSize);
