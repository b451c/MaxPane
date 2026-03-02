#pragma once
#include "globals.h"
#include "state_limits.h"
#include <cstring>

class StateAccessor {
public:
  virtual ~StateAccessor() = default;
  virtual void Set(const char* section, const char* key, const char* value, bool persist) = 0;
  virtual const char* Get(const char* section, const char* key) = 0;
};

class GlobalStateAccessor : public StateAccessor {
public:
  void Set(const char* section, const char* key, const char* value, bool persist) override {
    if (g_SetExtState) g_SetExtState(section, key, value, persist);
  }
  const char* Get(const char* section, const char* key) override {
    if (!g_GetExtState) return nullptr;
    const char* val = g_GetExtState(section, key);
    return (val && val[0]) ? val : nullptr;
  }
};

class ProjectStateAccessor : public StateAccessor {
  ReaProject* m_proj;
  char m_buf[4096];
public:
  explicit ProjectStateAccessor(ReaProject* proj) : m_proj(proj) { m_buf[0] = '\0'; }
  void Set(const char* section, const char* key, const char* value, bool) override {
    if (g_SetProjExtState && m_proj) g_SetProjExtState(m_proj, section, key, value);
  }
  const char* Get(const char* section, const char* key) override {
    if (!g_GetProjExtState || !m_proj) return nullptr;
    m_buf[0] = '\0';
    g_GetProjExtState(m_proj, section, key, m_buf, sizeof(m_buf));
    return m_buf[0] ? m_buf : nullptr;
  }
};

// RPP write accessor — collects key=value pairs for writing to RPP chunk

class RppWriteAccessor : public StateAccessor {
  struct KV {
    char key[RPP_KV_KEY_LEN];
    char value[RPP_KV_VAL_LEN];
  };
  KV m_entries[RPP_KV_MAX];
  int m_count;
public:
  RppWriteAccessor() : m_count(0) { memset(m_entries, 0, sizeof(m_entries)); }
  void Set(const char* /*section*/, const char* key, const char* value, bool) override {
    if (m_count >= RPP_KV_MAX || !key) return;
    safe_strncpy(m_entries[m_count].key, key, RPP_KV_KEY_LEN);
    safe_strncpy(m_entries[m_count].value, value ? value : "", RPP_KV_VAL_LEN);
    m_count++;
  }
  const char* Get(const char* /*section*/, const char* /*key*/) override { return nullptr; }

  int GetCount() const { return m_count; }
  const char* GetKey(int i) const { return (i >= 0 && i < m_count) ? m_entries[i].key : ""; }
  const char* GetValue(int i) const { return (i >= 0 && i < m_count) ? m_entries[i].value : ""; }
};

// RPP read accessor — parses "KEY VALUE" lines from buffered RPP chunk data
class RppReadAccessor : public StateAccessor {
  const char (*m_lines)[RPP_MAX_LINE_LEN];  // pointer to array of lines
  int m_lineCount;
  mutable char m_buf[RPP_KV_VAL_LEN];
public:
  RppReadAccessor(const char lines[][RPP_MAX_LINE_LEN], int lineCount)
    : m_lines(lines), m_lineCount(lineCount) { m_buf[0] = '\0'; }

  void Set(const char*, const char*, const char*, bool) override { /* read-only */ }

  const char* Get(const char* /*section*/, const char* key) override {
    if (!key || !key[0]) return nullptr;
    int keyLen = (int)strlen(key);
    for (int i = 0; i < m_lineCount; i++) {
      // Lines are "KEY VALUE" format — match key followed by space
      if (strncmp(m_lines[i], key, (size_t)keyLen) == 0 && m_lines[i][keyLen] == ' ') {
        const char* val = m_lines[i] + keyLen + 1;
        safe_strncpy(m_buf, val, sizeof(m_buf));
        return m_buf[0] ? m_buf : nullptr;
      }
    }
    return nullptr;
  }
};
