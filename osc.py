import serial
import time
import re
from collections import deque
from pythonosc import udp_client

# ─────────────────────────────────────────────────────────────────
# CONFIG
# ─────────────────────────────────────────────────────────────────
SERIAL_PORT        = "COM8"
BAUD_RATE          = 115200
TOUCHDESIGNER_IP   = "127.0.0.1"
OSC_PORT           = 7000

# MAX30102: if IR value is below this, no finger is on the sensor.
# Adjust based on your sensor; typical resting IR is 50000–150000+.
IR_FINGER_THRESHOLD = 50000

# Calibration: collect readings for this many seconds to set personal baselines
CALIBRATION_DURATION = 15   # seconds

# GSR drift correction: rolling window (number of samples)
GSR_WINDOW_SIZE    = 30

# How many seconds of "no finger" before sending idle signal to TD
IDLE_TIMEOUT       = 10

# 2D Affect Matrix thresholds (deviations from personal baseline)
# HRV: positive offset means calmer than baseline
HRV_HIGH_OFFSET    = +10   # ms above baseline → High HRV (calm)
HRV_LOW_OFFSET     = -10   # ms below baseline → Low HRV (stressed)
# GSR: delta from rolling average
GSR_DELTA_HIGH     = +30   # rising conductance → arousal/excitement

# State confirmation: how many consecutive reads must agree before
# we commit to a new state. Prevents single-sample flicker.
# At ~1 read/sec (HRV updates every 5-6s), 3 = ~15-18 seconds of stability.
N_CONFIRM_READINGS = 3

# Minimum seconds a committed state must hold before it can change.
# Matches the biological reality: HRV cannot physiologically change
# in under ~8 seconds.
STATE_HOLD_TIME    = 8    # seconds

# How often (seconds) to send raw sensor values for TD debugging display.
# State flags are only sent on actual state changes (not on a timer).
RAW_SEND_INTERVAL  = 1.0  # seconds
# ─────────────────────────────────────────────────────────────────


# ── OSC CLIENT ───────────────────────────────────────────────────
client = udp_client.SimpleUDPClient(TOUCHDESIGNER_IP, OSC_PORT)


def get_value(line, key, default="0"):
    """Extract a labelled value from a serial string like 'BPM=72 HRV=58 GSR=412'."""
    match = re.search(rf"{key}\s*=\s*([A-Za-z0-9.\-]+)", line)
    return match.group(1) if match else default


def classify_state(hrv, hrv_baseline, gsr_delta):
    """
    Classify the user's emotional state using the 2D Circumplex Affect Model.
    Returns: (state_name, stress_level 0-1, calm_level 0-1)

    Axes:
      HRV  → Valence  (High HRV = Calm/Positive)
      GSR Δ → Arousal (High GSR delta = Excited/Tense)
    """
    high_hrv = hrv >= (hrv_baseline + HRV_HIGH_OFFSET)
    low_hrv  = hrv <= (hrv_baseline + HRV_LOW_OFFSET)
    high_gsr = gsr_delta >= GSR_DELTA_HIGH

    if high_hrv and not high_gsr:
        return "ZEN",     0.0, 1.0   # Calm + Low arousal
    elif low_hrv and high_gsr:
        return "STRESS",  1.0, 0.0   # Stressed + High arousal
    elif high_hrv and high_gsr:
        return "FLOW",    0.4, 0.9   # Positive + High arousal (eustress / focus)
    else:
        return "BURNOUT", 0.6, 0.2   # Stressed/flat + Low arousal (fatigue)


# ── CONNECT TO ESP32 ─────────────────────────────────────────────
print(f"Connecting to ESP32 on {SERIAL_PORT}...")
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()
except Exception as e:
    print(f"ERROR: Could not open serial port — {e}")
    exit()

print(f"Streaming to TouchDesigner at {TOUCHDESIGNER_IP}:{OSC_PORT}")
print("Press Ctrl+C to stop.\n")


# ── STATE VARIABLES ───────────────────────────────────────────────
# Calibration
calibration_hrv = []
calibration_gsr = []
calibration_start = None
is_calibrating   = False
hrv_baseline     = None
gsr_baseline     = None

# GSR drift correction (rolling window)
gsr_window = deque(maxlen=GSR_WINDOW_SIZE)

# No-finger / idle tracking
no_contact_start = None
idle_sent        = False

# ── State confirmation & hold tracking ───────────────────────────
# candidate_state: what the last read classified (may not be committed yet)
# confirm_count:   how many consecutive reads agree with candidate_state
# committed_state: the last state that was actually sent to TD
# state_held_since: when the committed state was locked in
# last_raw_send:   timestamp of the last raw sensor OSC burst
candidate_state  = None
confirm_count    = 0
committed_state  = None
state_held_since = 0.0
last_raw_send    = 0.0


# ── MAIN LOOP ─────────────────────────────────────────────────────
try:
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()

        if not line:
            continue

        print(f"RAW: {line}")

        # Parse sensor values
        ir    = int(float(get_value(line, "IR",    0)))
        bpm   = int(float(get_value(line, "BPM",  0)))
        hrv   = int(float(get_value(line, "HRV",  0)))
        gsr   = int(float(get_value(line, "GSR",  0)))
        beats = int(float(get_value(line, "Beats", 0)))

        # ── 1. VALIDITY: IS A FINGER ON THE SENSOR? ───────────────
        if ir < IR_FINGER_THRESHOLD:
            if no_contact_start is None:
                no_contact_start = time.time()
                idle_sent = False
                print("  [!] No finger detected — holding last TD state.")

            # After IDLE_TIMEOUT, tell TD to show idle animation
            if not idle_sent and (time.time() - no_contact_start) > IDLE_TIMEOUT:
                client.send_message("/sensor/contact", 0)
                print("  [!] Idle timeout — sent /sensor/contact = 0 to TD.")
                idle_sent = True

            # Do NOT send any new data — TD holds last received values
            time.sleep(0.1)
            continue

        # Finger is back on the pad
        if no_contact_start is not None:
            print("  [+] Finger detected — resuming.")
            no_contact_start = None
            idle_sent = False
            # Reset calibration for new user session
            hrv_baseline = None
            calibration_hrv.clear()
            calibration_gsr.clear()
            gsr_window.clear()

        client.send_message("/sensor/contact", 1)

        # ── 2. CALIBRATION PHASE (first 15 seconds per user) ──────
        if hrv_baseline is None:
            if not is_calibrating:
                print(f"\n  [CAL] Starting {CALIBRATION_DURATION}s calibration — hold still...")
                calibration_start = time.time()
                is_calibrating = True
                client.send_message("/sensor/calibrating", 1)

            if hrv > 0:
                calibration_hrv.append(hrv)
            if gsr > 0:
                calibration_gsr.append(gsr)

            elapsed   = time.time() - calibration_start
            remaining = CALIBRATION_DURATION - elapsed
            print(f"  [CAL] {remaining:.1f}s left | HRV={hrv} GSR={gsr}")

            if elapsed >= CALIBRATION_DURATION:
                hrv_baseline = (
                    sum(calibration_hrv) / len(calibration_hrv)
                    if calibration_hrv else 50
                )
                gsr_baseline = (
                    sum(calibration_gsr) / len(calibration_gsr)
                    if calibration_gsr else 400
                )
                is_calibrating = False
                client.send_message("/sensor/calibrating", 0)
                print(f"\n  [CAL] Done! Baseline → HRV={hrv_baseline:.1f}ms  GSR={gsr_baseline:.1f}\n")

            time.sleep(0.1)
            continue

        # ── 3. GSR DRIFT CORRECTION (rolling delta) ────────────────
        # Instead of using raw GSR (which drifts as skin acclimatises),
        # we track the change from a short rolling average.
        gsr_window.append(gsr)
        gsr_rolling_avg = sum(gsr_window) / len(gsr_window)
        gsr_delta = gsr - gsr_rolling_avg   # +ve = rising arousal, -ve = falling

        # ── 4. CLASSIFY EMOTIONAL STATE ───────────────────────────
        raw_state, stress_level, calm_level = classify_state(hrv, hrv_baseline, gsr_delta)

        # ── 5. STATE CONFIRMATION (debounce) ──────────────────────
        # A state is only committed after N_CONFIRM_READINGS consecutive
        # reads agree AND STATE_HOLD_TIME has passed since last commit.
        now = time.time()
        hold_elapsed = now - state_held_since

        if raw_state == candidate_state:
            confirm_count += 1
        else:
            # New candidate — reset counter
            candidate_state = raw_state
            confirm_count   = 1

        state_committed_this_loop = False
        if (
            confirm_count >= N_CONFIRM_READINGS
            and raw_state != committed_state
            and hold_elapsed >= STATE_HOLD_TIME
        ):
            # Commit the new state
            committed_state  = raw_state
            state_held_since = now
            state_committed_this_loop = True
            print(f"\n  ★ STATE COMMITTED → {committed_state} "
                  f"(confirmed {confirm_count}× | held {hold_elapsed:.1f}s)\n")

            # Send state flags ONLY on state change
            client.send_message("/state/zen",     1 if committed_state == "ZEN"     else 0)
            client.send_message("/state/stress",  1 if committed_state == "STRESS"  else 0)
            client.send_message("/state/flow",    1 if committed_state == "FLOW"    else 0)
            client.send_message("/state/burnout", 1 if committed_state == "BURNOUT" else 0)
            client.send_message("/sensor/stress", stress_level)
            client.send_message("/sensor/calm",   calm_level)

        # ── 6. RAW SENSOR VALUES (slow, on interval) ───────────────
        # Send raw values at a relaxed rate for TD debug overlays.
        # These do NOT drive the visual state — they are informational.
        if now - last_raw_send >= RAW_SEND_INTERVAL:
            client.send_message("/sensor/ir",        ir)
            client.send_message("/sensor/bpm",       bpm)
            client.send_message("/sensor/hrv",       hrv)
            client.send_message("/sensor/gsr",       gsr)
            client.send_message("/sensor/gsr_delta", round(gsr_delta, 2))
            client.send_message("/sensor/beats",     beats)
            last_raw_send = now

        status = "✓ COMMITTED" if state_committed_this_loop else f"  pending ({confirm_count}/{N_CONFIRM_READINGS})"
        print(
            f"  [{committed_state or 'NONE':8s}] candidate={raw_state:8s} {status} "
            f"| HRV:{hrv:3d}ms | GSR_Δ:{gsr_delta:+.1f}"
        )

        # Fast inner loop — IR check runs every 0.1s
        # Classification happens every serial line (ESP32 rate, ~1s for HRV)
        time.sleep(0.1)

except KeyboardInterrupt:
    print("\n\nStopping bridge...")

finally:
    ser.close()
    print("Serial port closed.")
