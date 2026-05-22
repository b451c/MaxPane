#include "instance_manager.h"

InstanceManager& InstanceManager::Get()
{
  static InstanceManager s_instance;
  return s_instance;
}

MaxPaneContainer* InstanceManager::GetOrCreate(int instanceId)
{
  if (instanceId < 0 || instanceId >= MaxPaneContainer::MAX_INSTANCES) return nullptr;
  if (!m_instances[instanceId]) {
    m_instances[instanceId] = std::make_unique<MaxPaneContainer>(instanceId);
  }
  return m_instances[instanceId].get();
}

MaxPaneContainer* InstanceManager::GetExisting(int instanceId) const
{
  if (instanceId < 0 || instanceId >= MaxPaneContainer::MAX_INSTANCES) return nullptr;
  return m_instances[instanceId].get();
}

void InstanceManager::DestroyAll()
{
  for (auto& p : m_instances) p.reset();
}

MaxPaneContainer* InstanceManager::GetFocused() const
{
  // If the focused slot has no live container, walk forward to find the
  // first one. Falls back to nullptr if no instance is open.
  MaxPaneContainer* c = GetExisting(m_focusedId);
  if (c && c->GetHwnd()) return c;
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    if (m_instances[i] && m_instances[i]->GetHwnd()) return m_instances[i].get();
  }
  return nullptr;
}

void InstanceManager::SetFocused(int id)
{
  if (id >= 0 && id < MaxPaneContainer::MAX_INSTANCES) m_focusedId = id;
}
