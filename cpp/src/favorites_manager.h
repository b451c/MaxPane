#pragma once
#include "config.h"

struct FavoriteEntry {
  char name[256];
  char searchTitle[256];
  int toggleAction;           // resolved numeric action ID (runtime)
  char actionCommand[128];    // stable command string ("_RSxxx" or "12345")
  bool isKnown;
  bool used;
};

class FavoritesManager {
public:
  FavoritesManager();

  void Load();   // from ExtState
  void Save();   // to ExtState

  bool Add(const char* name, const char* searchTitle, const char* actionCommand, bool isKnown);
  void Remove(int index);
  int GetCount() const { return m_count; }
  const FavoriteEntry& Get(int index) const;
  int FindByName(const char* name) const;

private:
  FavoriteEntry m_favorites[MAX_FAVORITES];
  int m_count;
};
