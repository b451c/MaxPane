#include "action_list.h"

#include <cstdio>

int ParseActionList(const char* s, int* out, int maxOut)
{
  if (!s || !out || maxOut <= 0) return 0;
  int count = 0;
  const char* cur = s;
  while (*cur && count < maxOut) {
    int a = 0;
    int digits = 0;
    while (*cur >= '0' && *cur <= '9') {
      if (++digits > 9) return count;  // would overflow int — reject the rest
      a = a * 10 + (*cur - '0');
      cur++;
    }
    if (a > 0) out[count++] = a;
    if (*cur == ',') cur++;
    else break;
  }
  return count;
}

bool ActionListContains(const int* arr, int count, int action)
{
  if (!arr) return false;
  for (int i = 0; i < count; i++) {
    if (arr[i] == action) return true;
  }
  return false;
}

bool AppendUniqueAction(int* arr, int* count, int maxCount, int action)
{
  if (!arr || !count || action <= 0 || *count >= maxCount) return false;
  if (ActionListContains(arr, *count, action)) return false;
  arr[(*count)++] = action;
  return true;
}

int SerializeActionList(const int* arr, int count, char* buf, int bufSize)
{
  if (!buf || bufSize <= 0) return 0;
  buf[0] = '\0';
  if (!arr) return 0;
  int len = 0;
  for (int i = 0; i < count; i++) {
    int n = snprintf(buf + len, (size_t)(bufSize - len),
                     "%s%d", len > 0 ? "," : "", arr[i]);
    if (n < 0 || n >= bufSize - len) {
      buf[len] = '\0';  // drop the truncated entry, keep the list valid
      break;
    }
    len += n;
  }
  return len;
}
