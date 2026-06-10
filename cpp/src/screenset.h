#pragma once

// MaxPane screenset integration (v2.1).
//
// REAPER "Window sets" (screensets) persist which windows are open + their
// dock/position state, and can round-trip third-party extension windows ONLY
// if the extension registers a screenset callback via screenset_registerNew().
// Without it, recalling a window set re-opens MaxPane's captured windows as
// FLOATING (REAPER reopens the native windows but knows nothing about MaxPane's
// layout) — the user has to re-open MaxPane manually for them to snap back in.
//
// This is the screenset analogue of the project_config_extension_t hook MaxPane
// already uses for .rpp <MAXPANE_STATE> chunks: SAVE_STATE serializes the same
// tree+tabs blob, LOAD_STATE drives the existing restore funnel
// (ReloadProjectState / Create → LoadState). See ADR-050.
namespace MaxPaneScreenset {

// Register one screenset id per instance slot. Call once at plugin load,
// after the REAPER API pointers are resolved.
void RegisterAll();

// Unregister all ids. Call from onAtExit.
void UnregisterAll();

}  // namespace MaxPaneScreenset
