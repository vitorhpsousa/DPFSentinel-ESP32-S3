package io.github.vitorhpsousa.dpfsentinel.core

import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

// The firmware's golden vectors (test/test_decode/test_decode.cpp), captured from the real ix35.
class DecodeTest {
    private fun extract(raw: String) = IsoTp.extract(raw, "7E8")
    private fun dec(name: String, p: IntArray) = Pids.byName(name).decode(p)

    @Test fun diffPressure() {
        val p = extract("7E804611B0012")
        assertEquals(4, p.size)
        assertEquals(9.0, dec("dpf_diff_pressure_hpa", p))
    }

    @Test fun sootAndIntercooler() {
        val p = extract("7E8100C61FF11001E8B\r7E8215500000305FF55")
        assertEquals(12, p.size)
        assertEquals(1.96, dec("dpf_soot_level_g", p), 0.01)
        assertEquals(35.0, dec("intercooler_temp_c", p))
    }

    @Test fun mode21Multiframe() {
        val p = extract(
            "7E8104B6103FFFFFFFF\r7E82100010001000000\r7E82200001000010010\r7E82300000000000100\r" +
                "7E82401000100010001\r7E82500010001000100\r7E82601000100FF0000\r7E8270000000C077B28\r" +
                "7E828000049DC0C07C5\r7E82904000100000000\r7E82A010001FFFFFF55",
        )
        assertEquals(75, p.size)
        assertEquals(0x61, p[0])
        assertEquals(0.0, dec("dpf_regen_active", p))
        val egt = dec("egt_before_dpf_c", p)
        assertTrue(egt > 50 && egt < 900)
        assertTrue(dec("odometer_mi", p) > 1000)
    }

    @Test fun shortAndForeignFramesRejected() {
        assertEquals(0, extract("NO DATA").size)
        assertEquals(0, IsoTp.extract("7EA804611B0012", "7E8").size)
        assertTrue(dec("dpf_soot_level_g", intArrayOf(0x61, 0, 0)).isNaN())
    }

    @Test fun batteryVoltage() {
        assertEquals(14.0, dec("control_module_v", intArrayOf(0x41, 0x42, 0x36, 0xB0)), 0.01)
    }

    @Test fun negativeSteeringAngle() {
        val p = IntArray(13).also { it[0] = 0x61; it[1] = 0x01; it[11] = 0xFF; it[12] = 0x9C }   // -100 -> -10.0 deg
        assertEquals(-10.0, dec("eps_steering_deg", p), 0.001)
    }

    @Test fun csvHeaderMatchesFirmwareLogs() {
        val example = listOf(File("../../logs/examples/session_example.csv"), File("../logs/examples/session_example.csv"))
            .first { it.exists() }
        assertEquals(example.readLines().first(), CsvFormat.HEADER)
    }

    @Test fun csvRowFormat() {
        val v = DoubleArray(Pids.COLUMNS.size) { Double.NaN }
        v[0] = 888.5
        assertEquals("15320,," + "888.500" + ",".repeat(Pids.COLUMNS.size - 1), CsvFormat.row(15320, Double.NaN, v))
    }
}
