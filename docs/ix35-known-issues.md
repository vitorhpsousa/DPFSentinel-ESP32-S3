# ix35 1.7 CRDi (D4FD) - known issues and logger monitoring plan

Researched 2026-09-26. Not professional or legal advice.

Tags: [V] = statement read on a page I actually fetched this session. [U] = unverified (search-result snippet only, forum hearsay, or my own engineering guess).
Blocked/unreadable pages (not used as [V]): autodoc.co.uk blog (403), justanswer.* (403), mhhauto.com (403), hyundai-forums via tollbit redirect (402), carchecker.pro (429). The oil-dilution, limp-mode and pump claims below therefore come from search snippets and are [U].

Honest summary of the evidence: there is NO public, citable Hyundai TSB or recall specific to D4FD DPF/EGR/turbo that I could find. Fault mileages are mostly not given by the sources. Numeric thresholds in section 3 are my guesses unless stated.

## 0. What the logger already has (from pid_registry.py and memory)

Mode 01: engine_rpm, coolant_temp, vehicle_speed, maf_flow (g/s), engine_load_pct, egr_duty_pct (012C), intake_air_temp_c, dpf_zone_temp_c (really catalyst temp 013E). Battery voltage is PID 0142 per memory but is NOT yet in the Pi registry (memory says "port newest PIDs (0142) to the Pi registry"; the S3 has it).
Mode 21 on 7E0: dpf_diff_pressure_hpa (211B), dpf_soot_level_g and intercooler_temp_c (21948001), egt_before_dpf_c, dpf_dist_since_regen_mi, odometer_mi, dpf_regen_active, dpf_regen_burning (2103). Derived: dpf_odo_at_last_regen_mi. Steering ECU 7D4: eps_speed, eps_steering, eps_voltage.
Not available: MAP/boost (010B unsupported), fuel level, mode 22, rail pressure (the brief mentions a "rail pressure probe" but it is not in pid_registry.py; treat as not available on the Pi until verified). No oil temp, no oil level, no oil-dilution PID, no DTC reading in the registry.
Regen alerts (start/finish/interrupted) already exist on the S3 (report/regen_watch.cpp).

## 1. Known issues table

Mileage column: only what a source states; "n/s" = not stated.

| Issue | What sources say | Mileage | Tag / URL |
|---|---|---|---|
| DPF clogging on short trips | Most frequent complaint set for D4FD: "EGR fouling, intake deposits and DPF clogging in short trip use". Remedy: long motorway drive; specialist cleaning before replacement. | n/s | [V] https://selectedworld.com/engine-hyundai-1-7-crdi-diesel-2012-2018-141-hp-code-d4fd-specs-problems-reliability/ ; [V] https://autozilla.co.uk/hyundai/1-7-crdi-engine-problems/ ; [V] https://technicalparameters.eu/hyundai-ix35-faults/ |
| Owner-observed regen frequency | One 1.7 ix35 owner: regens about every 80 miles (expected ~300), ~15 miles long, economy 45 -> 30 mpg during them; 18 mi/day country-lane use "right on the threshold"; dealer test drive found nothing. | ~n/s | [V] https://singletrackworld.com/forum/topic/hyundai-ix35-17-dpf-issues (one owner's report, so anecdotal) |
| Oil level rising / fuel dilution from post-injection regen | Extra fuel injected for regen can pass piston rings into sump and dilute oil. Advice: an hour at motorway speed every 3-4 weeks; check dipstick; oil analysis. Page is an Australian Q&A about the 2010-2015 ix35 diesel. | n/s | [V] https://www.carsguide.com.au/hyundai/ix35/problems/diesel ; [U] snippet: https://www.carsguide.com.au/car-advice/q-and-a/increasing-oil-level-in-a-2012-hyundai-ix35-92348 ; [U] threads (unreadable): https://www.hyundai-forums.com/threads/diesel-mixing-with-oil-ix35.704932/ , https://www.hyundai-forums.com/threads/i40-1-7-diesel-oil-increasing-in-sump.685980/ |
| EGR valve fouling | Stuck open/closed from soot and carbon; power loss, poor acceleration, worse economy; specialist clean cheaper than replacement (GBP 250-350 quoted). | "extended wear" | [V] https://autozilla.co.uk/hyundai/1-7-crdi-engine-problems/ ; [V] https://technicalparameters.eu/hyundai-ix35-faults/ |
| EGR stuck causing limp / RPM ceiling | Owner of 2011 ix35 1.7: would not pass 3000 rpm; EGR valve clogged solid. Another 2012 1.7: limp mode in warm weather, P0404 (EGR) codes. | n/s | [U] search snippets: https://www.hyundai-forums.com/threads/engine-refuses-to-pass-3000-rpm-and-it-feels-that-it-has-no-power-anymore.702611/ , https://www.justanswer.co.uk/car/kfkpc-2013-ix35-1-7-crdi-last-week-problem-lack.html |
| Turbo | Oil-seal degradation / gasket leaks; blue-grey smoke, oil smell; "as miles rack up". VGT seizing from soot on short-trip cars, reportedly 120-180k km (that range is from a snippet on a 2.0 page, so weak). Turbo actuator sticking -> limp mode ~2000 rpm (2.0 CRDi cases). | 120-180k km [U] | [V] https://autozilla.co.uk/hyundai/1-7-crdi-engine-problems/ ; [U] https://www.justanswer.com/hyundai/fxzd6-hyundai-ix35-2-0-crdi-2015-enters-lipm.html |
| Injectors | "Fuel Injector Failure - particularly on 1.7 and 2.0 CRDi"; poor fuel quality linked; injector condition is a big price factor for replacement engines. | n/s | [V] https://technicalparameters.eu/hyundai-ix35-faults/ ; [V] https://selectedworld.com/engine-hyundai-1-7-crdi-diesel-2012-2018-141-hp-code-d4fd-specs-problems-reliability/ ; [U] https://enginesmarket.co.uk/kia/d4fd-engine |
| High-pressure fuel pump | "Problems from 70,000 km" - only a search-snippet claim, no page read. Separate recall (below) concerns a pump feed-pipe leak. | 70k km [U] | [U] search snippet on autodoc/other; not verified |
| MAF and other sensors | Faulty throttle-position, MAF and crank sensors cause stalling/rough running. | n/s | [V] https://technicalparameters.eu/hyundai-ix35-faults/ |
| Glow plugs | Wear -> smoke, poor economy, slow starts; replace roughly every 90,000 miles per one source; another says originals can last >155,000 km. Conflicting. | 90k mi / 155k km | [V] https://autozilla.co.uk/hyundai/1-7-crdi-engine-problems/ ; [U] forum snippet https://www.hyundai-forums.com/threads/ix35-diesel-glow-plug-replacement-procedure.683867/ |
| Dual-mass flywheel / clutch (manual) | Affects manual versions "typically after 80,000-100,000 miles"; maxi torque at 1250 rpm stresses it (snippet). Symptoms: idle rattle, judder/vibration pulling away, slip, high bite point. A PistonHeads 2011 1.7 at 110k+ miles had low-rev boom/vibration, never diagnosed. | 80-100k mi | [V] https://technicalparameters.eu/hyundai-ix35-faults/ ; [V] https://www.pistonheads.com/gassing/topic.asp?h=0&f=66&t=2109647 (unresolved) ; [U] https://www.carchecker.pro/reports/hyundai_tucson_tl_1.7_crdi.html |
| Timing drive | D4FD is CHAIN driven (parts catalogues sell chain kits, no belt). "Chain rattle on diesels, especially early models; can lead to engine failure". Practical replacement ~200,000 km, no official interval. | ~200k km | [V] chain rattle: https://technicalparameters.eu/hyundai-ix35-faults/ ; [U] 200k km / chain kit: https://www.auto-abc.eu/Hyundai-ix35/v4882-2013/service , https://www.autodoc.co.uk/car-parts/timing-chain-kit-15065/hyundai/ix35/ix35-lm-el-elh/9931-1-7-crdi |
| Swirl flaps / intake | Intake rod slipping out of the throttle-body/swirl-flap linkage, code P2015 (one owner report, snippet only). Intake deposits on D4FD listed as common. | n/s | [U] https://www.hyundai-forums.com/threads/need-help-identifying-name-of-a-part-ix35-2012-1-7-crdi.707309/ ; [V] deposits: https://selectedworld.com/engine-hyundai-1-7-crdi-diesel-2012-2018-141-hp-code-d4fd-specs-problems-reliability/ |
| Battery / alternator | Battery light while driving, failure to restart, poor alternator plug wiring; parasitic drain; one owner saw charge voltage fall from 14.2-14.4 V to 12.6-12.8 V. | n/s | [U] snippets only: https://www.justanswer.com/hyundai/4qspb-hyundai-ix35-1-7-crdi-hi-recently-bought-ix35-1-7.html , https://www.hyundai-forums.com/threads/alternator-issue.632969/ |
| Coolant/thermostat | Cooling-system health is "critical for turbo longevity". No thermostat-specific failure evidence found for D4FD. | n/s | [V] https://selectedworld.com/engine-hyundai-1-7-crdi-diesel-2012-2018-141-hp-code-d4fd-specs-problems-reliability/ (general only) |
| DPF differential-pressure and EGT sensors | No source found describing D4FD-specific failure of these sensors. The MHH Auto tuning thread mentions an EDC17C08 ECU DPF "solution" (removal) but I could not read it. | n/s | [U] https://mhhauto.com/Thread-Hyundai-iX35-EDC17C08-DPF-solution (403) |
| Limp mode (general) | Causes: turbo/boost leaks, fuel system, EGR. | n/s | [U] snippet https://www.justanswer.com/hyundai/fxzd6-hyundai-ix35-2-0-crdi-2015-enters-lipm.html |
| Excessive oil consumption | Reported "in some models". | n/s | [V] https://technicalparameters.eu/hyundai-ix35-faults/ |

### Recalls (UK)
- Fuel leak at the feed-pipe connection to the high-pressure pump, high-power "R" diesel engine, build 01/09/2011 - 02/11/2011. [V] https://car-recalls.co.uk/vin-recalls/hyundai-ix35/ (Note: "high power R engine" is not obviously the 1.7 D4FD; the owner's 2014 car is outside the build window either way.)
- ABS control-unit electrical fault, build 06/08/2013 - 02/06/2015 (could cover a 2014 car). [V] same URL. Advice in the interim letter was to park outdoors (from search snippet [U]).
- Seat-belt pre-tensioner, 17/10/2011 - 08/06/2012 (outside 2014). [V] same URL.
- Steering-wheel airbag not secured, 32,525 cars built Jan 2011 - Dec 2013 (snippet, Australian). [U] https://www.carsguide.com.au/hyundai/ix35/problems/recall
- ACTION: enter the VIN at https://www.check-vehicle-recalls.service.gov.uk/ (official DVSA) to confirm; I could not query it for this VIN.
- TSBs: none found publicly for D4FD DPF/EGR/turbo. Hyundai dealers hold TSBs that are not public; ask the dealer if any apply to the ECU software of this VIN. [U]

## 2. Watch-list for the owner (practical)

1. Short trips: the sources agree short, low-load journeys prevent DPF regeneration ([V] carsguide, autozilla, technicalparameters). The owner's 3-mile rides already pushed soot from 12.2 to 17.6 g (project memory). Plan a proper 30+ minute steady run (about 60 mph / 2000+ rpm range is my guess [U]) whenever soot is high. CarsGuide's advice is roughly an hour at motorway speed every 3-4 weeks [V].
2. Do not interrupt a regen (engine running, fan, higher idle, fuel economy dip). One owner reports a regen lasting about 15 miles [V singletrack]. Finish the drive.
3. Oil: check the dipstick monthly and before long trips. A RISING level (not falling) suggests fuel dilution [V carsguide]. Diesel smell or thin oil on the stick is a warning [U]. Use the correct low-SAPS oil (5W-30 low SAPS per [V] selectedworld) and follow the owner's-manual interval; on a short-trip car with regens, shortening the interval is common practice [U] and an oil analysis is offered as a diagnostic [V carsguide]. I did not find a Hyundai UK interval for a DPF car on a fetched page.
4. Early symptoms per sources: reduced power, hesitation, more smoke, warning lights, worse economy, odd odours, difficulty starting ([V] autozilla, selectedworld). Blue/grey smoke with burning-oil smell = turbo seals [V].
5. Limp mode: do not keep driving hard; read codes (an EGR or boost code like P0404 shows up in reports [U]).
6. Clutch: shudder pulling away, idle rattle, high biting point [V technicalparameters].
7. Chain: rattle on cold start is the flagged symptom [V technicalparameters].
8. Battery: dimming lights, slow cranking, battery light; watch the logged voltage (section 3) [U].
9. Fuel quality/filter: injector problems tied to poor fuel [V selectedworld]; use good fuel and change the fuel filter on schedule.
10. Confirm any DPF "off/tuning" advice from forums is not applicable: removing a DPF is illegal for road use in the UK (MOT emissions failure) [U, general knowledge, not verified this session].

## 3. Proposed derived metrics and alerts

All numbers are guesses [U] unless a value is from a source; calibrate against the owner's own logs first (the Open items in memory already list "hot-idle pressure baseline"). "Screen page" = the 3.5" soot/dashboard panel or the Waveshare board; "Telegram" = an S3 alert rule modelled on regen_watch.cpp.

| # | Metric (derived from) | Rule (guess) | Issue covered | Screen page | Telegram |
|---|---|---|---|---|---|
| 1 | Soot g vs time and per mile, split by trip type (soot_g, odometer, speed, trip length) | Log soot delta and miles per trip; classify trip short (<10 mi or <15 min) vs long. Alert if soot > 16 g (the owner's own 12-17.6 g range; Car Scanner reference top is the memory note) and no long trip since. | DPF clogging | Soot panel: soot bar plus "g per 10 mi (short/long)" | Warn at >=16 g and >=20 g "do a long drive" |
| 2 | Regen counter and interval (regen_active edges, dist_since_regen reset, odometer) | Store miles and days between regen starts. Alert when interval < 100 mi for 3 consecutive regens (owner in singletrack saw ~80 vs expected ~300 [V]; expected interval for THIS car should be learned from own data). | Frequent regen -> oil dilution, sensor or EGR fault | Regen history page: last 5 intervals | "Regens too frequent: N mi" |
| 3 | Failed / interrupted regen counter (regen_active 1->0 without dist_since_regen reset, or without burning=1) | Count per 30 days; alert on any interrupted regen, escalate at 2 in a row. Already partly built (regen_watch). | Failed regen, short trips | Regen page badge | Interrupted alert (exists) plus "2nd interrupted regen in a row" |
| 4 | Time since last complete regen (dist_since_regen_mi, days) | Warn when dist_since_regen > 1.5x the learned median interval, or > 300 mi; and soot > 14 g [guess]. | DPF loading | Main page: "since regen" chip goes amber/red | Daily-report line |
| 5 | Idle diff-pressure baseline drift (dpf_diff_pressure_hpa at hot idle: coolant>80 C, speed 0, rpm ~800-900, EGT stable) | Take the median of the last hot-idle minute per trip; track a rolling median right AFTER each completed regen (soot ~0). A rising post-regen baseline over months indicates ash loading / permanent blockage. Alert if post-regen idle dP is >30% above the first-100-day baseline [guess]. Also alert if dP is high while soot low (sensor disagreement) [guess]. | Ash loading, DPF wear, dP-sensor drift | Trends page: baseline vs date | Monthly report line |
| 6 | dP-to-flow ratio (dP / MAF) at steady cruise | Ratio rising over time at the same MAF band = restriction; use only when rpm and MAF in fixed bands. Sensor-stuck check: dP unchanged (+/-1 hPa) for 10 minutes while MAF varies > 30% [guess]. | DPF restriction, dP sensor fault | Trends | Alert on sensor-stuck |
| 7 | Warm-up time to 80 C (coolant_temp from cold start; ambient via intake_air_temp) | Time from start (coolant < 40 C) to 80 C at driving load; record and compare vs ambient bucket. Alert if median warm-up time rises >30% over baseline, or if coolant never exceeds 75 C after 15 min of driving, or exceeds 105 C [guesses]. | Thermostat stuck open, cooling issues; also regens need heat | Engine page: warm-up minutes | Alert on stuck-cold/overheat |
| 8 | Battery voltage (PID 0142, eps_voltage cross-check) | Cranking dip: minimum in first 3 s of start < 9.6 V (typical rule of thumb [guess]); charging: running voltage < 13.2 V or > 15.0 V for >60 s [guess]; the owner report of 12.6-12.8 V while driving is below healthy charging [U]. Rest voltage < 12.2 V at start. Note the S3 already logs 0142; the Pi does not. | Battery/alternator | Electrics page: V plot | Alert on low charging |
| 9 | EGR duty trend (egr_duty_pct at hot idle/steady cruise, with MAF) | Compare EGR duty vs MAF at fixed rpm/load bands; a rising duty with falling MAF over weeks = clogging; an EGR that reads high but MAF does not drop = stuck. Alert on drift > 20% over baseline [guess]. | EGR clogging/sticking (P0404-type faults reported [U]) | Trends | Monthly report |
| 10 | MAF sanity at idle | MAF at hot idle at a fixed rpm compared with baseline; low-reading drift >15% [guess]; or MAF > expected for load. | MAF sensor, intake leak | Trends | Monthly |
| 11 | Turbo/boost proxy | Boost cannot be read (010B unsupported). Proxy: engine_load_pct vs MAF vs rpm; a fall in MAF at the same load/rpm/IAT = boost leak or turbo weakness. Guess-only. | Turbo/actuator, limp mode | Trends | Alert on sudden step change |
| 12 | EGT/catalyst sanity in regen (egt_before_dpf, dpf_zone_temp) | Log peak EGT per regen and burn duration; alert if burn phase never reaches ~500 C (the existing burning flag uses >~500 C) or if peak > 800 C [guess]; regens ending early (< N minutes) counted as failed. | EGT sensor, incomplete regen | Regen page | With alert 3 |
| 13 | Oil-dilution risk score | Sum of regen minutes and post-injection time since last oil change; provide "regens since oil change" and "regen minutes since oil change". Alert at N regens (e.g. 15 [guess]) as a reminder to check the dipstick. The logger cannot measure dilution, only exposure. | Fuel dilution | Maintenance page | "Check dipstick" reminder |
| 14 | Intercooler/IAT delta | intercooler_temp minus IAT rising over time or IAT>60 C in cruise | Intercooler/charge-air | Trends | none |
| 15 | Cold-start idle rpm stability / hard-start (rpm rise time) | Time to reach idle rpm >600 after crank; increases hint at glow plugs/injectors [guess] | Glow plugs, injectors | Engine page | Monthly |
| 16 | Short-trip share | Percent of trips < 10 mi in the last 30 days and fraction of trips reaching 80 C | DPF, oil dilution, battery | Summary page | Weekly report line |
| 17 | Limp-mode heuristic | rpm plateau ~2000-3000 with load high and speed not rising, or load/MAF suddenly dropping [guess]; no DTC available on the logger, so flag "possible limp" | Limp mode | Main page banner | Alert |
| 18 | Odometer service reminders | Oil change interval (owner sets), fuel filter, glow plug at the ~90k mi source figure [V autozilla] but sources conflict | Maintenance | Maintenance page | Reminders |

Design notes: compute on hot, steady conditions only (coolant > 80 C) to keep baselines comparable; store the median per trip rather than raw samples; keep state in the S3 flash/SD.

## 4. Open questions

1. Is the "rail pressure probe" mentioned in the brief real? It is not in pid_registry.py. If it exists on the S3, high-pressure pump / injector monitoring (rail pressure vs demand, cranking rail pressure) becomes possible.
2. What is the correct oil grade/spec and service interval for this VIN in the UK owner's manual (I could not fetch a Hyundai document)? Is there a Hyundai TSB on oil dilution for D4FD?
3. What is a healthy idle diff pressure for this car at hot idle, post-regen? Needs the owner's data (memory open item).
4. Can the ECU expose more useful mode 21/22 data (oil dilution counter, ash load estimate, DTCs via mode 03/07/0A)? Adding mode 03/07 DTC reads would help limp-mode and failed-regen classification.
5. Does the VIN fall in the ABS recall build window (06/08/2013 - 02/06/2015)? Check at gov.uk.
6. The 70k km high-pressure pump and 120-180k km turbo figures are snippet-level; verify with a fetched source or a specialist.
7. What is this car's odometer/mileage now? Ranges above (80-100k mi clutch, ~200k km chain, glow plugs) depend on it.
8. Thermostat failure modes for D4FD: no evidence found; the warm-up metric is preventive.
