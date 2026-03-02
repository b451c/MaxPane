#include "favorites_manager.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

FavoritesManager::FavoritesManager()
  : m_count(0)
{
  memset(m_favorites, 0, sizeof(m_favorites));
}

void FavoritesManager::Load()
{
  m_count = 0;
  memset(m_favorites, 0, sizeof(m_favorites));

  if (!g_GetExtState) return;

  const char* countStr = g_GetExtState(EXT_SECTION, "fav_count");
  DBG("[ReDockIt] FavoritesManager::Load: section='%s' fav_count='%s'\n",
      EXT_SECTION, countStr ? countStr : "(null)");
  if (!countStr || !countStr[0]) return;

  int count = atoi(countStr);
  if (count < 0) count = 0;
  if (count > MAX_FAVORITES) count = MAX_FAVORITES;

  char key[128];
  for (int i = 0; i < count; i++) {
    FavoriteEntry& fav = m_favorites[m_count];
    memset(&fav, 0, sizeof(FavoriteEntry));

    snprintf(key, sizeof(key), "fav_%d_name", i);
    const char* name = g_GetExtState(EXT_SECTION, key);
    DBG("[ReDockIt] FavoritesManager::Load: fav_%d_name='%s'\n", i, name ? name : "(null)");
    if (!name || !name[0]) continue;
    safe_strncpy(fav.name, name, sizeof(fav.name));

    snprintf(key, sizeof(key), "fav_%d_search", i);
    const char* search = g_GetExtState(EXT_SECTION, key);
    if (search && search[0]) {
      safe_strncpy(fav.searchTitle, search, sizeof(fav.searchTitle));
    } else {
      safe_strncpy(fav.searchTitle, fav.name, sizeof(fav.searchTitle));
    }

    snprintf(key, sizeof(key), "fav_%d_actioncmd", i);
    const char* actionCmd = g_GetExtState(EXT_SECTION, key);
    DBG("[ReDockIt] FavoritesManager::Load: fav_%d_actioncmd='%s'\n", i, actionCmd ? actionCmd : "(null)");
    if (actionCmd && actionCmd[0]) {
      safe_strncpy(fav.actionCommand, actionCmd, sizeof(fav.actionCommand));
      fav.toggleAction = ResolveActionCommand(actionCmd);
      DBG("[ReDockIt] FavoritesManager::Load: resolved actioncmd '%s' -> %d\n",
          actionCmd, fav.toggleAction);
    } else {
      // Legacy: try old numeric-only key
      snprintf(key, sizeof(key), "fav_%d_action", i);
      const char* actionStr = g_GetExtState(EXT_SECTION, key);
      fav.toggleAction = (actionStr && actionStr[0]) ? atoi(actionStr) : 0;
      if (fav.toggleAction > 0) {
        snprintf(fav.actionCommand, sizeof(fav.actionCommand), "%d", fav.toggleAction);
      }
      DBG("[ReDockIt] FavoritesManager::Load: legacy action='%s' -> %d\n",
          actionStr ? actionStr : "(null)", fav.toggleAction);
    }

    snprintf(key, sizeof(key), "fav_%d_known", i);
    const char* knownStr = g_GetExtState(EXT_SECTION, key);
    fav.isKnown = (knownStr && knownStr[0] == '1');

    fav.used = true;
    m_count++;
  }

  DBG("[ReDockIt] FavoritesManager: loaded %d favorites\n", m_count);
}

void FavoritesManager::Save()
{
  if (!g_SetExtState) return;

  char buf[256];
  char key[128];

  snprintf(buf, sizeof(buf), "%d", m_count);
  g_SetExtState(EXT_SECTION, "fav_count", buf, true);

  for (int i = 0; i < m_count; i++) {
    const FavoriteEntry& fav = m_favorites[i];

    snprintf(key, sizeof(key), "fav_%d_name", i);
    g_SetExtState(EXT_SECTION, key, fav.name, true);

    snprintf(key, sizeof(key), "fav_%d_search", i);
    g_SetExtState(EXT_SECTION, key, fav.searchTitle, true);

    snprintf(key, sizeof(key), "fav_%d_actioncmd", i);
    g_SetExtState(EXT_SECTION, key, fav.actionCommand, true);

    snprintf(key, sizeof(key), "fav_%d_known", i);
    g_SetExtState(EXT_SECTION, key, fav.isKnown ? "1" : "0", true);
  }

  // Clear leftover entries
  for (int i = m_count; i < MAX_FAVORITES; i++) {
    snprintf(key, sizeof(key), "fav_%d_name", i);
    g_SetExtState(EXT_SECTION, key, "", true);
  }
}

bool FavoritesManager::Add(const char* name, const char* searchTitle, const char* actionCommand, bool isKnown)
{
  if (!name || !name[0]) return false;
  if (m_count >= MAX_FAVORITES) return false;

  // Check for duplicate
  if (FindByName(name) >= 0) return false;

  FavoriteEntry& fav = m_favorites[m_count];
  memset(&fav, 0, sizeof(FavoriteEntry));
  safe_strncpy(fav.name, name, sizeof(fav.name));
  safe_strncpy(fav.searchTitle, searchTitle ? searchTitle : name, sizeof(fav.searchTitle));
  if (actionCommand && actionCommand[0]) {
    safe_strncpy(fav.actionCommand, actionCommand, sizeof(fav.actionCommand));
    fav.toggleAction = ResolveActionCommand(actionCommand);
  } else {
    fav.toggleAction = 0;
  }
  fav.isKnown = isKnown;
  fav.used = true;
  m_count++;

  Save();
  DBG("[ReDockIt] FavoritesManager: added '%s' (count=%d)\n", name, m_count);
  return true;
}

void FavoritesManager::Remove(int index)
{
  if (index < 0 || index >= m_count) return;

  for (int i = index; i < m_count - 1; i++) {
    m_favorites[i] = m_favorites[i + 1];
  }
  m_count--;
  memset(&m_favorites[m_count], 0, sizeof(FavoriteEntry));

  Save();
}

const FavoriteEntry& FavoritesManager::Get(int index) const
{
  static FavoriteEntry empty = {};
  if (index < 0 || index >= m_count) return empty;
  return m_favorites[index];
}

int FavoritesManager::FindByName(const char* name) const
{
  if (!name) return -1;
  for (int i = 0; i < m_count; i++) {
    if (m_favorites[i].used && strcmp(m_favorites[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}
