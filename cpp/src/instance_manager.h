#pragma once

#include "container.h"
#include <array>
#include <memory>

// Singleton owner of up to MaxPaneContainer::MAX_INSTANCES MaxPane containers.
//
// Instance 0 corresponds to the v1.5.x MaxPane — its ExtState, dock ident,
// and RPP chunk use the legacy names verbatim. Instances 1..N-1 use suffixed
// variants (see MaxPaneContainer constructor).
//
// Existing user data continues to load into instance 0 without migration.
class InstanceManager {
public:
  static InstanceManager& Get();

  // Returns the container for the requested slot, creating an empty one if
  // none exists yet. Does NOT call Create() — that's the caller's job.
  // Returns nullptr if instanceId is out of range.
  MaxPaneContainer* GetOrCreate(int instanceId);

  // Returns the container at this slot, or nullptr if not yet created.
  MaxPaneContainer* GetExisting(int instanceId) const;

  // Destroy every instance (used at plugin shutdown). After this any
  // GetExisting returns nullptr.
  void DestroyAll();

  // Iterate over slots that currently hold a container. Pred(int id, MaxPaneContainer&).
  template<typename Fn>
  void ForEach(Fn fn) {
    for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
      if (m_instances[i]) fn(i, *m_instances[i]);
    }
  }

  // Focused-instance tracking — used by F6 slot actions to know which
  // instance should load the workspace / capture the favorite. Updated on
  // any user interaction with a container (mouse click). Defaults to 0 so a
  // freshly-launched session routes to the legacy instance.
  int GetFocusedId() const { return m_focusedId; }
  MaxPaneContainer* GetFocused() const;
  void SetFocused(int id);

private:
  InstanceManager() = default;
  std::array<std::unique_ptr<MaxPaneContainer>, MaxPaneContainer::MAX_INSTANCES> m_instances;
  int m_focusedId = 0;
};
