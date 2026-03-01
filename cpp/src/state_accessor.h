#pragma once
#include "globals.h"

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
