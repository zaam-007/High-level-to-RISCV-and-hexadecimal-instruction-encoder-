// ============================================================
//          Digital Mirror — ESP32 Sensor Firmware
//          Sensors: MAX30102 (Pulse/HRV) + GSR
//          Output:  Serial (USB) → Python osc.py
//          Serial format every 500ms:
//            IR=xxxxx BPM=xx HRV=xx GSR=xxxx Beats=x
// ============================================================

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ── Hardware Config ──────────────────────────────────────────
#define SCREEN_WIDTH         128
#define SCREEN_HEIGHT         64
#define GSR_PIN               35      // Analog pin for GSR sensor
#define PULSE_AMP             0x1F    // MAX30102 LED amplitude

// ── Finger Detection Thresholds ──────────────────────────────
// IR must exceed THRESHOLD to confirm finger is on sensor.
// Hysteresis: finger is "lost" if IR drops below LOST.
#define IR_FINGER_THRESHOLD  70000
#define IR_FINGER_LOST       50000

// ── BPM Calculation ──────────────────────────────────────────
// Average BPM over last N beats. 5 beats ≈ 5-6 seconds at 60 BPM.
// More beats = smoother, slower to update.
#define BPM_AVERAGE_SIZE      5
#define MIN_BEAT_DELTA_MS    333    // 333ms = 180 BPM max
#define MAX_BEAT_DELTA_MS   1500    // 1500ms = 40 BPM min
#define MIN_VALID_BEATS       5     // Wait for this many beats before reporting

// ── HRV (RMSSD) Calculation ──────────────────────────────────
// Store last N R-R intervals (time between beats in ms).
// RMSSD = sqrt( mean of squared successive differences ).
// Minimum 4 intervals needed for a meaningful RMSSD value.
// 8 intervals ≈ 8-12 seconds of data at resting heart rate.
#define RR_WINDOW             8
#define MIN_RR_FOR_HRV        4

// ── GSR Sampling ─────────────────────────────────────────────
#define GSR_SAMPLES          10    // Average over 10 samples per read

// ── Serial / OLED Output Rate ────────────────────────────────
#define SERIAL_INTERVAL_MS   500   // Print to serial every 500ms
#define OLED_INTERVAL_MS     250   // Update OLED display every 250ms

// ── DC Filter for Beat Detection ─────────────────────────────
// Removes slow DC drift from IR signal so beat peaks are sharp.
#define DC_ALPHA              0.85f

// ─────────────────────────────────────────────────────────────

MAX30105 particleSensor;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── BPM State ────────────────────────────────────────────────
byte  bpmRates[BPM_AVERAGE_SIZE];
byte  rateSpot       = 0;
int   validBeatCount = 0;
long  lastBeat       = 0;
float beatsPerMinute = 0;
int   beatAvg        = 0;
float irFiltered     = 0;

// ── HRV (RMSSD) State ────────────────────────────────────────
long  rrBuffer[RR_WINDOW];   // Circular buffer of R-R intervals (ms)
int   rrHead    = 0;          // Next write position in buffer
int   rrCount   = 0;          // Number of valid entries currently in buffer
int   hrvRMSSD  = 0;          // Latest computed HRV value in ms

// ── General State ────────────────────────────────────────────
bool  fingerOn       = false;
int   gsrSmoothed    = 0;
unsigned long lastSerialPrint = 0;
unsigned long lastOLEDUpdate  = 0;


// ─────────────────────────────────────────────────────────────
// FUNCTIONS
// ─────────────────────────────────────────────────────────────

int readGSR() {
  long sum = 0;
  for (int i = 0; i < GSR_SAMPLES; i++) {
    sum += analogRead(GSR_PIN);
    delayMicroseconds(100);  // Small gap between samples
  }
  return (int)(sum / GSR_SAMPLES);
}

// Add a new R-R interval to the circular buffer and recompute RMSSD.
void addRRInterval(long deltaMs) {
  rrBuffer[rrHead] = deltaMs;
  rrHead = (rrHead + 1) % RR_WINDOW;
  if (rrCount < RR_WINDOW) rrCount++;

  if (rrCount < MIN_RR_FOR_HRV) {
    hrvRMSSD = 0;  // Not enough data yet
    return;
  }

  // Read intervals in chronological order from the circular buffer
  long ordered[RR_WINDOW];
  int  start = (rrHead - rrCount + RR_WINDOW) % RR_WINDOW;
  for (int i = 0; i < rrCount; i++) {
    ordered[i] = rrBuffer[(start + i) % RR_WINDOW];
  }

  // Compute RMSSD: root mean square of successive differences
  float sumSqDiff = 0.0f;
  int   pairs     = rrCount - 1;
  for (int i = 0; i < pairs; i++) {
    float diff = (float)(ordered[i + 1] - ordered[i]);
    sumSqDiff += diff * diff;
  }
  hrvRMSSD = (int)sqrt(sumSqDiff / pairs);
}

void resetSensorState() {
  beatsPerMinute = 0;
  beatAvg        = 0;
  lastBeat       = 0;
  rateSpot       = 0;
  validBeatCount = 0;
  irFiltered     = 0;
  hrvRMSSD       = 0;
  rrHead         = 0;
  rrCount        = 0;
  for (byte x = 0; x < BPM_AVERAGE_SIZE; x++) bpmRates[x] = 0;
  for (byte x = 0; x < RR_WINDOW;       x++) rrBuffer[x]  = 0;
}

void updateOLED(long ir) {
  display.clearDisplay();

  // Line 1: IR + GSR raw values
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("IR:");
  display.print(ir / 1000);  // Show in thousands to fit screen
  display.print("k  G:");
  display.print(gsrSmoothed);

  // Line 2: Finger status
  display.setCursor(0, 12);
  display.print(fingerOn ? "Finger: ON" : "Finger: OFF  --");

  // Line 3: BPM (large)
  display.setCursor(0, 24);
  display.print("BPM:");
  display.setTextSize(2);
  display.setCursor(50, 20);
  if (fingerOn && validBeatCount >= MIN_VALID_BEATS && beatAvg > 0)
    display.print(beatAvg);
  else if (fingerOn)
    display.print("...");
  else
    display.print("--");

  // Line 4: HRV
  display.setTextSize(1);
  display.setCursor(0, 42);
  display.print("HRV:");
  if (fingerOn && hrvRMSSD > 0)
    display.print(hrvRMSSD);
  else
    display.print("--");
  display.print("ms");

  // Line 5: State label (Python does the real classification, this is indicative)
  display.setCursor(0, 54);
  if (!fingerOn) {
    display.print("Place finger...");
  } else if (validBeatCount < MIN_VALID_BEATS) {
    display.print("Collecting...");
  } else {
    display.print("BPM:");
    display.print(beatAvg);
    display.print(" HRV:");
    display.print(hrvRMSSD);
  }

  display.display();
}


// ─────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);  // SDA=21, SCL=22

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found! Check wiring.");
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Digital Mirror");
  display.setCursor(0, 12);
  display.print("Initialising...");
  display.display();

  // MAX30102 Init
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found! Check wiring.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Sensor Error!");
    display.display();
    while (1);
  }

  // Sensor settings: LED amplitude, sample rate, pulse width, ADC range
  particleSensor.setup(PULSE_AMP, 4, 2, 400, 411, 4096);
  particleSensor.setPulseAmplitudeRed(PULSE_AMP);
  particleSensor.setPulseAmplitudeIR(PULSE_AMP);
  particleSensor.setPulseAmplitudeGreen(0);  // Green off (saves power)

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Ready!");
  display.setCursor(0, 12);
  display.print("Place finger on");
  display.setCursor(0, 24);
  display.print("sensor pad.");
  display.display();

  Serial.println("ESP32 Digital Mirror ready. Place finger on sensor.");
}


// ─────────────────────────────────────────────────────────────
// MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  long irRaw = particleSensor.getIR();

  // ── 1. FINGER DETECTION (with hysteresis) ────────────────
  if (!fingerOn && irRaw >= IR_FINGER_THRESHOLD) {
    fingerOn = true;
    resetSensorState();
    Serial.println("Finger detected. Collecting data...");
  }
  if (fingerOn && irRaw < IR_FINGER_LOST) {
    fingerOn = false;
    resetSensorState();
    Serial.println("Finger removed.");
  }

  // ── 2. GSR READING (always, finger or not) ───────────────
  gsrSmoothed = readGSR();

  // ── 3. BEAT DETECTION + BPM + HRV ────────────────────────
  if (fingerOn) {
    // DC filter: removes slow drift, isolates beat peaks
    irFiltered  = DC_ALPHA * irFiltered + (1.0f - DC_ALPHA) * irRaw;
    long irForBeat = (irRaw - (long)irFiltered) + 120000;

    if (checkForBeat(irForBeat)) {
      long delta = millis() - lastBeat;

      if (lastBeat != 0
          && delta >= MIN_BEAT_DELTA_MS
          && delta <= MAX_BEAT_DELTA_MS)
      {
        // Valid beat interval
        beatsPerMinute = 60000.0f / delta;

        if (beatsPerMinute >= 40 && beatsPerMinute <= 180) {
          // Add to BPM rolling average
          bpmRates[rateSpot++] = (byte)beatsPerMinute;
          rateSpot %= BPM_AVERAGE_SIZE;
          if (validBeatCount < MIN_VALID_BEATS) validBeatCount++;

          if (validBeatCount >= MIN_VALID_BEATS) {
            beatAvg = 0;
            for (byte x = 0; x < BPM_AVERAGE_SIZE; x++)
              beatAvg += bpmRates[x];
            beatAvg /= BPM_AVERAGE_SIZE;
          }

          // Add R-R interval and recompute HRV (RMSSD)
          addRRInterval(delta);
        }
      }
      lastBeat = millis();
    }
  } else {
    // Finger off: do not compute beats
    resetSensorState();
  }

  // ── 4. OLED UPDATE (every 250ms) ─────────────────────────
  unsigned long now = millis();
  if (now - lastOLEDUpdate >= OLED_INTERVAL_MS) {
    lastOLEDUpdate = now;
    updateOLED(irRaw);
  }

  // ── 5. SERIAL OUTPUT (every 500ms) ───────────────────────
  // Python parses: IR=xxxxx BPM=xx HRV=xx GSR=xxxx Beats=x
  if (now - lastSerialPrint >= SERIAL_INTERVAL_MS) {
    lastSerialPrint = now;

    int reportBPM = (fingerOn && validBeatCount >= MIN_VALID_BEATS) ? beatAvg : 0;
    int reportHRV = (fingerOn && rrCount >= MIN_RR_FOR_HRV) ? hrvRMSSD : 0;

    Serial.print("IR=");     Serial.print(irRaw);
    Serial.print(" BPM=");   Serial.print(reportBPM);
    Serial.print(" HRV=");   Serial.print(reportHRV);
    Serial.print(" GSR=");   Serial.print(gsrSmoothed);
    Serial.print(" Beats="); Serial.println(validBeatCount);
  }
}