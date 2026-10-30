package io.github.vitorhpsousa.dpfsentinel.core

// One assembled logger row: a value per Pids.COLUMNS entry, NaN = no valid reading.
class Row(val values: DoubleArray = DoubleArray(Pids.COLUMNS.size) { Double.NaN }) {
    operator fun get(name: String): Double {
        val i = Pids.col(name)
        return if (i >= 0) values[i] else Double.NaN
    }

    val rpm get() = get("engine_rpm")
    val speed get() = get("vehicle_speed")
    val coolant get() = get("coolant_temp")
    val batteryV get() = get("control_module_v")
    val diffPressure get() = get("dpf_diff_pressure_hpa")
    val egt get() = get("egt_before_dpf_c")
    val catTemp get() = get("dpf_zone_temp_c")
    val sinceRegen get() = get("dpf_dist_since_regen_mi")
    val odometer get() = get("odometer_mi")
    val soot get() = get("dpf_soot_level_g")
    val intercooler get() = get("intercooler_temp_c")
    val maf get() = get("maf_flow")
    val regenActive get() = get("dpf_regen_active").let { !it.isNaN() && it != 0.0 }

    fun anyPid(): Boolean = (0 until Pids.TABLE.size).any { !values[it].isNaN() }

    // Same as the firmware's isDpfIdleBlockageFlagged (threshold unverified, see docs).
    fun idleBlockageFlagged(): Boolean =
        !diffPressure.isNaN() && !rpm.isNaN() && rpm <= IDLE_RPM_CEILING && diffPressure >= IDLE_PRESSURE_THRESHOLD_HPA

    companion object {
        const val IDLE_PRESSURE_THRESHOLD_HPA = 20.0
        const val IDLE_RPM_CEILING = 1100.0
    }
}
