#pragma once

// ═══════════════════════════════════════════════════════════
//  Web management page (Remote only) — http://heatpump.local
//  Status, settings, ON/OFF, defrost, and firmware update for
//  both boards. HTTP Basic auth: WEB_USER / settings.webPass.
// ═══════════════════════════════════════════════════════════

void webBegin();
void webStop();
void webLoop();    // call every loop iteration
