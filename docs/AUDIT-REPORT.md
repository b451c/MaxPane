# ReDockIT — Finalny raport audytu produkcyjnego (rev.3)

**Data:** 2026-03-02
**Wersja:** 1.1.0 (commit `be84959`)
**Audytorzy:** Claude Opus 4.6, Codex 5.3 (wyniki połączone, skrzyżowane i 3x zweryfikowane)
**Zakres:** 35 plików śledzonych przez Git · 5589 linii kodu źródłowego (cpp/src/)
**Cel:** Finalny research przed publikacją open-source społeczności REAPER
**Rewizje:** v1 → v2 (re-weryfikacja każdego znaleziska w kodzie) → v3 (korekty po cross-review Codex)

---

## Metodyka

Dwa niezależne audyty (Claude Opus 4.6 + Codex 5.3) przeprowadzone file-by-file na zamrożonym stanie repozytorium. Wyniki skrzyżowane, zduplikowane znaleziska połączone, false-positives odrzucone z uzasadnieniem. Każde znalezisko zweryfikowane pod kątem numeru linii i kontekstu SWELL/macOS.

**Re-weryfikacja (rev.2):** Po złożeniu raportu v1, każde znalezisko ponownie sprawdzone bezpośrednio w kodzie z analizą edge-cases. Korekty:
- **2.15 (HWND_TOP)** usunięte — `HWND_TOP` operuje w child z-order containera, nie na pulpicie; zmiana na `SWP_NOZORDER` złamałaby przełączanie tabów
- **2.16 (SaveState debounce)** przeniesione z WAŻNY do NICE-TO-HAVE — `SaveState()` wywoływane tylko po dyskretnych akcjach użytkownika (nigdy podczas mouse move), `g_SetExtState` jest operacją in-memory
- **Dodane 2.15 (nowe)** — `RecalcNode` cycle protection: stack overflow z cyklicznego snapshot (częściowo z Codex C4)

### Klasyfikacja priorytetów

| Priorytet | Definicja |
|-----------|-----------|
| **KRYTYCZNY** | Crash, UB, memory corruption, błąd funkcjonalny widoczny dla użytkownika |
| **WAŻNY** | Ryzyko utrzymania, defensywna poprawność, dead code, DX, performance z realnym wpływem |
| **NICE-TO-HAVE** | Styl, spójność, refaktor ergonomiczny, prewencja regresji |

---

## Mapa zależności

```
main.cpp
  ├── container.cpp  (orchestrator — 1455 linii)
  │     ├── split_tree.cpp      (layout engine)
  │     ├── window_manager.cpp  (capture/release/tabs)
  │     ├── capture_queue.cpp   (async capture + dock frame wait)
  │     ├── workspace_manager.cpp (state persistence)
  │     ├── context_menu.cpp    (menu construction)
  │     └── favorites_manager.cpp
  └── project_state.cpp  (RPP chunk I/O — extern coupling z container/main)
```

**Coupling hotspot:** `container.cpp` jest centralnym orchestratorem i zależy od wszystkich modułów. `project_state.cpp` ↔ `main.cpp` ma twarde sprzężenie via extern (`GetContainer`, `OnRppStateReady`).

---

## 1. KRYTYCZNE

### 1.1 · `activeTab` nie korygowany po usunięciu wcześniejszego taba

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 5. Bug funkcjonalny |
| **Pliki** | `window_manager.cpp:449`, `:503`, `:602` |

**Problem:** W `CloseTab`, `MoveTab` (source pane) i `CheckAlive` — po usunięciu taba i przesunięciu tablicy w lewo, `activeTab` nie jest dekrementowany gdy usunięty tab miał indeks mniejszy niż aktywny.

**Reprodukcja:** Pane z tabami `[A, B, C, D]`, `activeTab=2` (C). Zamknij tab 0 (A). Po shifcie: `[B, C, D]`, ale `activeTab` nadal = 2, więc wskazuje na **D** zamiast **C**.

Kod obsługuje tylko `activeTab >= tabCount`, ale nie `tabIndex < activeTab`:

```cpp
// window_manager.cpp:449-455 (CloseTab)
if (ps.tabCount == 0) {
    ps.activeTab = -1;
} else if (ps.activeTab >= ps.tabCount) {
    ps.activeTab = ps.tabCount - 1;
} else if (ps.activeTab == tabIndex && ps.activeTab >= ps.tabCount) {
    // ^^^ unreachable — already handled above
    ps.activeTab = ps.tabCount - 1;
}
```

**Fix:** Po array shift, przed sprawdzeniem `tabCount == 0`, dodać:

```cpp
if (tabIndex < ps.activeTab) {
    ps.activeTab--;
}
```

Dotyczy trzech miejsc: `CloseTab` (linia 449), `MoveTab` (linia 503), `CheckAlive` (linia 602).

---

## 2. WAŻNE

### 2.1 · `config.h` — statyczne tablice duplikowane w każdym TU

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 3. Zależności |
| **Plik** | `config.h:25-85` |

`KNOWN_WINDOWS[]`, `TAB_COLORS[]`, `PRESET_NAMES[]`, `PRESET_PANE_COUNT[]` — static const w headerze, każdy TU (7+) dostaje własną kopię w segmencie danych.

**Fix:** Przenieść definicje tablic do nowego `config.cpp`, w headerze zostawić `extern const`.

---

### 2.2 · `FAV_SECTION` duplikuje `EXT_SECTION`

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 10. Spójność |
| **Plik** | `favorites_manager.cpp:8` |

`static const char* FAV_SECTION = "ReDockIt_cpp"` — identyczne z `EXT_SECTION` w `config.h`.

**Fix:** Usunąć `FAV_SECTION`, użyć `EXT_SECTION` z `config.h`.

---

### 2.3 · Niespójne użycie `StateAccessor` w `workspace_manager.cpp`

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 10. Spójność |
| **Plik** | `workspace_manager.cpp:545-649` |

`SaveCurrentState`/`LoadCurrentState` poprawnie używają `GlobalStateAccessor`, ale `LoadList`/`SaveList` wołają `g_GetExtState`/`g_SetExtState` bezpośrednio — mieszane podejście w jednym module.

**Fix:** Ujednolicić — w `LoadList`/`SaveList` użyć `GlobalStateAccessor`.

---

### 2.4 · Surowy `strncpy` zamiast `safe_strncpy`

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 2. Jakość kodu |
| **Plik** | `workspace_manager.cpp:257` |

```cpp
strncpy(panes[p].tabs[t].actionCommand, afterArb, cmdLen);
panes[p].tabs[t].actionCommand[cmdLen] = '\0';
```

Działa poprawnie (manualne null-terminowanie), ale niespójne z resztą codebase która używa `safe_strncpy`.

**Fix:** Zamienić na `safe_strncpy`.

---

### 2.5 · Brak walidacji indeksów w inline accessorach `SplitTree`

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 6. Bezpieczeństwo |
| **Plik** | `split_tree.h:68-71` |

```cpp
const SplitNode& GetNode(int idx) const { return m_nodes[idx]; }
int GetPaneId(int nodeIdx) const { return m_nodes[nodeIdx].paneId; }
const RECT& GetPaneRect(int paneId) const { return m_paneRects[paneId]; }
```

Jeśli `idx = -1` (wartość sentinelowa), to UB — odczyt spoza tablicy. Callerzy walidują w praktyce (sprawdzone: `PaneAtPoint` sprawdza `nodeIdx < 0`, `RepositionAll` iteruje po leaf list, `context_menu` po leaf list), ale brak defensywnego checka na poziomie accessora. Ryzyko: przyszły caller bez walidacji.

**Fix:** Dodać `assert(idx >= 0 && idx < MAX_TREE_NODES)` w Debug buildach (zero overhead w Release).

---

### 2.6 · `TabEntry::originalStyle` / `originalExStyle` — nieużywane pola

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `window_manager.h:11-12`, `window_manager.cpp:324-325` |

Zapisywane w `DoCapture`, ale celowo nigdy nie przywracane w `DoRelease` (przywracanie produkuje frameless windows — to był bug naprawiony wcześniej).

**Fix:** Usunąć pola z `TabEntry` i kod zapisujący w `DoCapture`.

---

### 2.7 · `swell_modstub.cpp` — nieużywany plik

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `cpp/src/swell_modstub.cpp` |

Nie jest w `add_library()` w CMakeLists.txt. Na macOS używany jest `swell-modstub.mm` z WDL.

**Fix:** Usunąć albo dodać z komentarzem jako plik dla przyszłego Linux buildu.

---

### 2.8 · Hardcoded 512 w `state_accessor.h`

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 10. Spójność |
| **Plik** | `state_accessor.h:69` |

```cpp
const char (*m_lines)[512];  // should match RPP_MAX_LINE_LEN
```

**Fix:** Użyć `RPP_MAX_LINE_LEN` z `project_state.h` lub przenieść stałą do wspólnego miejsca.

---

### 2.9 · `m_shutdownGraceTicks` — ustawiane, nigdy odczytywane

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `container.h:69`, `container.cpp:34, 1416` |

Pole inicjalizowane na 0 w konstruktorze i ustawiane na `SHUTDOWN_GRACE_TICKS` (10) po capture, ale nigdy odczytywane — mechanizm grace period nie jest dokończony. Stała `SHUTDOWN_GRACE_TICKS` w `config.h:141` jest również martwa.

**Fix:** Dokończyć mechanizm (sprawdzać `m_shutdownGraceTicks > 0` przed shutdown) albo usunąć pole i stałą.

---

### 2.10 · `CheckAlive` — nieużywany parametr `containerHwnd`

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `window_manager.h:47`, `window_manager.cpp:583` |

```cpp
void WindowManager::CheckAlive(HWND containerHwnd)
```

Parametr `containerHwnd` nie jest używany w ciele funkcji. Wywoływane z `container.cpp:845` jako `m_winMgr.CheckAlive(m_hwnd)`.

**Fix:** Usunąć parametr z deklaracji, definicji i call-site.

---

### 2.11 · `PendingCapture::colorIndex` — nieużywane pole

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `capture_queue.h:17` |

Pole `int colorIndex = 0` w `PendingCapture` — nigdy odczytywane ani zapisywane poza domyślną inicjalizacją.

**Fix:** Usunąć pole.

---

### 2.12 · `MAX_RETRIES_STARTUP` — nieużywana stała

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `capture_queue.h:29` |

```cpp
static const int MAX_RETRIES_STARTUP = 600;  // ~30s for startup restore
```

Zdefiniowana ale nigdy nie referencjonowana w żadnym pliku.

**Fix:** Usunąć stałą.

---

### 2.13 · `g_ShowConsoleMsg` — nieużywany symbol globalny

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `globals.h:17`, `globals.cpp:6` |

```cpp
extern void (*g_ShowConsoleMsg)(const char*);  // deklaracja
void (*g_ShowConsoleMsg)(const char*) = nullptr;  // definicja
```

Przypisywane, ale nigdy wywoływane w codebase.

**Fix:** Usunąć z `globals.h`, `globals.cpp` i import w `main.cpp`.

---

### 2.14 · `atoi`/`atof` bez walidacji w parserach stanu

| | |
|---|---|
| **Źródło** | Codex + Claude (re-weryfikacja) |
| **Kategoria** | 6. Bezpieczeństwo danych wejściowych |
| **Plik** | `workspace_manager.cpp:104-291` (11 wystąpień), `favorites_manager.cpp:28, 63` (2 wystąpienia) |

Parsery stanu z RPP/ExtState opierają się na `atoi`/`atof` bez walidacji zakresów. Dane pochodzą z plików edytowalnych przez użytkownika (RPP, reaper-extstate.ini).

**Analiza wpływu:** `atoi` na błędny input zwraca 0 — brak crashu. `(SplitNodeType)atoi(val)` z wartością spoza enum to UB teoretyczne (w praktyce: nierozpoznany case w switch → domyślna ścieżka). `count` i `tabCount` mają clampy (linie 105-106, 549-550). **Brak clampów** dla: `childA`, `childB`, `paneId`, `parent` (linie 125-137), `ratio` (linia 121). Wartości `childA`/`childB` poza zakresem [0, MAX_TREE_NODES) nie powodują OOB w `RecalcNode` (bounds check na linii 290), ale cykliczne wartości (node 0→1→0) mogą spowodować stack overflow (patrz 2.15).

**Fix:** Dla wartości używanych jako indeksy tablicowe: `strtol` + clamp do [0, MAX). Dla `ratio`: clamp do [0.1, 0.9]. Dla enum castów: clamp do poprawnego zakresu.

---

### 2.15 · `RecalcNode` — brak ochrony przed cyklami w snapshot

| | |
|---|---|
| **Źródło** | Codex C4 (zweryfikowane w re-audycie) |
| **Kategoria** | 6. Bezpieczeństwo / robustność |
| **Plik** | `split_tree.cpp:288-316` (RecalcNode), `split_tree.cpp:481-519` (LoadSnapshot) |

`RecalcNode` jest funkcją rekurencyjną: `RecalcNode(node.childA, ...)` → `RecalcNode(node.childB, ...)`. Ma bounds check (`idx < 0 || idx >= MAX_TREE_NODES → return`), więc OOB jest bezpieczne. **Ale brak ochrony przed cyklami:** jeśli uszkodzony/edytowany RPP zawiera cycle (node 0 → childA=1, node 1 → childA=0), to nieskończona rekursja → stack overflow → crash REAPER.

`LoadSnapshot` waliduje tylko `childA != childB` (linia 485), ale nie sprawdza: zakresu indeksów, parent-child spójności, ani acykliczności grafu.

**Fix (minimalny):** Dodać parametr `depth` do `RecalcNode`:

```cpp
void SplitTree::RecalcNode(int idx, const RECT& area, int depth = 0)
{
  if (idx < 0 || idx >= MAX_TREE_NODES) return;
  if (depth > MAX_TREE_NODES) return;  // cycle protection
  // ...
  RecalcNode(node.childA, partA, depth + 1);
  RecalcNode(node.childB, partB, depth + 1);
}
```

**Fix (pełny):** W `LoadSnapshot` dodać walidację zakresu childA/childB i acykliczności (DFS z visited array).

---

### 2.16 · `list_windows.cpp` — niezarejestrowany hook

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 4. Martwy kod |
| **Plik** | `cpp/test_minimal/list_windows.cpp:47-58` |

Lambda `hookCmd` jest zdefiniowana ale nigdy nie zarejestrowana przez `plugin_register("hookcommand", ...)`. Narzędzie diagnostyczne faktycznie nie działa.

**Fix:** Zarejestrować hookcommand albo usunąć plik z repozytorium.

---

### 2.17 · README vs index.xml — niespójny status platform

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 8. DX |
| **Plik** | `README.md:138`, `index.xml:36-40` |

README mówi że Windows/Linux "planned", a `index.xml` publikuje gotowe binarki dla Win/Linux.

**Fix:** Ujednolicić status platform między README i ReaPack index.

---

## 3. MARTWY KOD (podsumowanie z 2.x)

| Plik | Linia | Element | Źródło |
|------|-------|---------|--------|
| `window_manager.cpp` | 453 | Nieosiągalny branch w `CloseTab` | Claude |
| `split_tree.cpp` | 468 | Nieosiągalny `break` w `SaveSnapshot` | Claude |
| `window_manager.cpp` | 177-212 | `DumpAllWindowTitles` no-op w release | Claude |
| `window_manager.h` | 11-12 | `originalStyle`/`originalExStyle` (2.6) | Claude |
| `swell_modstub.cpp` | cały plik | Nie w CMakeLists.txt (2.7) | Claude |
| `container.h` | 69 | `m_shutdownGraceTicks` write-only (2.9) | Codex |
| `window_manager.cpp` | 583 | `CheckAlive` unused param (2.10) | Codex |
| `capture_queue.h` | 17 | `PendingCapture::colorIndex` (2.11) | Codex |
| `capture_queue.h` | 29 | `MAX_RETRIES_STARTUP` (2.12) | Codex |
| `globals.h/cpp` | 17/6 | `g_ShowConsoleMsg` (2.13) | Codex |
| `list_windows.cpp` | 47-58 | Niezarejestrowany hook (2.16) | Codex |

---

## 4. NICE-TO-HAVE

### 4.1 · GDI brush caching w `OnPaint`

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 7. Performance |
| **Plik** | `container.cpp` — `DrawTabBar`, `DrawSplitters` |

`CreateSolidBrush`/`DeleteObject` co frame. Przy szybkim resize splitterów (drag → `InvalidateRect`) to setki alokacji GDI/s.

**Fix:** Cache brushy jako member fields, odtwarzać tylko przy zmianie kolorów.

---

### 4.2 · Targeted `InvalidateRect`

| | |
|---|---|
| **Źródło** | Claude Opus |
| **Kategoria** | 7. Performance |
| **Plik** | `container.cpp` |

`InvalidateRect(nullptr)` unieważnia cały container zamiast tylko dirty rect.

**Fix:** Obliczać dirty rect (splitter region / tab bar) i invalidować precyzyjnie.

---

### 4.3 · Tab overflow UI

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 5. UX |
| **Plik** | `container.cpp:683-690, 441-460` |

Stała szerokość tabów z clipping — przy wielu tabach (>6-8) taby stają się nieklikalne/ucięte.

**Fix:** Scroll tabów lub overflow menu; ujednolicenie geometrii draw + hit-test.

---

### 4.4 · `NOMINMAX` — prewencja regresji

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 10. Spójność |
| **Plik** | Globalnie — `globals.h`, `split_tree.h`, `config.h` |

Brak `NOMINMAX` to mina regresyjna — dziś `std::min`/`std::max` nie są użyte (własne clampy), ale przyszłe zmiany mogą je wprowadzić.

**Fix:** Dodać `NOMINMAX` globalnie w CMake lub standaryzować użycie helperów `clamp_i`/`clamp_f`.

---

### 4.5 · `container.cpp` — monolit (1455 linii)

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 1. Architektura |
| **Plik** | `container.cpp` |

Łączy lifecycle, UI paint, input handling, capture dispatch, menu dispatch, persistence. Opcjonalny rozbicie na `container_paint.cpp`, `container_input.cpp`.

---

### 4.6 · Niespójny copyright (LICENSE vs README)

| | |
|---|---|
| **Źródło** | Codex |
| **Kategoria** | 10. Spójność |
| **Plik** | `LICENSE:3`, `README.md:188` |

LICENSE: `2025`, README: `2025–2026`.

**Fix:** Ujednolicić do `2025–2026`.

---

### 4.7 · `SaveState()` — opcjonalny debounce

| | |
|---|---|
| **Źródło** | Codex (przeniesione z WAŻNY po re-weryfikacji) |
| **Kategoria** | 7. Performance |
| **Plik** | `container.cpp` — 19 call-sites |

`SaveState()` wywoływane po każdej operacji UI. **Re-weryfikacja:** wszystkie call-sites są event-driven (klik, drop, menu) — żadne NIE jest w ścieżce mouse-move/drag. `g_SetExtState` jest operacją in-memory (hash table update), dysk flush jest asynchroniczny w REAPER. Debounce dodałby złożoność (timer, edge cases shutdown) z minimalnym zyskiem.

**Fix (opcjonalny):** Dirty flag + timer flush. Robić tylko jeśli profiling pokaże realny problem.

---

## 5. ODRZUCONE FALSE-POSITIVES

| ID | Opis | Powód odrzucenia |
|----------|------|-------------------|
| **Codex C1** | `SetWindowLong`/`GWL_USERDATA` truncation na 64-bit | Na SWELL (macOS/Linux) `SetWindowLong` = `SetWindowLongPtr`, `GWL_USERDATA` = `GWLP_USERDATA` — aliasy, brak ryzyka truncation na aktualnej platformie. **Ale:** ścieżka `_WIN32` w `globals.h:3-4` includuje `<windows.h>` gdzie `SetWindowLong` operuje na `LONG` (32-bit) a nie `LONG_PTR` (64-bit). Jeśli build Win64 użyje `SetWindowLong` z pointerem, dojdzie do truncation. **Reklasyfikacja:** nie FP lecz **niskie ryzyko portability** — do naprawy przy uruchomieniu buildu Windows (wrapper `SetWindowLongPtr`/`GWLP_USERDATA` z `#ifdef` lub warunkowy alias). |
| **Codex C2** | `DoRelease` nie przywraca `originalStyle`/`originalExStyle` — "krytyczne" | **Celowe.** Przywracanie stylów produkuje frameless windows. To był bug naprawiony w commit `ce3a730`. Codex rekomenduje przywrócenie buga. |
| **Codex W4 / v1 2.15** | `HWND_TOP` z-order side effect w `RepositionAll` (`window_manager.cpp:573`) | **False positive.** `tab.hwnd` jest child window containera (via `SetParent` w `DoCapture`). `HWND_TOP` operuje w child z-order **wewnątrz rodzica**, nie na pulpicie. Aktywny tab MUSI być na wierzchu siblings — zmiana na `SWP_NOZORDER` złamałaby przełączanie tabów (ukryty tab mógłby przysłaniać aktywny). Zweryfikowane: kod działa poprawnie na REAPER 7.62. |

---

## 6. BEZPIECZEŃSTWO / PAMIĘĆ

**Brak memory corruption, memory leaks ani buffer overflows.** Luki robustności inputu opisane w 2.14 (atoi/atof bez clampów) i 2.15 (brak cycle protection w RecalcNode) mogą prowadzić do cichych błędów lub stack overflow przy uszkodzonych danych wejściowych — nie są to klasyczne security issues, lecz braki walidacji inputu (defense-in-depth).

Bezpieczeństwo pamięci:

- Wszystkie `snprintf`/`safe_strncpy` ograniczone buforami ✓
- `std::unique_ptr` dla `CaptureQueue`/`FavoritesManager`/`WorkspaceManager` ✓
- GDI obiekty tworzone i niszczone w tym samym scope ✓
- `HMENU` niszczone po `TrackPopupMenu` ✓
- `IsWindow()` sprawdzane przed operacjami na HWND ✓
- Brak `new` bez smart pointer ✓
- Brak buffer overflow ✓

Portability note: `SetWindowLong`/`GWL_USERDATA` z pointerem — bezpieczne na SWELL (macOS/Linux), wymaga wrappera dla Win64 (patrz sekcja 5, Codex C1).

---

## 7. STRATEGIA TESTOWANIA

Moduły z testowalną logiką host-agnostic:

| Moduł | Co testować | Wydzielenie |
|-------|-------------|-------------|
| `SplitTree` | Split/merge/hit-test/recalculate, snapshot round-trip, corrupt data | Już niezależny od REAPER API |
| `WorkspaceManager` serialization | `ReadTreeNodesStatic`/`WritePaneTabsStatic` round-trip | Mock `StateAccessor` |
| `WindowManager` tab logic | `CloseTab`/`MoveTab` index tracking (bug 1.1) | Wymaga abstrakcji HWND |
| Parsery `atoi`/`atof` | Boundary values, corrupted input, fuzz | Wydzielić do pure functions |
| `RppReadAccessor`/`RppWriteAccessor` | Parse "KEY VALUE" lines, round-trip | Już niezależne |

Rekomendacja: single-header test framework (doctest) w `cpp/tests/`, host-agnostic unit tests.

---

## 8. ReaImGui DOCK-FRAME — ANALIZA RYZYKA

ReaImGui scripts (np. ReaMD) tworzą dwa okna: **inner window** (`"Title"` — puste, bez renderowanego UI) oraz **dock frame** (`"Title (docked)"` — z renderowanym UI i dock tab bar). To najdelikatniejsza część capture pipeline — źródło blank/grey renderów i regresji.

### Ścieżki capture i ich strategia

| Ścieżka | Plik:Linia | Strategia | Komentarz |
|---------|-----------|-----------|-----------|
| `FindReaperWindow` | `window_manager.cpp:70-174` | 5-stage search: dock frame first → exact title → enum top → enum children → enum grandchildren | **Poprawna.** Preferuje dock frame, fallback do inner window. |
| `CaptureQueue::Tick` | `capture_queue.cpp:142-168` | Szuka dock frame `"Title (docked)"`. Retry do `maxRetries` (30 = ~1.5s). Jeśli nie znaleziony, fallback do inner window. | **Poprawna.** 8+ retries daje czas na pojawienie się dock frame. |
| Context menu (known) | `container.cpp:958` | Toggle → search → capture bezpośrednio | **OK** — known windows mają stabilne tytuły. |
| Context menu (arb docked) | `container.cpp:1079-1096` | Capture child + hide dock frame | **Uwaga:** strategia odwrotna do A (capture child zamiast frame). Celowe — dla arb windows child jest już w menu jako target. |
| FAV_BASE (arb) | `container.cpp:1375-1391` | Toggle → enqueue do CaptureQueue jeśli brak dock frame | **Poprawna.** Defers do async retry. |

### Scenariusze ryzyka

| Scenariusz | Ryzyko | Obecna obsługa | Ocena |
|-----------|--------|----------------|-------|
| Inner-only (no dock frame) | Blank/grey render | Fallback w CaptureQueue po maxRetries | ⚠️ Akceptowalne — użytkownik widzi puste okno ale bez crashu |
| Dock-frame-only | Brak problemu | FindReaperWindow preferuje dock frame | ✅ |
| Delayed dock-frame (>1.5s) | Capture inner, potem dock frame pojawia się floating | CaptureQueue timeout → fallback | ⚠️ Rzadkie, ale możliwe z wolnymi skryptami |
| Title mismatch (script zmienia tytuł) | Nie znaleziony przy restore | Puste pane, brak crashu | ⚠️ Akceptowalne |
| Restart restore | REAPER re-tworzy okna, ReDockIT recapture via saved `actionCommand` | Startup timer → enqueue z `deferAction=true` | ✅ Działa poprawnie |
| Docker parentage zmiana (REAPER update) | FindReaperWindow może nie znaleźć | 5-stage search pokrywa top/child/grandchild | ⚠️ Zależy od REAPER internals |

### Werdykt

Logika dock-frame detection jest **funkcjonalnie poprawna** i pokrywa znane scenariusze. Niespójność strategii (A: capture dock frame vs B: capture child + hide frame) jest celowa — różne code paths mają różne konteksty (async queue vs synchronous menu click). **Brak potwierdzonego buga**, ale scenariusze inner-only i delayed dock-frame (⚠️) niosą ryzyko regresji — obszar jest kruchy i wymaga testów regresyjnych przy aktualizacjach REAPER/ReaImGui.

---

## 9. TABELA PODSUMOWUJĄCA

| Kategoria | Znalezisk | Priorytet | Źródło |
|-----------|:---------:|-----------|--------|
| Bug funkcjonalny | 1 | KRYTYCZNY | Claude |
| Jakość kodu / spójność | 4 | WAŻNY | Claude 3, Codex 1 |
| Martwy kod | 11 | WAŻNY | Claude 5, Codex 6 |
| Bezpieczeństwo danych wejściowych | 2 | WAŻNY | Claude 1, Codex (re-weryfikacja) |
| Robustność (cycle protection) | 1 | WAŻNY | Codex C4 (zweryfikowany) |
| DX | 1 | WAŻNY | Codex |
| Performance | 3 | NICE-TO-HAVE | Claude 2, Codex 1 |
| UX | 1 | NICE-TO-HAVE | Codex |
| Architektura | 1 | NICE-TO-HAVE | Codex |
| Spójność prewencyjna | 2 | NICE-TO-HAVE | Claude 1, Codex 1 |
| Bezpieczeństwo / pamięć | 0 | — | Oba |
| Portability (niskie ryzyko) | 1 | WAŻNY (Win64) | Codex C1 (reklasyfikowany) |
| False-positives odrzucone | 2 | — | Codex C2, re-weryfikacja (HWND_TOP) |
| **RAZEM (do implementacji)** | **28** | | |
| **False-positives odrzucone** | **2** | | |

---

## 10. REKOMENDACJA RELEASE

### Faza 1 — Release blockers (przed publikacją)

1. **Naprawić punkt 1.1** (activeTab) — jedyny bug funkcjonalny
2. **Dodać cycle protection** (2.15) — ochrona przed stack overflow z uszkodzonego RPP
3. **Usunąć martwy kod** (sekcja 3) — czysty codebase przed open-source
4. **Ujednolicić README vs index.xml** (2.17) — spójny przekaz dla społeczności
5. **Ujednolicić copyright** (4.6) — LICENSE vs README

### Faza 2 — Jakość kodu (v1.1.1)

6. Wydzielić tablice z config.h do config.cpp (2.1)
7. Usunąć duplikat FAV_SECTION (2.2)
8. Ujednolicić StateAccessor w workspace_manager (2.3)
9. Zamienić strncpy na safe_strncpy (2.4)
10. Dodać Debug asserty w SplitTree accessorach (2.5)
11. Dodać walidację atoi/atof z clampami (2.14)
12. Usunąć hardcoded 512 w state_accessor.h (2.8)

### Faza 3 — Backlog (otwarte, do realizacji w v1.2.0+)

| ID | Status | Decyzja |
|----|--------|---------|
| 4.1 GDI brush cache | ZREALIZOWANE | Wdrożone w ramach audytu (commit 11) |
| 4.2 Targeted InvalidateRect | OTWARTE | Profile-first — wymaga benchmarku przed optymalizacją |
| 4.3 Tab overflow UI | OTWARTE | Feature request — zaplanować jako osobna feature |
| 4.4 NOMINMAX | ZREALIZOWANE | Wdrożone w ramach audytu (commit 10) |
| 4.5 Container.cpp split | ZREALIZOWANE | Rozbite na 4 pliki: container, _paint, _input, _state |
| 4.7 SaveState debounce | ODROCZONE | Re-weryfikacja wykazała brak problemu (event-driven, in-memory) |
| C1 Win64 portability | OTWARTE | Do naprawy przy uruchomieniu Windows buildu |

**Ogólna ocena:** Codebase jest w dobrej kondycji. Architektura czytelna, moduły mają jasne odpowiedzialności, brak memory leaks, brak security issues. Wszystkie 23 znaleziska z Fazy 1 i 2 wdrożone implementacyjnie + 5 udokumentowane jako otwarte TODO w backlogu.

---

## 11. REJESTR ZMIAN RAPORTU

| Wersja | Data | Zmiany |
|--------|------|--------|
| v1 | 2026-03-02 | Pierwotny raport: 27 znalezisk + 2 FP |
| v2 | 2026-03-02 | Re-weryfikacja każdego znaleziska w kodzie. Usunięte: HWND_TOP (→ FP). Przeniesione: SaveState debounce (WAŻNY → NICE). Dodane: RecalcNode cycle protection. Wzbogacone: 2.5, 2.14. |
| **v3 (rev.3)** | 2026-03-02 | Korekty po review Codex: C1 reklasyfikacja z FP na niskie ryzyko portability (Win64). Dodana sekcja 8 (ReaImGui dock-frame risk matrix). Sekcja 6 przeformułowana: „brak memory corruption/leak, ale luki robustności inputu" (spójność z 2.14/2.15). |
