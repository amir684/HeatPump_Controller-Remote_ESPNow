#pragma once

// ═══════════════════════════════════════════════════════════
//  Touch UI of the S3 Remote (replaces display/buttons/menu)
//  Also implements displayToast() from display.h for the shared modules.
// ═══════════════════════════════════════════════════════════

void uiBegin(bool paired);   // paired = running with the old Remote's MAC
void uiLoop();               // call every loop iteration; draws at up to ~30 fps
