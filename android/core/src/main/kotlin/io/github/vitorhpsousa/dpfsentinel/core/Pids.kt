package io.github.vitorhpsousa.dpfsentinel.core

// Port of src/obd/pid_registry.h + pid_decode.cpp: the PIDs verified on a 2014 ix35 1.7 CRDi.
// `decode` receives the ISO-TP payload starting at the service id, so offsets include it.
// Order matters: it is the CSV column order, identical to the firmware's logs.
class PidDef(
    val name: String,
    val unit: String,
    val header: String,
    val request: String,
    val frames: String,
    val prefix: IntArray,
    val periodMs: Long,
    val decode: (IntArray) -> Double,
)

object Pids {
    private const val MI_PER_M = 0.000621371

    private fun u8(p: IntArray, i: Int) = p[i].toDouble()
    private fun u16(p: IntArray, i: Int) = ((p[i] shl 8) or p[i + 1]).toLong()
    private fun u24(p: IntArray, i: Int) = ((p[i].toLong() shl 16) or (p[i + 1].toLong() shl 8) or p[i + 2].toLong())
    private fun u32(p: IntArray, i: Int) = (p[i].toLong() shl 24) or u24(p, i + 1)

    private fun need(n: Int, f: (IntArray) -> Double): (IntArray) -> Double = { p -> if (p.size < n) Double.NaN else f(p) }

    // Floats in the C++ decoders: round through Float so the numbers match the firmware's CSVs.
    private fun f(v: Double) = v.toFloat().toDouble()

    val TABLE: List<PidDef> = listOf(
        // header 7DF, standard mode 01
        PidDef("engine_rpm", "rpm", "7DF", "010C", "1", intArrayOf(0x41, 0x0C), 200, need(4) { f(u16(it, 2) * 0.25) }),
        PidDef("vehicle_speed", "km/h", "7DF", "010D", "1", intArrayOf(0x41, 0x0D), 500, need(3) { u8(it, 2) }),
        PidDef("engine_load_pct", "%", "7DF", "0104", "1", intArrayOf(0x41, 0x04), 250, need(3) { f(u8(it, 2) * 100.0 / 255.0) }),
        PidDef("maf_flow", "g/s", "7DF", "0110", "1", intArrayOf(0x41, 0x10), 500, need(4) { f(u16(it, 2) * 0.01) }),
        PidDef("coolant_temp", "C", "7DF", "0105", "1", intArrayOf(0x41, 0x05), 2000, need(3) { u8(it, 2) - 40.0 }),
        PidDef("control_module_v", "V", "7DF", "0142", "1", intArrayOf(0x41, 0x42), 2000, need(4) { f(u16(it, 2) * 0.001) }),
        PidDef("egr_duty_pct", "%", "7DF", "012C", "1", intArrayOf(0x41, 0x2C), 2000, need(3) { f(u8(it, 2) * 100.0 / 255.0) }),
        PidDef("intake_air_temp_c", "C", "7DF", "010F", "1", intArrayOf(0x41, 0x0F), 5000, need(3) { u8(it, 2) - 40.0 }),
        // 013E is Catalyst Temp B1S2; the name is kept for continuity. The DPF-inlet temperature is egt_before_dpf_c.
        PidDef("dpf_zone_temp_c", "C", "7DF", "013E", "1", intArrayOf(0x41, 0x3E), 1000, need(4) { f(u16(it, 2) * 0.1 - 40.0) }),

        // header 7E0, ECM manufacturer mode 21
        PidDef("dpf_diff_pressure_hpa", "hPa", "7E0", "211B", "1", intArrayOf(0x61, 0x1B), 1000, need(4) { u16(it, 2) * 0.5 }),
        PidDef("egt_before_dpf_c", "C", "7E0", "2103", "B", intArrayOf(0x61, 0x03), 1000, need(75) { f(u16(it, 48) * 0.0183966 + 99.013) }),
        PidDef("dpf_dist_since_regen_mi", "mi", "7E0", "2103", "B", intArrayOf(0x61, 0x03), 1000, need(75) { f(u24(it, 56) * MI_PER_M) }),
        PidDef("odometer_mi", "mi", "7E0", "2103", "B", intArrayOf(0x61, 0x03), 1000, need(75) { f(u32(it, 59) * MI_PER_M) }),
        PidDef("dpf_regen_active", "bool", "7E0", "2103", "B", intArrayOf(0x61, 0x03), 1000, need(75) { if (it[71] == 4) 1.0 else 0.0 }),
        PidDef("dpf_regen_burning", "bool", "7E0", "2103", "B", intArrayOf(0x61, 0x03), 1000, need(75) { if (it[19] == 21) 1.0 else 0.0 }),
        PidDef("dpf_soot_level_g", "g", "7E0", "21948001", "2", intArrayOf(0x61), 2000, need(12) { f(u8(it, 10) * 100.0 / 255.0) }),
        PidDef("intercooler_temp_c", "C", "7E0", "21948001", "2", intArrayOf(0x61), 2000, need(12) { u8(it, 6) - 50.0 }),

        // header 7D4, steering ECU
        PidDef("eps_speed_kmh", "km/h", "7D4", "2101", "3", intArrayOf(0x61, 0x01), 5000, need(13) { u8(it, 5) }),
        PidDef("eps_steering_deg", "deg", "7D4", "2101", "3", intArrayOf(0x61, 0x01), 5000, need(13) { f(u16(it, 11).toShort() * 0.1) }),
        PidDef("eps_voltage_v", "V", "7D4", "2101", "3", intArrayOf(0x61, 0x01), 5000, need(13) { f(u8(it, 3) * 0.1) }),
    )

    // Computed from the above, never requested from the ECU.
    val DERIVED: List<String> = listOf("dpf_odo_at_last_regen_mi")

    val COLUMNS: List<String> = TABLE.map { it.name } + DERIVED

    fun col(name: String): Int = COLUMNS.indexOf(name)

    fun byName(name: String): PidDef = TABLE.first { it.name == name }

    fun rxIdFor(header: String): String = if (header == "7D4") "7DC" else "7E8"
}
