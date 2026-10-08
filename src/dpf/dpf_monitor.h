#pragma once

// Flags DPF differential pressure that's high while the engine is at
// idle — pressure that's high purely because of engine speed/exhaust
// flow (e.g. motorway running) is normal and NOT a blockage signal.
// Requires dpf_diff_pressure_hpa and engine_rpm to both be confirmed
// PIDs (see pid_registry.h) before this produces meaningful output.
bool isDpfIdleBlockageFlagged(float diffPressureHpa, float rpm);
