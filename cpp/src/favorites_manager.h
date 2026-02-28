#pragma once

static const int MAX_FAVORITES = 32;

struct FavoriteEntry {
  char name[256];
  char searchTitle[256];
  int toggleAction;       // REAPER action ID (0 = arbitrary, no auto-open)
  bool isKnown;
  bool used;
};

class FavoritesManager {
public:
  FavoritesManager();

  void Load();   // from ExtState
  void Save();   // to ExtState

  bool Add(const char* name, const char* searchTitle, int toggleAction, bool isKnown);
  void Remove(int index);
  int GetCount() const { return m_count; }
  const FavoriteEntry& Get(int index) const;
  int FindByName(const char* name) const;

private:
  FavoriteEntry m_favorites[MAX_FAVORITES];
  int m_count;
};
