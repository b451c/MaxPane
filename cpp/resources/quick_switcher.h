// Control ID defines for the Quick Switcher dialog (F4, ADR-024 era).
// Shared between the .rc file (processed by swell_resgen.pl into a SWELL
// dialog template) and the C++ DlgProc.
#pragma once

#define IDD_QUICK_SWITCHER   1100
#define IDC_QS_SEARCH        1101
#define IDC_QS_LIST          1102
#define IDC_QS_HINT          1103
// IDOK = 1, IDCANCEL = 2 are the platform defaults — routed via the
// default dialog button (Enter) and keyboard shortcut (Esc) respectively.
