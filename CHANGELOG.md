# Changelog

All notable changes to MaxPane will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [2.3.0] - 2026-07-02

A full sweep of the open forum reports (15 issues from the release thread),
every fix verified live — Windows 11 and Ubuntu VMs plus macOS on real
3-display hardware. Headliners: the startup lag is gone, floating-window
position restore is fixed at the source on Windows, and four new features:
clean mode, track-FX-chain capture, follow mode, and pane border colors.

### Fixed

- **REAPER startup lag with MaxPane installed (up to minutes on Windows)
  is gone.** The startup cleanup of leftover window entries re-ran a full
  window-tree search for every entry on every timer tick — and on Windows
  additionally probed every process on the desktop. It now uses a cheap
  exact-title probe per tick with the deep search on just three sweep
  ticks. Measured on the same machine and saved state: cleanup work
  8.2 s → 1.1 s, and the per-tick cost stays far below the timer
  interval, so REAPER remains responsive while it runs.
  (Reported by poydepzaj1616.)
- **Windows: a floating MaxPane finally remembers its position.** The
  restore never worked on Windows at all: geometry messages that Windows
  fires during window creation overwrote the loaded position with the
  dialog template's default before it was ever applied. macOS doesn't
  fire those messages during creation, which is why the feature kept
  verifying as "working". Covers the repeated multi-monitor reports —
  verified on a physical 3-display setup including a monitor at negative
  coordinates. (Reported by todoublez.)
- **Windows: a maximized floating MaxPane restores maximized** — on the
  monitor it was on — and un-maximizing returns it to its exact previous
  rectangle. (Requested by mb945.) macOS/Linux keep size-only restore.
- **A deliberately closed second MaxPane instance no longer resurrects on
  every project load.** Project state now records whether the instance
  was open when the project was saved; closing an instance sticks after
  the next save. (Reported by LorenzoB.)
- **Two instances no longer fight over the same captured window.** The
  500 ms parent ping-pong (visible as flicker) is replaced by
  cross-instance arbitration: the instance that owns the window keeps it,
  the other drops its claim. (Reported by LorenzoB.)
- **FX windows restore with far less flicker when a project loads** — the
  capture queue no longer burns a fixed wait when the target window is
  already resolvable. The visible floating interval dropped from ~600 ms
  to under ~200 ms. (Reported by LorenzoB / sebsteeno.)
- **"Open MaxPane automatically on startup" now does what it says.**
  Previously the checkbox was silently ignored unless MaxPane was also
  open at last quit. Ticked = always opens, unticked = never; settings
  never touched keep the old restore-last-state behavior. An instance
  that opens before REAPER's docker layout exists now re-docks itself
  instead of ending up as a small window behind the main one.
  (Reported by mb945.)
- **Workspaces recall toolbars even when they aren't already open.**
  Startup cleanup raced the workspace recall and toggled the toolbar
  straight back off. (Reported by poydepzaj1616.)
- **Releasing a captured toolbar can no longer fire one of its buttons**
  mid-gesture. (Reported by poydepzaj1616.)
- **Windows: left-click-dragging a captured ReaImGui window's background
  no longer slides it out of its pane.** The move is now vetoed at the
  source, so the window behaves like a natively docked one; input stays
  correct. On Linux the existing snap-back (within ~0.5 s) still applies;
  macOS unchanged. (Reported by X-Raym.)
- **Script windows that close and reopen their own sub-windows keep their
  pane.** A captured tab whose window disappeared used to be ejected;
  it now waits and re-captures automatically when the script reopens the
  window — e.g. a sampler opening per-pad FX from its own UI.
  (Reported by mequaz.)
- **Stale gray strips and flicker in panes are painted over.** When a
  captured window is smaller than its pane (a window that refuses the
  resize), the remainder of the pane is now filled instead of showing
  stale pixels; the tab-bar strip also no longer lingers when a pane's
  header is hidden. (Reported by poydepzaj1616.)
- **Tooltips render at the correct size under Windows display scaling**
  (e.g. 125%). (Reported by X-Raym.)

### Added

- **Capture a whole track FX chain in one click.** Pane context menu →
  "Capture track FX chain": every FX of the last-touched track lands in
  the pane as tabs. If the chain is longer than the pane's free tab
  slots, MaxPane captures what fits and says so.
  (Requested by sebsteeno.)
- **Follow mode (experimental).** Settings → "Follow selected track's FX
  in pane 1": pane 1 releases and re-captures the FX chain as you move
  between tracks (debounced, so arrow-key surfing doesn't churn).
  Follow-mode tabs are transient by design — they are never written into
  workspaces or project files. (Requested by Rodulf and sebsteeno.)
- **Clean mode.** Settings → "Hide pane tab bars (clean mode)": occupied
  panes drop their tab bars entirely (on macOS an 8 px sliver remains as
  the mouse surface), giving captured windows every pixel; empty panes
  keep the full header and capture UI. The follow-mode pane keeps its tab
  bar — the tabs are its switcher. (Requested by poydepzaj1616,
  LorenzoB, and others.)
- **Pane border color.** Settings → "Pane border": seven presets
  (Default / Graphite / Slate blue / Teal / Amber / Crimson / Violet),
  applied live. (Requested by poydepzaj1616.)
- **Always-on-top is now a bindable action** — "MaxPane: Toggle
  always-on-top (floating mode)", same toggle the floating context menu
  has had since v2.0. (Requested by mb945.)
- **Windows: "Hide floating MaxPane from the taskbar"** Settings option.
  Trade-off (by Windows design): the window also leaves the Alt-Tab
  list. (Requested by mb945.)

### Known limitations

- **Maximize persistence, taskbar-hide, and the ReaImGui drag-veto are
  Windows-only.** macOS/Linux restore floating size/position without the
  maximized flag; taskbar-hide has no portable equivalent; on Linux a
  dragged ReaImGui window snaps back within ~0.5 s instead of being
  vetoed, and on macOS the v2.2.1 ReaImGui known limitation still
  applies.
- **Linux: the always-on-top toggle is a no-op** — SWELL on Linux does
  not implement the topmost style.
- **Linux: HiDPI tooltip scaling is not implemented yet** (tooltips
  assume a 1.0 scale factor).
- The v2.2.1 limitations still stand: Linux GL ReaImGui capture requires
  ReaImGui's "Disable hardware acceleration", and macOS ReaImGui scripts
  that enforce a minimum window size can resize REAPER's main window
  (see [2.2.1] below).

## [2.2.1] - 2026-06-11

ReaImGui script windows (TK Patchbay and friends) get a definitive,
per-platform resolution, including two crash fixes found on Linux minutes
after v2.2.0 — this is the release v2.2.0 should have been. Verified in
live owner sessions on Ubuntu and Windows 11 VMs and on macOS.

### Fixed

- **Linux: closing a captured script tab could crash REAPER.** Scripts
  destroy their own window inside the close toggle; the release path then
  touched the freed window. All release paths now re-validate the window
  after the toggle.
- **Linux: capturing a GL-rendered ReaImGui window (e.g. TK Patchbay)
  killed REAPER** on the script's next frame — reparenting destroys the
  X11 window under the script's GL context. Capture now works when
  ReaImGui's "Disable hardware acceleration" preference is enabled
  (Preferences > Plug-ins > ReaImGui; restart the script after changing
  it); otherwise MaxPane refuses the capture and tells you exactly that
  instead of crashing.
- **Windows: captured ReaImGui windows were mouse-dead** — they rendered
  but ignored the mouse, because the script kept hit-testing against its
  pre-capture screen position. Captured script windows are now fully
  interactive (verified live with TK Patchbay).
- **Windows: closing a captured untitled ReaImGui script could launch the
  ReaImGui demo** — action discovery matched the shared renderer module
  name. Discovery is now skipped for untitled ReaImGui windows on all
  platforms.
- **A script window enforcing its own minimum size no longer overflows its
  pane**, covering the splitter and the neighboring tab bar. MaxPane
  learns the enforced minimum at runtime (Windows/Linux); below it the
  window hides and the pane explains: "needs at least WxH px — enlarge
  this pane to show it."
- **Captured ImGui windows dragged by their background no longer slide out
  of their pane** — the position snaps back within a tick.
- The floor-hidden pane hint no longer smears during resize or shows the
  previous tab's stale last frame underneath.
- **"Release Window" works for captured ReaImGui windows on Linux** — the
  window returns floating and alive.

### Added

- **Capture refusals now explain themselves.** When MaxPane declines to
  capture a window (e.g. the Linux ReaImGui hardware-acceleration case), a
  message says why and what to do, instead of silently doing nothing.

### Known limitations

- **Linux: capturing GL ReaImGui windows requires ReaImGui's "Disable
  hardware acceleration"** (Preferences > Plug-ins > ReaImGui). A script
  started before the preference was enabled keeps hardware rendering for
  its lifetime — restart the script after enabling.
- **macOS: ReaImGui scripts that enforce a fixed minimum window size
  (e.g. TK Patchbay) can resize and move REAPER's main window** when
  captured into a pane smaller than that minimum. This is structural in
  ReaImGui's macOS backend: its size enforcement targets the hosting
  window, which after capture is REAPER's own. Workaround: dock such
  scripts natively in REAPER's docker, or keep the MaxPane pane at least
  as large as the script's minimum. Scripts without size constraints
  (e.g. ReaMD) are unaffected.

## [2.2.0] - 2026-06-10

Windows and Linux reach interaction parity: capture-by-click and
drag-to-dock work and show live visual feedback on all three platforms,
verified in live debug sessions on Windows 11 and Ubuntu VMs. Plus a
hardening pass from a full-code audit: capture regressions fixed, silent
failures surfaced, chronic background work trimmed.

### Added

- **Live visual feedback for capture and drag on Windows and Linux.**
  Arming click-capture now outlines the window under the cursor with a blue
  frame (as on macOS), and drag-to-dock draws a frame around the exact drop
  zone — including over panes already occupied by a captured window, where
  the painted preview was invisible. Zone changes settle briefly (~64 ms)
  before the frame moves, so the preview never flickers and the drop always
  lands exactly where the frame shows.
- **Natural title-bar drags.** Drag-to-dock aiming is now hybrid: the
  cursor keeps priority (precision aiming for splits), but when it isn't
  over any pane and the dragged window's body hangs over one — typical when
  dragging a window by its OS title bar — that pane is targeted as
  "Add as tab" (Shift = replace). All platforms.
- **MIDI toolbars are first-class capturable windows.** All 16 MIDI
  toolbars and the MIDI piano-roll toolbar now resolve to their real toggle
  actions, capture cleanly by click or menu, and survive save/restore.
- **Collapsible navigation bar.** A new chevron at the right end of the nav
  bar folds it down to a thin strip, giving your panes the space back; click
  the strip to expand it again. The state persists across restarts. The
  Settings "Show navigation bar" full-hide is unchanged.
- **Single-window panes can hide their tab bar.** New Settings checkbox
  "Collapse tab bar when a pane has a single window": the bar shrinks to a
  thin colored strip and the captured window gets the space. The strip stays
  interactive — right-click opens the tab menu, dragging it moves the tab —
  and the full bar returns automatically when a second tab arrives.
  (Requested by poydepzaj1616 on the forum.)
- **Right-click now cancels capture mode on all platforms.** On Linux there
  was previously **no way to cancel** an armed click-capture — Esc isn't
  detectable there, so the only way out was to capture something. The
  "capture a window" hints now advertise the right gesture per platform
  ("Esc or right-click to cancel"; "right-click to cancel" on Linux).

### Changed

- **Failures are no longer silent.** A failed capture, a window that can't be
  re-captured when a workspace or project loads, a corrupt saved layout
  (now: "layout reset" message instead of a wordless empty launcher), and
  saving when all 32 workspace slots are full (previously dropped the save
  while still reporting "Saved") now show a message instead of doing nothing.
  A failed capture also no longer makes the original window disappear — it
  stays where it was.
- **Less chronic background work.** Saved cleanup entries for windows that no
  longer exist (e.g. an uninstalled script or removed toolbar) used to be
  re-probed at every future startup, forever — they are now dropped once the
  startup cleanup window closes. A tab whose window went away (e.g. a closed
  MIDI editor) used to pay a full window-tree scan twice a second forever; it
  now backs off after repeated misses and recovers instantly on re-capture.
- **Internal:** the stale-list and saved-capture parsers — previously
  hand-rolled in several places with mismatched buffer sizes — are unified
  into single unit-tested modules; CI now builds with a warning gate
  (`-Werror`), and the release workflow runs the test suite before packaging
  binaries.

### Fixed

- **Windows: capture-by-click and drag-to-dock work again.** Both were
  effectively dead since the v2.1.0 capture redesign (which was only
  compile-verified on Windows): the click resolver accepted only an
  allow-list of known windows, and quick taps fell entirely between input
  polls. Verified live on a Windows 11 VM. (ADR-058)
- **Linux: docked windows capture by click, floating script windows
  resolve, and clicks respect visual stacking.** SWELL's hit-testing walks
  windows in list order, not stacking order, so a floating window hovering
  over the main window was previously unreachable by click or drag; clicks
  on X11 title bars (which belong to the window manager, not the window)
  now resolve via a decoration-band fallback. (ADR-059)
- **The Main-toolbar ghost cycle is terminally closed.** The exclusion
  shipped earlier in this cycle left one path alive (a workspace slot-merge
  could re-adopt the toolbar), and the startup cleanup now also closes an
  existing ghost window outright. (ADR-056)
- **Capturing a MIDI toolbar from a docker no longer freezes all clicks**
  (a stuck mouse capture in the toolbar click filter). (ADR-057)
- **The Main toolbar can no longer be captured.** Capturing REAPER's main
  toolbar produced a floating duplicate ("ghost") at every startup that
  MaxPane's cleanup couldn't see. It is now excluded from every capture path
  (menus, click-capture, drag-to-dock), and an already-floating Main-toolbar
  ghost left by an earlier version self-heals at the next startup. The
  toolbar action table was also repaired: "Toolbar 9" through "Toolbar 16"
  fired the wrong actions (including MIDI toolbars), and Toolbars 17–32
  (added in REAPER 7) are now supported. (ADR-052)
- **Capture-by-click works again for floating ReaImGui / script windows on
  Windows and Linux** (e.g. TK Patchbay). A v2.1.0 regression misclassified
  any floating window owned by REAPER's main window as "embedded in the main
  window" and silently rejected the click. (ADR-053)
- **Esc now cancels tab-drag and drag-to-dock on macOS** — it only ever
  worked on Windows.
- **Linux:** the support and update links now open in the browser
  (`xdg-open` — they previously did nothing); released / detached windows get
  their title bar and frame back instead of coming back borderless and
  unmovable; dark-mode "Auto" follows the GNOME/GTK setting instead of always
  rendering light (KDE still falls back to light); window geometry saved on a
  since-disconnected monitor is clamped back on-screen.
- **Updater hardening:** quitting REAPER during a slow update check can no
  longer crash at exit (the background check is now cleanly stopped at
  unload), and update-check responses are capped at 1 MB.
- **Windows:** the Quick Switcher's favorite-entry marker no longer renders
  as mojibake.

*Capture, drag and their visual feedback were verified live on Windows 11
and Ubuntu VMs for this release; macOS is the daily-driver platform. Known
platform quirk: on Linux/X11 a bare title-bar click can't always be
attributed to its window (the title bar belongs to the window manager) —
clicking window content always works.*

## [2.1.1] - 2026-06-08

**Quick fix** for a regression in v2.1.0.

### Fixed

- **Docked toolbars no longer disappear when their pane is made small.** The
  v2.1.0 ReaImGui size-guard — which stops large ImGui windows (e.g. TK Patchbay)
  from shrinking REAPER's main window — was too broad: it floor-hid *any*
  captured "arbitrary" window below ~200px, including toolbars. A docked toolbar
  is legitimately small. The guard (dock min-clamp + pane floor-hide) is now
  scoped to actual ReaImGui / Lua-gfx hosts only — toolbars, FX windows, and
  other native captures are never hidden and can be docked as small as you like.

*Verified on macOS (arm64); Windows and Linux binaries built by CI, not yet
runtime-tested for this release.*

## [2.1.0] - 2026-06-08

**Feature + stability release.** MaxPane's layout now round-trips through REAPER
"Window sets" (screensets) — recalling a set restores MaxPane and re-captures
its windows instead of leaving them floating. Plus the full v2.0.6 bugfix cycle
(manager-window float-on-close, restore redraw, ReaImGui collapse for ReaMD,
floating geometry, toolbar release, Windows startup lag, and more) and a
dark-mode "Auto" fix. Bumped to 2.1.0 (minor) for the screenset feature.

**Platform testing:** verified on **macOS (arm64)**. The Windows and Linux
binaries are produced by CI but have **not yet been runtime-tested** for this
release — please report any issues on the forum or GitHub.

> Most of the *Fixed* and *Changed* items below were developed as the
> **v2.0.6 bugfix cycle** — triaged from forum thread 307267 (page 2) after the
> 2.0.5 release — but v2.0.6 was never released on its own. They ship here,
> folded into 2.1.0, alongside the new screenset feature and the dark-mode fix.

### Added

- **Screenset integration.** MaxPane now registers a REAPER screenset callback,
  so saving and recalling a Window set restores MaxPane's pane layout and
  re-captures its windows automatically. Previously a recalled Window set
  re-opened the formerly-captured windows *floating* (REAPER reopened the
  native windows but knew nothing about MaxPane), and you had to re-open
  MaxPane by hand for them to snap back in. The screenset blob reuses the same
  serialization as the project `<MAXPANE_STATE>` chunk and drives the same
  restore path; it defers to a project-load restore when one is in flight so
  the two never fight. (reported on the forum by DerTonmeister, todoublez)

### Fixed

- **Dark mode "Auto (follow system)" now follows the macOS system setting.** It
  was stuck on the light palette even in system Dark mode, because REAPER ships
  with `NSRequiresAquaSystemAppearance` and forces an Aqua app appearance;
  MaxPane was reading that forced appearance. It now reads the OS-wide
  appearance (`AppleInterfaceStyle`) directly. (Force dark / Force light were
  unaffected.)
- **Manager windows no longer left floating when MaxPane closes.** Capturing
  Routing Matrix / Track Manager (or other toggle-action manager windows) and
  closing MaxPane could leave them floating, because the toggle and `WM_CLOSE`
  both no-op while the window is MaxPane's child. Release now retries the toggle
  after detaching so REAPER's visibility state is re-synced.
- **Tab bar and pane dividers no longer blank after reopening** until you grab a
  splitter — the container view is now force-laid-out and redrawn at the end of
  the restore.
- **Tab-bar hover works when a captured FX/plugin tab has focus.** A focused
  plugin window used to steal the mouse-moved stream; the container now has an
  always-active tracking area.
- **Captured ReaImGui windows no longer shrink REAPER's main window** when their
  pane is made small (fully fixed for ReaMD and similar; see Known limitations
  for large-UI scripts like TK Patchbay).
- **TK Patchbay (and other ReaImGui scripts) no longer crash when their pane is
  minimized.** A captured ReaImGui window in a pane shrunk to zero area is now
  hidden rather than sized into an invalid state that could trip an
  `ImGui_EndChild` assertion. (reported on the forum by Rodulf)
- **Floating MaxPane position / size / monitor persists** across Cmd+Q and close.
- **Releasing a captured toolbar no longer fires its buttons.**
- **Windows:** reduced the 10-15s startup lag (module-enumeration caching).
- **macOS:** ReaImGui / script windows (empty native title) now appear in the
  "Open windows" capture menu, matching Windows.
- **Nav bar no longer shows a grey un-painted band** when the container is narrow.

### Changed

- **"Release Window" returns the captured window to REAPER visible (floating)**
  for all window types — "Close Tab" remains the separate destructive action.
- **Capture-by-click redesigned** — modal-dialog and core-window safety guards
  (can no longer freeze REAPER by grabbing a modal, or tear out the main edit
  view), a crosshair cursor while armed, a hover preview of the target's name +
  outline, and Esc to cancel.
- Removed the tab "Close Others / Close to Right / Close All" submenu (over-
  engineered for MaxPane's tab counts); the launcher "Capture a window" button
  now stays visible while you arm a click-capture.

### Known limitations

- **ReaImGui windows with large UIs (e.g. TK Patchbay)** can still shrink
  REAPER's main window if their MaxPane pane is made very small. ReaMD and
  standard windows are fine. A per-window adaptive minimum is planned for v2.2.
- **A docked MaxPane placed on a second monitor** may return to the main monitor
  after a restart — REAPER manages docked-window position and does not reliably
  persist a second-monitor docker. Use MaxPane's **floating mode**, which
  remembers its monitor across restart.
- **Switching the macOS appearance while REAPER is running** does not live-update
  MaxPane; re-open Settings (or restart) to pick up the new Light/Dark setting.

## [2.0.5] - 2026-06-03

**Bugfix release. Captured plugin windows now restore into MaxPane when a saved project is opened (no longer come back floating). In-pane tab/plugin order is preserved across restore. Windows capture-by-click now works for the MIDI editor.**

### Fixed

- **Captured windows restore into MaxPane on project open (no longer float).**
  Opening a saved project whose MaxPane had docked FX could bring the plugin
  windows back floating outside MaxPane, with MaxPane closed or showing the
  empty launcher. A project stores its MaxPane layout in per-project state,
  and that path had no trigger to open MaxPane on load (only the in-`.rpp`
  chunk did). MaxPane now opens and restores its captured windows whenever
  the opened project carries MaxPane state — whether MaxPane was closed or
  already showing the launcher when you open the project. (reported on the
  forum by todoublez)

- **In-pane tab order is preserved across save → restore.** When a pane
  held several captured windows or plugins — most visibly FX across
  multiple tracks — the tabs could come back in a different order after
  a workspace or project reload. Cause: windows that must be reopened
  are re-captured through an asynchronous queue and landed in
  capture-completion order rather than the saved order, and the
  scrambled order was then written back to disk (making it sticky).
  Restore now stashes the saved order for any pane filled
  asynchronously and re-sorts its tabs back to that order — keyed on the
  stable `(track GUID, FX GUID)` / window identity — once the queue
  drains, before saving. Tab colors and the active tab are re-derived
  from the corrected order, so those land on the right tabs too.
  (reported on the forum by Rodulf)

- **Windows: capture-by-click now works for the MIDI editor and other
  REAPER-native dynamic-title windows.** On Windows, clicking such a
  window in capture-by-click mode was silently dropped — the window has
  no toggle action and resolves to REAPER's own module, which the
  click resolver rejected — so you had to use the "Open Windows"
  submenu instead. Capture-by-click now accepts these recognised native
  windows, matching macOS and Linux behaviour.

### Known limitations

- After a captured plugin is restored, switching between tabs in a pane can
  briefly show a stale/grey plugin view until you click the tab again; the
  plugin redraws correctly on the second visit. A more complete redraw fix
  is a planned follow-up.

### Verify

This release needs user confirmation. If you reported one of the issues
above, please test whether it is fixed and report back on the forum —
and if a window still misbehaves, say which window and under what exact
steps (see the thread for what details help).

## [2.0.4] - 2026-05-26

**Four-feature follow-up to v2.0.3. AU/VST/JSFX plugin windows now save and restore via workspaces. Hotkey binding moved inline (no more hunting through 200k REAPER actions). Update check on REAPER startup is non-blocking. Settings dialog ~26% smaller.**

### Added

- **AU/VST/JSFX plugin window save and restore.** Capture a floating
  plugin UI into a MaxPane pane, save the workspace, restart REAPER —
  the plugin reattaches to its pane on workspace load. Identity uses
  the same `(track GUID, FX GUID)` pair REAPER itself stores in RPP
  `FXID {…}` blocks, so it survives FX slot reorder and plugin rename
  without re-capture. Also covers master-track FX and take FX (item
  FX). Cross-platform — single code path, no `#ifdef`.
- **In-MaxPane hotkey binding via native shortcut dialog.** Right-click
  a workspace card → "Bind hotkey" now opens REAPER's keystroke-capture
  modal scoped to that specific workspace slot, instead of dumping you
  into the full Actions dialog with 200k entries to filter. REAPER
  handles capture, conflict detection ("already bound to X — replace?"),
  and `reaper-kb.ini` write natively.
- **Async update check on REAPER startup.** A detached worker thread
  hits the ReaPack manifest on github.com off the main thread; if a
  newer version exists, a modal pops up shortly after startup with a
  one-click jump to the Releases page. REAPER startup is no longer
  blocked by the ~1-3 s HTTP round-trip the v2.0.3 manual button
  triggered. New "Automatically on REAPER startup" checkbox in
  Settings → UPDATES (default ON; toggle to disable).

### Changed

- **Settings dialog compact redesign.** ~380 → 280 px tall (~26%
  shorter). Tighter section gaps, "Color mode" label inlined to the
  left of the cycle button, HOTKEYS description shortened from 3 lines
  to 1, UPDATES split out as its own section. All control IDs and
  logic unchanged.

### Known limitations

- **Plugin restore needs the matching project open.** Track GUIDs and
  FX GUIDs are project-bound (this is how REAPER itself works — the
  GUIDs live inside the `.rpp` file). Workspace load reopens FX UIs
  only when the project that owns those tracks is currently loaded.
  Recommended workflow: open the project first, then load the
  workspace. The per-project state path (saving the project file with
  Cmd+S after capture) is the more direct option for project-bound
  layouts — REAPER's RPP `<MAXPANE_STATE>` chunk handles auto-restore
  when the project reopens.
- **Container FX (nested FX, REAPER 7.06+) not supported in v2.0.4.**
  Capturing the UI of an FX inside a container chain works at the
  time, but the identity isn't encoded in the workspace format yet.
  Re-add manually after restart. Top-level track FX and recFX (input
  FX) are fully supported. Tracked as a v2.1 candidate.
- **FX moved between tracks won't auto-restore.** If you drag a
  plugin from track 3 to track 5 between save and load, the strict
  track-GUID match means the workspace tab skips silently (a toast
  surfaces "FX missing: …"). Re-capture manually in the new location.
- **Old (v2.0.3 and earlier) workspaces need one-time re-capture for
  AU/VST tabs.** Legacy entries persisted as `arb:0:<plugin name>` —
  v2.0.4 reads them but can't resolve to a live FX instance. Open the
  workspace, re-capture each plugin once, save. Existing non-FX tabs
  (Mixer, FX Browser, toolbars, scripts) restore as before.
- **Plugin window scaling is plugin-side.** Most VST/AU plugin GUIs
  render at a fixed resolution and do not dynamically resize to fit
  the MaxPane pane they're captured into. The plugin window appears
  at its native size — if smaller than the pane, surrounded by
  whitespace; if larger, cropped to the pane viewport. MaxPane
  reparents the OS window but cannot force the plugin's framework
  (JUCE / VST3 SDK / iPlug2 / etc.) to redraw at a different scale.
  Resize the MaxPane pane to roughly match the plugin's preferred
  size, or use the plugin's own "Resize" / "Zoom" controls if it
  exposes them. Same limitation applies in REAPER's native FX float
  windows.

## [2.0.3] - 2026-05-26

**Polish hot-fix on top of v2.0.2. Sharpens the nav-bar icons on
Windows, surfaces version / license / support info in the Settings
dialog, and lets you check for new versions without leaving REAPER.**

### Added

- **About section in the Settings dialog.** Now shows the version
  number, MIT license, project URL, and the three support links
  (Ko-fi, Buy Me a Coffee, PayPal) inside the plugin UI — previously
  the support links were buried in the nav-bar Support popup and
  there was no version display anywhere.
- **"Check for updates" button** in the new About section. Hits the
  project's ReaPack manifest on github.com and shows a modal if a
  newer version is out (with a one-click jump to the Releases page).
  Manual-only for now — synchronous HTTPS GET briefly blocks the UI
  for ~1-3 s while the request completes. Automatic check-on-startup
  is planned for a follow-up release.

### Fixed

#### Windows
- **Nav-bar icons no longer render pixelated.** Win32 GDI's
  `StretchDIBits` default mode (`COLORONCOLOR`) drops pixels on
  downscale; now switches to `HALFTONE` + `SetBrushOrgEx` before the
  blit and restores the previous mode after. Bilinear-equivalent
  smoothing; same look as macOS Quartz and Linux LICE.

#### macOS
- **Nav-bar icons get high-quality interpolation.** `BlitBGRABitmapMacOS`
  now sets `kCGInterpolationHigh` before `CGContextDrawImage` —
  matches the Win32 HALFTONE smoothing for visual parity across all
  three platforms.

---

## [2.0.2] - 2026-05-26

**Adds Linux x86_64 + aarch64 binaries to the public Release. Fixes
the three issues Linux smoke surfaced (tofu nav-bar icons, empty
modal dialogs, ReaImGui / Lua scripts not restoring from workspace).
Unifies the nav-bar icon set across all three platforms — macOS and
Windows users will see a one-off icon refresh.**

### Added

- **Linux x86_64 and Linux aarch64 binaries** in the public Release.
  Closes the Linux gap left by v2.0.1; the FX Browser close crash
  (issue [#9](https://github.com/b451c/MaxPane/issues/9)) reproduces
  no longer on REAPER 7.69 (covered by an upstream WDL fix). Linux
  parity sprint validated capture / restore / workspaces / context
  menus on Ubuntu 24.04 aarch64.
- **Unified Phosphor icon set** in the navigation bar across macOS /
  Windows / Linux. Replaces the per-platform Unicode glyphs (which
  rendered as tofu on Linux Noto Sans). Crisp at 20 px, uniform
  hover/active tinting, no per-glyph font-size tuning.

### Fixed

#### Linux native (parity sprint)
- **Nav-bar icons no longer render as tofu (□).** SWELL on Linux
  resolves an empty `CreateFont` family to fontconfig's default
  (Noto Sans Regular on Ubuntu 24.04), which lacks the Unicode
  miscellaneous-symbol codepoints the toolbar used. Bitmap path
  via Phosphor Icons (MIT) embedded as alpha masks.
- **Settings / Save Workspace / Quick Switcher dialogs open with
  content.** REAPER's bundled SWELL doesn't substitute the
  `SWELL_DEF_DLGSCALE2` default the `_BEGIN2` macro emits, so
  every child control sized to zero. All three dialog TUs now use
  the explicit-scale `_BEGIN` form with a local
  `SWELL_DLG_WS_DEFAULT_SCALING` shim (the upstream macro
  references a symbol that's only defined under
  `SWELL_TARGET_OSX`).

#### Cross-platform
- **ReaImGui / Lua scripts whose runtime window title differs from
  the `Script: <filename>.lua` action name now save with the
  correct action and restore from workspace.** The token-scoring
  threshold for the toggle pool stays at ≥ 2 hits, but a separate
  script-pool track allows 1 hit, and a single-active-script
  fallback covers the "title shares zero tokens" case (e.g.
  `lyrics.lua` window titled "MIDI Lyrics"). Workspaces saved
  with `arb:0:<name>` from older builds still need a one-time
  re-capture to populate the action.

#### Windows (Entry 15 follow-up)
- **`FindWindowEnumProc` finds Direct2D plugins with empty
  `WindowText`.** v2.0.1 closed the save side of Entry 15 but
  missed the search side — when `GetWindowText` returned empty
  on Win32, the enum proc hard-skipped, so workspace-load couldn't
  locate the just-opened ReaBeat / Reamix window for capture.
  Now falls back to `TryGetAppNameFromModule` when the title is
  empty (Win32 only; SWELL macOS / Linux keep the v2.0 hard-skip).

### Known issues

- Linux: HiDPI scaling of the nav-bar icons not yet wired to
  REAPER's advisory scaling factor. The 2× icon source is
  selected when the target rect exceeds 38 px logical, which
  looks correct at 1× DPI; HiDPI testing pending.
- macOS / Windows users upgrading from v2.0.1 will see a one-off
  icon refresh in the nav bar (Phosphor pictograms replace the
  Unicode glyphs). Visual only — all functionality unchanged.

---

## [2.0.1] - 2026-05-25

**Adds Windows x64 to the public build matrix and fixes a class of
workspace-restore bugs that affected captured C++ plugins (ReaBeat,
Reamix) and ReaImGui scripts on every platform.**

### Added

- **Windows x64 binary** is now part of the public Release. Closes
  [#8](https://github.com/b451c/MaxPane/issues/8) (Windows mouse input
  not reaching the container) — see _Windows native_ below for the
  list of Win32-specific fixes.
- **Current workspace name in the navigation bar.** The center of the
  nav bar now shows the loaded workspace's name with a `•` dirty
  indicator when the layout has been changed since the last save.
  Hover to see the full name in a tooltip when ellipsis-truncated.
- **Async workspace Save.** "Save Workspace" is now non-blocking — the
  click frame returns immediately with a "Saving '<name>'…" toast and
  the ExtState writes flush on the next REAPER tick (becomes "Saved
  '<name>'" / "Replaced '<name>'" when done). Up to 2 queued saves
  collapse; quit-time flush prevents data loss. Eliminates the 100+ ms
  freeze on click for workspaces with many captures.
- **Plugin window action discovery.** Captures of third-party C++
  plugins and ReaImGui Lua scripts now find and persist the toggle/
  show action automatically, so saved workspaces reopen those windows
  on load — previously they saved without an action and silently
  failed to restore. Cross-platform; uses `kbd_getTextFromCmd`
  (REAPER 6.71+) for action-name scoring.

### Fixed

#### Windows native (B10 / issue #8)
- **Mouse buttons reach `DlgProc`.** `WM_NCHITTEST` now publishes its
  result via `SetWindowLongPtr(DWLP_MSGRESULT, …)` and returns `TRUE`
  per the Win32 `DialogProc` contract — previously `return HTCLIENT`
  was read as `TRUE`-handled with `DWLP_MSGRESULT` defaulting to 0
  (`HTNOWHERE`), so every `WM_LBUTTONDOWN` / `WM_RBUTTONDOWN` /
  `WM_CONTEXTMENU` got routed past us.
- **Captures stop freezing REAPER.** `DoCapture` no longer leaves
  windows with both `WS_POPUP` and `WS_CHILD` set simultaneously
  during reparent (`NtUserSetParent` parked the GUI thread).
- **Captured plugins lose their 3D frame.** `GWL_EXSTYLE` strip
  removes `WS_EX_WINDOWEDGE` / `WS_EX_CLIENTEDGE` / `WS_EX_STATICEDGE`
  / `WS_EX_DLGMODALFRAME` so dialog-class plugins (ReaBeat) sit
  flush in the pane.
- **Save / Settings / Quick Switcher dialogs open.** Native Win32
  `DialogBoxParam` now uses the plugin DLL's `HINSTANCE` (captured
  in `ReaperPluginEntry`) — previously `nullptr` resolved to the
  host EXE which doesn't carry MaxPane's resources, and the call
  returned `-1` silently.
- **Detach to floating works.** Native Win32 implementations of the
  Cocoa-helper API (`ApplyFloatingWindowChrome`, `IsSystemDarkMode`
  via the `AppsUseLightTheme` registry value, `OpenUrlPlatform`
  via `ShellExecuteA`, `ClampRectToVisibleScreen` via
  `MonitorFromRect`, …) — previously all `{}` stubs.
- **Capture-by-click no longer self-triggers** on the menu / button
  click that entered capture mode (rising-edge LMB detection) and
  walks parent chains correctly for **docked plugins** (returns
  the shallowest plugin-DLL-owned HWND instead of the dock frame's
  inner container).
- **Open Windows menu** stops listing foreign-process windows
  (Firefox, Program Manager) and REAPER internal UI children, while
  also surfacing empty-title Direct2D plugins via owning-DLL lookup.
- **GDI captures stop smearing on resize** (`WS_CLIPCHILDREN |
  WS_CLIPSIBLINGS` in the dialog template); Direct2D captures
  stop stacking pixel residue (`SWP_NOCOPYBITS` on arbitrary tabs).
- **`AUTOCHECKBOX` + ASCII em-dashes** in Settings / Quick Switcher
  `.rc` files (native `rc.exe` doesn't auto-toggle plain `CHECKBOX`;
  UTF-8 em-dash rendered as Latin-1 mojibake).
- **Debug log** lands in `%TEMP%\maxpane_debug.log` instead of the
  POSIX `/tmp` path that doesn't exist on Windows.

#### Cross-platform
- **ReaImGui crash on workspace switch fixed.** Scripts (action state
  `-1`) now get a `Main_OnCommand` close-fire before MaxPane reparents
  their window — previously the external `SetParent` left ReaImGui's
  internal `Docker` pointer stale and the next heartbeat crashed in
  `Docker::moveTo` with a null-deref.
- **Workspace restore for captured plugins / scripts.** Cross-platform
  action discovery (title-token scoring against
  `kbd_getTextFromCmd`) now finds the show/hide action for plugins
  with titled windows (ReaBeat, Reamix) and for Lua scripts with
  `RS…`-prefixed named commands (state `-1`). Saved workspaces that
  carry these captures actually reopen them on load.
- **Stale list across workspaces.** Every saved workspace's tab list
  contributes its action IDs to `stale_toggle_actions` so windows
  released by a workspace SWITCH (not still in any active pane)
  survive REAPER restart cleanly instead of returning as floating
  ghosts. State `-1` cleanup now sends `WM_CLOSE` + hide instead of
  silently skipping.
- **Auto-open survives Windows quit.** `was_visible` now persists
  the intent flag (`m_visible`) instead of `IsWindowVisible()` —
  the Win32 parent-chain teardown at quit hid the still-open
  container and overwrote `was_visible=0`.
- **Workspace save round-trips named commands** (`_RS…`). Both
  `GetActionCommandString` (adds the leading `_`) and
  `ResolveActionCommand` (retries with one prepended for backward
  compat) close the SDK contract gap.
- Home-overlay workspace card tooltips now appear (the timer was
  only armed in fresh launcher mode).
- `DoRelease` flips `WS_CHILD` → `WS_POPUP` after `SetParent(nullptr)`
  per MSDN, so released windows land as proper top-level instead of
  silent `WS_CHILD`-of-desktop.

### Known issues

- Linux remains pending — open blocker
  [#9](https://github.com/b451c/MaxPane/issues/9) (FX Browser close
  crash). Linux build is still produced as a workflow artifact for
  internal testing; not yet in the public Release.
- Old workspaces saved before v2.0.1 that carry plugin captures with
  no action info (`arb:0:<name>`) won't auto-restore. Re-save the
  workspace once with the new build to populate the action ID.

---

## [2.0.0] - 2026-05-22

v2.0 reworks the docking core for reliability, adds multi-instance /
floating containers / drag-to-dock / Quick Switcher, and surfaces the
things that used to be hidden in the right-click menu through a
persistent navigation bar.

> **macOS only for v2.0.0.** Windows and Linux builds will be added in
> a follow-up release after they pass smoke testing on those platforms
> — see [#8](https://github.com/b451c/MaxPane/issues/8) (Windows mouse
> input) and [#9](https://github.com/b451c/MaxPane/issues/9) (Linux FX
> Browser close) for the open blockers.

For an architectural overview of the codebase as it stands at v2.0, read
[ARCHITECTURE.md](ARCHITECTURE.md). For the locked v2.0 scope, decision
log, and per-bug investigation notes, the project keeps `docs/v2/V2_SCOPE.md`,
`docs/v2/V2_DECISIONS.md`, and `docs/v2/V2_PROGRESS.md` locally (gitignored
process docs).

### Added

#### Multi-instance (F2)
- **Up to 8 MaxPane containers** in one REAPER session, each with its own
  layout, captured windows, floating geometry, and project-state chunk.
  Instance 0 keeps the legacy ExtState / dock / RPP identifiers verbatim
  — upgraders from v1.5.x see no migration. Instances 1–7 use suffixed
  identifiers (`MaxPane_cpp_N`, `MaxPane_container_N`,
  `<MAXPANE_STATE_N>`).
- **Per-instance Open actions** — `MaxPane: Open Container` (legacy,
  instance 0), `MaxPane: Open Container 2` through `MaxPane: Open
  Container 8` (instances 1–7). Existing v1.5.x keybinds on the legacy
  action continue to work.
- **Workspaces shared, current state per-instance** — the workspace list
  (saved layouts) is a single user-level pool visible from every
  container; what a container is *currently showing* is its own per-
  instance state.

#### Floating mode (F1a)
- **"Detach to Floating" / "Re-dock"** in the pane menu turns the whole
  container into a top-level OS window with native chrome (title bar,
  close, resize). Geometry persists across sessions and float/dock
  transitions.
- **Always-on-top toggle for floating containers** (C5) — keeps the
  detached container above other apps when checked. Per-instance,
  persisted to ExtState.
- Multi-monitor clamping on macOS via `[NSScreen screens]`; centered-on-
  REAPER-main fallback on Win/Linux.

#### Quick Switcher (F4)
- **`MaxPane: Quick Switcher`** action opens a modal SWELL dialog with
  a search box and a unified list of all open tabs (across all
  instances), all workspaces, and all favorites. Fuzzy filter matches
  case-insensitive subsequence + scores prefix and consecutive hits.
  Enter activates, Esc closes. Bind your own hotkey in REAPER's
  Actions dialog.

#### Hotkey slots (F6)
- **32 workspace slot actions** — `MaxPane: Workspace Slot 1` through
  `MaxPane: Workspace Slot 32`. Bind a hotkey, hit it, the
  corresponding workspace loads into the focused (last-clicked) MaxPane
  container.
- **32 favorite slot actions** — `MaxPane: Favorite Slot 1` through
  `MaxPane: Favorite Slot 32`. Bind a hotkey, hit it, the favorite is
  captured into the focused pane.
- **MAX_WORKSPACES bumped 10 → 32** to match the new slot count and
  close a long-standing community ask.

#### Navigation bar + Home overlay + drag-to-dock (ADR-026)
- **Persistent navigation toolbar** at the top of every container:
  `[Home] | [Drag] [Switch] [Save] [Load▾] | [Settings] [Support]`.
  Buttons for the workspace actions that used to live only in the
  right-click menu, plus Settings and Support links. Toggle via
  Settings → "Show navigation bar". Dark/light palette follows the
  rest of the UI.
- **Home overlay** — click `[Home]` to overlay the launcher card grid
  on top of the current layout (non-destructive picker). Click a card
  to load that workspace; click empty space or press Esc to close
  without changing anything.
- **Drag-to-dock** — click `[Drag]`, then grab any REAPER window from
  outside MaxPane and drop it on a pane. Live preview renders during
  the drag showing the four edge zones (split left / right / top /
  bottom), the tab-bar zone (add as tab), and the body-center
  forgiving zone. Shift+drop replaces the active tab. Polling-based,
  no platform hooks — works identically on macOS / Win / Linux SWELL.

#### Mocna piątka — C-series UX wins (ADR-027)
- **C1 Reopen last closed tab** — `MaxPane: Reopen last closed tab`
  action restores the most recently closed tab. 16-entry per-container
  ring buffer, session-scoped (not persisted).
- **C2 Pinned tabs** — right-click a tab → Pin. Pinned tabs sort to
  the left of each pane, render with a bullet (•) prefix, and are
  exempt from "Close Others" / "Close to Right" / "Close All".
  Persisted in workspace saves.
- **C3 Close family for tabs** — right-click a tab → Close ▸ →
  "Close Others", "Close to Right", "Close All". Bulk close that
  respects pinned tabs.
- **C4 Workspace pickup** — single `MaxPane: Workspace pickup`
  action prompts for a slot number (1..32) and loads it. One hotkey
  reaches every workspace slot instead of 32 separate bindings.
- **C5 Always-on-top floating** — see Floating mode above.

#### Settings dialog (ADR-019)
- **Dedicated modal Settings dialog** reachable from the nav bar
  `[Settings]` button or right-click → Settings. Single-page layout
  with four sections: General (auto-open, show nav bar, default
  workspace), Appearance (dark-mode tristate cycle: Auto / Force
  dark / Force light), Hotkeys (link to REAPER Actions), Advanced
  (open actions, reset, version + support links).

#### Other UX
- **Custom Save Workspace dialog** — name input + clickable listbox
  of existing workspaces + dynamic status label ("Will replace
  existing workspace 'X'" / "Will save as new workspace") replaces
  the bare `GetUserInputs` textbox. Toast confirms the result.
- **Toast bar** — bottom-of-container 3-second feedback strip for
  non-fatal events (rename collision, duplicate at capacity,
  workspace saved, etc.). Fades out in the final 500 ms.
- **Workspace launcher hero** (ADR-014) — when a container is empty
  it renders a card grid of saved workspaces with mini layout
  previews. Click a card to load. Right-click for Load / Rename /
  Duplicate / Delete / Bind Hotkey. Footer carries support links.
- **Capture menu flattened** (ADR-020 / ADR-030) — the 15 known
  REAPER windows are reachable directly from the right-click menu's
  "Capture window ▸" submenu (one level), with no extra nesting on
  the most common path.
- **Accelerator hook** (ADR-029) — MaxPane action bindings now fire
  even when MaxPane has focus (or a captured pane has focus). v1.x
  required REAPER's main window to have focus first.

#### Build + test infrastructure (ADR-018 / ADR-023)
- **CI matrix** — `.github/workflows/ci.yml` builds and tests on
  macOS arm64, macOS x86_64 (cross-compile + Rosetta), Ubuntu, and
  Windows for every push to `v2`/`main` and every PR.
- **Unit tests** — Catch2 v2.13.10 vendored; 14 tests cover
  `SplitTree`, `WorkspaceManager` static serialization helpers,
  `safe_strncpy`, `ResolveActionCommand`. Run with
  `ctest --output-on-failure` from `cpp/build/`.
- **`ARCHITECTURE.md`** — full technical onboarding document for
  new contributors.
- **Perl-driven SWELL resgen step** added to CMake (ADR-023) for
  cross-platform `.rc` → `.rc_mac_dlg` conversion.

### Changed

- **Container teardown reworked** (ADR-011, B16/B17) — `WM_CLOSE`
  is now the primary close mechanism; the legacy `Main_OnCommand`
  toggle is a fallback for windows that ignore `WM_CLOSE`. The
  `stale_toggle_actions` ExtState list is merged across paths
  (workspace switch, mid-session close, atexit) so the next
  REAPER startup can close any ghost windows REAPER restored from
  its `wnd_vis` cache.
- **Startup ghost cleanup polls aggressively** (ADR-015) — every
  tick for the first ~75 ticks instead of a single-shot fire,
  cutting the visible flash from ~250 ms to ~30 ms on macOS
  restart.
- **Workspace launcher replaces empty-container UI** (ADR-013) —
  MaxPane opens to the workspace card grid instead of auto-
  restoring captured windows. `was_visible`-driven auto-restore
  was the source of many race conditions and didn't match REAPER's
  native docker behavior.
- **Workspace list is shared across instances; current state is
  per-instance** (ADR-012, supersedes part of ADR-009).
- **Settings dialog supersedes context-menu Auto-open toggle**
  (ADR-019). The legacy menu item handler is preserved so v1.5.x
  keybinds continue to work.
- **`MaxPaneIsDarkMode()` introduced** with cache invalidation
  so the Settings dialog's tristate dark-mode override takes
  effect live without restarting REAPER.

### Fixed

- **B1 — SetParent return value never checked.** Added
  `VerifySetParent` helper at all four call sites in
  `DoCapture` / `DoRelease` so post-reparent mismatches surface in
  the debug log instead of corrupting state silently.
- **B2 — `CheckAlive` doesn't verify parent.** Tabs whose captured
  HWND was externally reparented (e.g. REAPER moved the MIDI Editor
  to another docker on project tab switch) are now reclaimed via
  `DoCapture` or cleanly removed.
- **B3 — MIDI Editor recapture race.** Capturability guard between
  `FindReaperWindow` and `DoCapture` prevents acting on an HWND
  REAPER reparented or closed in the interim.
- **B4 — WM_DESTROY doesn't reparent captured children.** Container
  destruction triggered by REAPER (docker close / undock) now
  reparents captured children before tearing down so they don't
  become frameless ghosts.
- **B6 — Toolbar subclass cleanup.** `UnsubclassToolbar` fires on
  every `DoRelease` branch, including the B2 reclaim-fail branch.
- **B7 — Stale cleanup synchronous fallback** for windows REAPER
  hasn't yet opened from its `wnd_vis` cache.
- **B9 — Narrow tab close-button overlap.** New `TAB_CLOSE_MIN_WIDTH`
  threshold (90 px) hides the × when it would clip the tab name
  ellipsis. Layout-clamp `TAB_MIN_WIDTH` (60 px) unchanged.
- **B12 — Capture queue arbitrary unconditional toggle.** Arbitrary
  captures (toolbars, ReaImGui scripts) now respect an
  `alreadyOpen` guard before firing the toggle, mirroring the
  known-window path. Without this, loading a workspace whose
  scripts were already open closed them and burned 200 retries.
- **B13 — `DoRelease` reads toggle state AFTER `SetParent(nullptr)`.**
  SWELL races the `wnd_vis` reset to 0 even when the window is
  still visible. State is now sampled BEFORE the reparent.
- **B14 — `ShowWindow(SW_HIDE)` doesn't reliably orderOut**
  top-level NSWindows that SWELL recreated via
  `SetParent(nullptr)`. New `ForceHideWindow` helper calls
  `[NSView setHidden:YES] + [NSWindow orderOut:nil]`.
- **B15 — Startup-level stale cleanup.** Ghost windows REAPER
  restored from cached `wnd_vis` are now closed at REAPER startup
  even when MaxPane doesn't auto-open.
- **B16 — `WM_CLOSE` as primary close mechanism.** Replaces the
  unreliable `Main_OnCommand` toggle for windows that don't update
  `wnd_vis` after our reparent (Media Explorer, Actions, FX
  Browser, Undo History).
- **B17 — Toggle stale list merges, doesn't overwrite.** Workspace-
  switch stale entries are no longer dropped by a subsequent
  mid-session close.
- **B18 — Arbitrary capture with action=0 + empty cmd spammed
  retries.** Legacy workspace tabs without a resolvable action now
  probe once via `FindReaperWindow`; if not found, log SKIP and
  return. Killed a 10-second UI freeze + 414-line log spam per
  load.
- **B19 — `ForceHideWindow` hides REAPER main window.** Now only
  calls `orderOut` on NSWindows whose contentView equals the
  captured NSView (i.e. orphan NSWindows we own).
- **B20 — REAPER docker tab placeholder after capture.**
  `g_DockWindowRemove(target)` is called before the intermediate
  `SetParent` in `DoCapture` so REAPER's docker manager forgets
  the captured window.
- **B21 — Save Workspace leaves leftover keys in ExtState.**
  Slot reuse with a smaller layout pre-clears all
  `pane_N_tab_M_*` keys before writing valid panes; phantom panes
  from prior saves no longer resurrect.
- **B22 — Mixer prefix-match catches "Mixer Master".** Special-
  case in `FindWindowEnumProc`: when searching for `Mixer`,
  require exact title match.
- **B23 — Capture-by-click floating: NSView frame not synced.**
  `SetWindowPos(SWP_FRAMECHANGED|…)` after reparent (replacing
  `SWP_NOMOVE|SWP_NOSIZE`) so the view occupies the actual pane
  rect instead of its pre-capture screen coords.
- **B25 — Workspace list per-instance was wrong UX** (ADR-012):
  list is now shared across instances; only current state stays
  per-instance.
- **B27 — Frameless ReaImGui plugins on re-fire.** Capturing a
  third-party plugin (ReaBeat, reamix.me, ReaMD), closing it, and
  re-firing the plugin's action no longer produces a frameless
  window at the default position. New chrome-restore in
  `DoRelease` for `toggleAction == 0` captures: SetParent(null) +
  `ApplyFloatingWindowChrome` + `SetWindowPos` to the original
  rect before WM_CLOSE.
- **Home overlay right-click bug** (ADR-031) — right-click on a
  card now opens the card menu instead of leaking to the pane menu.

### Known issues

- **B8 — Frameless toolbar button misalignment** (issue
  [#6 comment thread](https://github.com/b451c/MaxPane/issues/6))
  persists for the "Frameless floating toolbar windows = ON"
  preference. Needs REAPER-side experimentation; deferred to a
  post-v2.0 patch.
- **Windows + Linux not in this release.** Builds compile on all 5 CI
  platforms but Windows mouse input
  ([#8](https://github.com/b451c/MaxPane/issues/8)) and the Linux FX
  Browser close path ([#9](https://github.com/b451c/MaxPane/issues/9))
  are open blockers. Binaries for those platforms will be added in a
  follow-up release after they're tested and the blockers cleared.
- **Linux symbol-font tofu in the nav bar** (Linux only). GTK default
  font lacks several of the Unicode glyphs used by MaxPane. Will be
  resolved alongside the Windows/Linux release follow-up (likely via
  bundled PNG icons).
- **B24 — Toolbars open offscreen after capture+release**. REAPER
  toolbar manager state inconsistency after our
  `DockWindowRemove`. v2.1 candidate.

---

## [1.5.2] - 2026-03-04

### Fixed
- **Crash when capturing Docker or MaxPane itself** — Added ancestor guard in `DoCapture()` that rejects any window in MaxPane's parent chain, preventing circular reparenting crash. Docker and MaxPane are also filtered from the Open Windows menu. ([#2](https://github.com/b451c/MaxPane/issues/2), [#4](https://github.com/b451c/MaxPane/issues/4))
- **MIDI Editor survives project tab switches** — New `dynamicTitle` mechanism stores a stable search prefix ("MIDI take:") instead of the exact window title. `CheckAlive` automatically recaptures the MIDI Editor when REAPER opens a new one after project switch or restart. ([#3](https://github.com/b451c/MaxPane/issues/3))
- **`was_visible` not cleared on close** — Added `was_visible=0` in `WM_DESTROY` handler as safety net for all window destruction paths. "Close Container" menu now calls `Toggle()` instead of `Shutdown()` directly. ([#5](https://github.com/b451c/MaxPane/issues/5))
- **Visual glitches** — Hide close button on narrow tabs (< 60px); splitter hover timer now uses full hit testing; hover state cleared when context menu closes; adaptive text color (black on bright tab colors like yellow/cyan). ([#7](https://github.com/b451c/MaxPane/issues/7))
- **Toolbar rendering and docking in MaxPane** — Toolbars can now be properly captured and displayed. Removed toolbar filter from `FindWindowEnumProc`. ([#6](https://github.com/b451c/MaxPane/issues/6))
- **Workspace switching: stale windows floating after restart** — Windows from previous workspaces (toolbars, Actions, FX Browser, ReaImGui scripts) no longer appear as floating windows after switching workspaces and restarting REAPER. State-specific cleanup: double-toggle for state=0 windows, ShowWindow fallback for stubborn HWNDs, skip-toggle for script actions (state=-1). ([#6](https://github.com/b451c/MaxPane/issues/6))
- **Toolbar icons disappearing on background click** — Subclassed captured toolbar windows to block REAPER's internal drag-to-undock behavior. Clicking and holding on toolbar background no longer causes icons to vanish. ([#6](https://github.com/b451c/MaxPane/issues/6))
- **Auto-detect toggle action** — `LookupToggleAction` now auto-detects the REAPER toggle action ID for any window title (toolbars + known windows), ensuring proper state management for windows captured via Open Windows menu.

---

## [1.5.1] - 2026-03-03

### Fixed
- **macOS dark mode support** — Pane background color now adapts to macOS dark/light appearance. Reparented windows (Track Manager, Routing Matrix, etc.) no longer show a light grey background when REAPER's dark mode is enabled. ([#1](https://github.com/b451c/MaxPane/issues/1))

---

## [1.5.0] - 2026-03-03

### Added
- **Solo/maximize pane** — Temporarily expand any pane to fill the entire container. Toggle via context menu (Solo Pane / Exit Solo). Full tree snapshot is saved and restored when exiting solo mode.
- **Tab reorder within pane** — Drag tabs left/right within the same pane to rearrange their order. Visual insertion indicator shows the drop position.
- **Splitter double-click reset** — Double-click any splitter bar to reset its ratio to 50/50.
- **Keyboard shortcuts via REAPER actions** — Five new actions registered in REAPER's Actions dialog, bindable to any key:
  - MaxPane: Next Tab
  - MaxPane: Previous Tab
  - MaxPane: Next Pane
  - MaxPane: Previous Pane
  - MaxPane: Solo Toggle

### Changed
- **TabEntry owned storage** — All string fields (`name`, `searchTitle`, `actionCmd`) are now owned `char[]` arrays instead of `const char*` pointers. Eliminates dangling pointer risk after tab moves/copies and removes the `FixTabPointers()` workaround.
- **ShiftTabsLeft helper** — Extracted repeated tab-shift-and-clear pattern into a single function, used by CloseTab, MoveTab, and CheckAlive.
- **InvalidateRect(…, FALSE)** everywhere — Semantically correct now that `WM_ERASEBKGND` is handled; avoids unnecessary erase flicker.

### Fixed
- **Open Windows menu validation** — Added `IsWindow()` check before using HWND from the open windows list, preventing crashes from stale window handles.

### Note
- Windows and Linux builds are included but have **not been tested**. Please report any issues at [GitHub Issues](https://github.com/b451c/MaxPane/issues).

---

## [1.4.0] - 2026-03-03

### Added
- **Region Render Matrix** added to known windows (action 41888).
- **Cross-platform support** — Windows x64 and Linux x86_64 builds now compile and are included in releases alongside macOS.
- `platform.h` — Central platform abstraction header replacing 5 scattered `#ifdef _WIN32` blocks.
- `CreateMaxPaneDialog()` — Portable dialog creation helper (native `CreateDialogIndirectParam` on Windows, `SWELL_CreateDialog` on macOS/Linux).

### Fixed
- **Windows compilation** — bridged `GWLP_USERDATA`/`SetWindowLongPtr` (Win64 names) and replaced SWELL-only `SWELL_CreateDialog` with portable helper.
- **Linux compilation** — suppressed SWELL `min`/`max` macro conflict with STL via `WDL_NO_DEFINE_MINMAX`.
- **Linker error on Windows/Linux** — `ForceViewLayoutAndDisplay()` now has an inline no-op for non-macOS platforms (Cocoa code is macOS-only).

### Changed
- Known windows reorganized into logical groups (Mixing & Routing, Browsing & Media, Regions, Editing, Monitoring, Instruments).
- Removed `SWELL_PROVIDED_BY_APP` from CMake global definitions — now set per-platform in `platform.h`.
- Removed CMake `get_target_property`/`list(REMOVE_ITEM)` hack for Windows.

### Removed
- **MIDI Editor** removed from known windows — cannot be toggled without an active MIDI item in the session.

---

## [1.3.0] - 2026-03-03

### Changed
- **Renamed project**: ReDockIt → **MaxPane** — all source, docs, CMake, actions, ExtState keys, RPP chunk tags updated.

### Added
- **Diagonal grid lines** in empty panes — subtle 45° lines on the pane background, disappear when a window is captured.

### Fixed
- **Pane background color** — captured windows (WS_CHILD) have no own background and inherited the parent's dark color. Fixed by setting `COLOR_PANE_BG = RGB(172,172,172)` as a neutral light gray.
- **DoRelease toggle for Actions window** — closing the Actions tab via [x] left REAPER's toggle state on (checkmark stayed). Root cause: SWELL destroys the NSWindow when a window becomes WS_CHILD, so `g_Main_OnCommand` couldn't find the window and opened a new one. Fix: `SetParent(nullptr)` to restore the NSWindow before toggling.
- **Hint text color** — "Click header to assign a window" was white on light gray after the background color fix. Changed to `RGB(80,80,80)`.

---

## [1.2.0] - 2026-03-02

### Added
- **Pane menu button (▼)** — 16 px button at the right edge of every tab bar. Left-click opens the pane context menu (same as right-click). Hit-test returns `-2`; hover highlights the button.
- `CalcTabBarLayout` shared helper — single source of truth for tab geometry used by `DrawTabBar`, `TabHitTest`, and `IsOnTabCloseButton`.
- `GetTabRect` helper — returns the screen rect for a given tab index; used for targeted invalidation on color change.
- `ExpandRect` static helper (both `container.cpp` and `container_input.cpp`) — in-place RECT union, skips empty src/dst to prevent dirty-rect corruption.

### Changed
- **Targeted `InvalidateRect`** — hover/drag operations now invalidate only the affected rect instead of the full window:
  - Splitter hover: union of old + new splitter rect
  - Tab/menu-button hover: union of old + new item rect via `GetTabRect` / inline button rect
  - Drag highlight change: union of old + new highlight pane rect
  - Drag cancel/end: source tab bar + old highlight pane rect
  - Tab color change: only that tab's rect via `GetTabRect`
  - Timer hover timeout: cached old hover rects only
- Tab area now reserves `PANE_MENU_BTN_WIDTH` (16 px) on the right for the menu button; tabs shrink accordingly.

### Fixed
- **`TabHitTest` x-bounds regression** — the menu-button check (`x >= paneRect.right - 16`) was firing for clicks *outside* the pane's x range (e.g. in the adjacent right-side pane), causing the pane context menu to appear instead of tab interactions. Fix: check `x < paneRect.left || x >= paneRect.right` before the menu-button test.
- `ExpandRect` now guards against empty `src` rect (`{0,0,0,0}`) in both call-sites, preventing the dirty rect from expanding into origin unnecessarily.

### Removed
- Scroll arrows (`<` / `>`) for tab overflow — tabs shrink when a pane is narrow (original behaviour). `m_tabScrollOffset`, `TAB_SCROLL_ARROW_WIDTH`, `TAB_OVERFLOW_THRESHOLD` removed.

---

## [1.1.0] - 2026-03-02

### Added
- Splitter hover highlights (white bar on mouseover)
- Tab hover highlights (lighten effect on mouseover)
- Per-project state persistence via RPP files (`project_config_extension_t`)
- `StateAccessor` abstraction for polymorphic state I/O (global, project, RPP)

### Fixed
- Workspace switch rendering: blank/artifact windows (e.g. Routing Matrix) after switching back
- Frameless floating windows when closing tabs via [x]
- Windows reappearing floating after REAPER restart despite being closed
- Shutdown lifecycle: proper toggle state check, reparent-before-toggle sequence
- Quit interception via hookcommand for reliable state save on macOS/Windows
- RepositionAll repaint: `SWP_FRAMECHANGED` + `InvalidateRect` after pane resize

### Changed
- Simplified to global docker model (removed per-project visibility switching)
- RPP state I/O now uses synchronous `project_config_extension_t` (replaced deferred timer)
- `LoadWorkspace` uses `ReleaseAll(false)` for smoother workspace transitions
- `DoCapture` forces frame recalculation via `SWP_FRAMECHANGED`

## [1.0.1] - 2026-03-01

### Fixed
- **Startup deadlock**: REAPER could hang when loading a project with MaxPane docked. The capture queue now defers `Main_OnCommand` calls during `LoadState()` to avoid calling REAPER APIs while the project is still loading.

### Changed
- Replaced raw `new`/`delete` with `std::unique_ptr` for safer resource management
- Replaced all `strncpy` calls with `safe_strncpy` helper for consistent null-termination
- Extracted magic numbers (colors, geometry, timing) into named constants in `config.h`
- Merged duplicate `FindWindowEnumProc`/`FindChildWindowEnumProc` into single function
- Debug logging is now conditional on `CMAKE_BUILD_TYPE=Debug` (no longer always enabled)
- Added `-Wshadow` and `-Wconversion` compiler warnings

### Removed
- Dead code: unused `BuildLists()` and `IsAnyCaptured()` methods

## [1.0.0] - 2025-03-01

### Added
- Native C++ REAPER extension (no script dependencies)
- Binary split tree layout engine with up to 16 panes
- Tabbed window management with drag-and-drop between panes
- 14 known REAPER windows with one-click capture
- Arbitrary window capture (including ReaImGui scripts)
- Dock frame detection for ReaImGui windows
- Async capture queue with retry logic
- Named workspaces (save/restore complete layouts)
- Favorites system with persistent action command strings
- Tab color palette (8 colors)
- 5 built-in layout presets
- Auto-open on REAPER startup (configurable)
- Full state persistence via REAPER ExtState
- Dockable container (integrates with REAPER's docker system)
- Context menus for panes and tabs
- Capture-by-click mode
- Conditional debug logging (Debug builds only)
- Cross-platform architecture via WDL/SWELL
