/*
 * Mitus Homer 5 - State Machine Haptic Actuator Controller
 *
 * NEW in v5: Built-in 4-state FSM for smooth inflate/deflate transitions
 * - Python sends simple 0/1 desired state
 * - Firmware handles all preflate/deflate timing automatically
 * - Smooth haptic feedback without Python-side complexity
 *
 * State Machine (per actuator):
 * - State 0 (OFF): Both valves closed
 * - State 1 (PREFLATING): Active inflation for T_PREFLATE ms
 * - State 2 (ON): Holding pressure
 * - State 3 (DEFLATING): Active deflation for T_DEFLATE ms
 *
 * Features:
 * - Multi-address support (0-5)
 * - Automatic state transitions with configurable timing
 * - Safety watchdog: Auto-shutoff after configurable time
 * - Serial protocol: SET <addr> <pixel> <level>
 *   - level=0: desired OFF (state machine handles deflate transition)
 *   - level=1-4: desired ON (state machine handles preflate transition)
 *   - level=5: force REVERSE/DEFLATE (immediate, bypasses state machine)
 *
 * Configuration (lines 42-52):
 * - NUM_HV507_CHIPS: Set to match physical hardware (1-6)
 * - DEBUG_SERIAL: Enable verbose debugging output
 * - T_PREFLATE: Inflation time (default 10ms)
 * - T_DEFLATE: Deflation time (default 20ms)
 * - WATCHDOG_ENABLED: Safety auto-shutoff feature
 * - WATCHDOG_TIMEOUT_MS: Max on-time per actuator (default 5000ms)
 */

#include <SPI.h>
#include <IntervalTimer.h>

// ========================= Pin Definitions =========================
#define PIN_SPI_MOSI   11
#define PIN_SPI_CLK    13
#define PIN_SPI_LE     6
#define PIN_BLANK      9
#define PIN_SHIFT_EN   4
#define PIN_HV_EN      5
#define PIN_HV_CTRL    23

// ========================= Configuration =========================
#define NUM_HV507_CHIPS 2       // CHANGE THIS to match your actual hardware (1-6)
#define DEBUG_SERIAL false      // Set to true for verbose state machine debugging

// State machine timing (milliseconds)
#define T_PREFLATE 20           // Time to inflate (10ms)
#define T_DEFLATE 200            // Time to deflate (20ms)
#define STATE_UPDATE_INTERVAL 5 // Check states every 5ms

// Watchdog configuration
#define WATCHDOG_ENABLED true   // Set to false to disable watchdog
#define WATCHDOG_TIMEOUT_MS 5000        // Max on-time per actuator (5 seconds)
#define WATCHDOG_CHECK_INTERVAL_MS 100  // Check every 100ms

// ========================= Valve Mappings =========================
static const byte finger[6][32] = {
    {18, 17, 47, 45, 44, 19, 16, 46, 43, 21, 20, 22, 42, 41, 23, 24, 40, 39, 25, 27, 37, 35, 38, 26, 29, 33, 36, 28, 31, 34, 30, 32},
    {61, 62, 0, 2, 3, 60, 63, 1, 4, 58, 59, 57, 5, 6, 56, 55, 7, 8, 54, 52, 10, 12, 9, 53, 50, 14, 11, 51, 48, 13, 49, 15},
    {18, 17, 47, 45, 44, 19, 16, 46, 43, 21, 20, 22, 42, 41, 23, 24, 40, 39, 25, 27, 37, 35, 38, 26, 29, 33, 36, 28, 31, 34, 30, 32},
    {61, 62, 0, 2, 3, 60, 63, 1, 4, 58, 59, 57, 5, 6, 56, 55, 7, 8, 54, 52, 10, 12, 9, 53, 50, 14, 11, 51, 48, 13, 49, 15},
    {18, 17, 47, 45, 44, 19, 16, 46, 43, 21, 20, 22, 42, 41, 23, 24, 40, 39, 25, 27, 37, 35, 38, 26, 29, 33, 36, 28, 31, 34, 30, 32},
    {61, 62, 0, 2, 3, 60, 63, 1, 4, 58, 59, 57, 5, 6, 56, 55, 7, 8, 54, 52, 10, 12, 9, 53, 50, 14, 11, 51, 48, 13, 49, 15},
  };

static const byte reservoir[6][32] = {
    {10, 9, 55, 53, 52, 11, 8, 54, 51, 13, 12, 14, 50, 49, 15, 0, 48, 63, 1, 3, 61, 59, 62, 2, 5, 57, 60, 4, 7, 58, 6, 56},
    {45, 46, 16, 18, 19, 44, 47, 17, 20, 42, 43, 41, 21, 22, 40, 39, 23, 24, 38, 36, 26, 28, 25, 37, 34, 30, 27, 35, 32, 29, 33, 31},
    {10, 9, 55, 53, 52, 11, 8, 54, 51, 13, 12, 14, 50, 49, 15, 0, 48, 63, 1, 3, 61, 59, 62, 2, 5, 57, 60, 4, 7, 58, 6, 56},
    {45, 46, 16, 18, 19, 44, 47, 17, 20, 42, 43, 41, 21, 22, 40, 39, 23, 24, 38, 36, 26, 28, 25, 37, 34, 30, 27, 35, 32, 29, 33, 31},
    {10, 9, 55, 53, 52, 11, 8, 54, 51, 13, 12, 14, 50, 49, 15, 0, 48, 63, 1, 3, 61, 59, 62, 2, 5, 57, 60, 4, 7, 58, 6, 56},
    {45, 46, 16, 18, 19, 44, 47, 17, 20, 42, 43, 41, 21, 22, 40, 39, 23, 24, 38, 36, 26, 28, 25, 37, 34, 30, 27, 35, 32, 29, 33, 31}
  };

// ========================= State Machine Data =========================
// Valve commands:
//   INFLATE (ON):  finger=0, reservoir=1
//   DEFLATE:       finger=1, reservoir=0  (reverse - vents air)
//   OFF:           finger=0, reservoir=0
//
// IMPORTANT: PREFLATING also uses DEFLATE (reverse) briefly to pre-vent
// before switching to inflate. This ensures actuator is ready to fill.

// States: 0=OFF, 1=PREFLATING (reverse), 2=ON, 3=DEFLATING (reverse), 4=FORCED REVERSE
uint8_t bubbleState[6][32];         // Current state per actuator
uint8_t desired[6][32];             // From host: 0=OFF, 1=ON, 5=FORCE REVERSE
unsigned long stateTimer[6][32];    // Timer for state transitions

// Watchdog tracking
#if WATCHDOG_ENABLED
  unsigned long onStartTime[6][32];        // When actuator entered ON state
  bool watchdogLocked[6][32];              // Is actuator locked off?
  unsigned int watchdogTriggerCount[6][32];
#endif

// Hardware state
uint8_t vals[6][64];    // Valve bits per address
uint32_t output[6][2];  // Packed output for SPI

// ========================= SPI Settings =========================
SPISettings hv507SPI(8000000, LSBFIRST, SPI_MODE0);

// ========================= Forward Declarations =========================
void updateStateMachine();
void applyValveCommands();
void packBits(const uint8_t arr[64], uint32_t &lowOut, uint32_t &highOut);
void shiftOut384();
void enableHV();
void disableHV();
void parseSetCommand(String line);
#if WATCHDOG_ENABLED
  void updateWatchdog();
#endif

// ========================= Setup =========================
void setup() {
  pinMode(PIN_SHIFT_EN, OUTPUT);  digitalWrite(PIN_SHIFT_EN, HIGH);
  pinMode(PIN_BLANK, OUTPUT);     digitalWrite(PIN_BLANK, HIGH);
  pinMode(PIN_SPI_LE, OUTPUT);    digitalWrite(PIN_SPI_LE, HIGH);
  pinMode(PIN_HV_EN, OUTPUT);     digitalWrite(PIN_HV_EN, LOW);
  pinMode(PIN_HV_CTRL, OUTPUT);   digitalWrite(PIN_HV_CTRL, LOW);

  SPI.begin();

  // Initialize state machine
  for(int addr=0; addr<6; addr++){
    for(int pix=0; pix<32; pix++){
      bubbleState[addr][pix] = 0;  // All OFF
      desired[addr][pix] = 0;       // All OFF
      stateTimer[addr][pix] = 0;

      #if WATCHDOG_ENABLED
        onStartTime[addr][pix] = millis();
        watchdogLocked[addr][pix] = false;
        watchdogTriggerCount[addr][pix] = 0;
      #endif
    }
    for(int i=0; i<64; i++){
      vals[addr][i] = 0;
    }
  }

  Serial.begin(250000);
  delay(500);
  Serial.print("=== TEENSY Mitus Homer v5: ");
  Serial.print(NUM_HV507_CHIPS);
  Serial.println(" HV507 chip(s), State Machine Mode ===");
  Serial.println("State Machine: Preflate=" + String(T_PREFLATE) + "ms, Deflate=" + String(T_DEFLATE) + "ms");
  Serial.print("Send: SET <addr> <pixel> <level>   (addr=0..");
  Serial.print(NUM_HV507_CHIPS-1);
  Serial.println(", pixel=0..31, level: 0=OFF 1-4=ON 5=REVERSE)");

  #if WATCHDOG_ENABLED
    Serial.print("Watchdog enabled: ");
    Serial.print(WATCHDOG_TIMEOUT_MS);
    Serial.println("ms max on-time");
  #endif

  // Turn on HV supply
  enableHV();
  Serial.println("HV supply enabled. Ready.\n");
}

// ========================= Main Loop =========================
void loop() {
  static unsigned long lastStateUpdate = 0;

  // Update state machine periodically
  if(millis() - lastStateUpdate >= STATE_UPDATE_INTERVAL) {
    updateStateMachine();
    lastStateUpdate = millis();
  }

  #if WATCHDOG_ENABLED
    static unsigned long lastWatchdogCheck = 0;
    if(millis() - lastWatchdogCheck >= WATCHDOG_CHECK_INTERVAL_MS) {
      updateWatchdog();
      lastWatchdogCheck = millis();
    }
  #endif

  // Process serial commands
  if(Serial.available()>0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if(line.startsWith("SET ")) {
      parseSetCommand(line.substring(4));
      if(DEBUG_SERIAL) Serial.println("OK");
    }
    else if(line.length()>0) {
      Serial.print("Unknown cmd: ");
      Serial.println(line);
    }
  }
}

// ========================= State Machine Update =========================
// Runs every STATE_UPDATE_INTERVAL ms
void updateStateMachine() {
  unsigned long now = millis();
  bool needsUpdate = false;

  for(int addr=0; addr<NUM_HV507_CHIPS; addr++){
    for(int pix=0; pix<32; pix++){
      uint8_t state = bubbleState[addr][pix];
      uint8_t want = desired[addr][pix];

      // Force reverse overrides any state
      if(want == 5){
        if(state != 4){
          bubbleState[addr][pix] = 4;  // Forced reverse
          needsUpdate = true;
          if(DEBUG_SERIAL){
            Serial.print("Addr ");
            Serial.print(addr);
            Serial.print(" Pix ");
            Serial.print(pix);
            Serial.println(": -> FORCED REVERSE");
          }
        }
        continue;  // Skip normal state machine
      }

      switch(state) {
        case 0:  // OFF
          if(want > 0){
            bubbleState[addr][pix] = 1;  // Start preflating
            stateTimer[addr][pix] = now + T_PREFLATE;
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": OFF -> PREFLATING");
            }
          }
          break;

        case 1:  // PREFLATING
          if(want == 0){
            // Abort inflation, go back to OFF
            bubbleState[addr][pix] = 0;
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": PREFLATING -> OFF (aborted)");
            }
          } else if(now >= stateTimer[addr][pix]){
            // Preflate complete, go to ON
            bubbleState[addr][pix] = 2;
            #if WATCHDOG_ENABLED
              onStartTime[addr][pix] = now;  // Start watchdog timer
            #endif
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": PREFLATING -> ON");
            }
          }
          break;

        case 2:  // ON
          if(want == 0){
            // Start deflating
            bubbleState[addr][pix] = 3;
            stateTimer[addr][pix] = now + T_DEFLATE;
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": ON -> DEFLATING");
            }
          }
          break;

        case 3:  // DEFLATING
          if(want > 0){
            // Abort deflation, go back to ON
            bubbleState[addr][pix] = 2;
            #if WATCHDOG_ENABLED
              onStartTime[addr][pix] = now;  // Restart watchdog timer
            #endif
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": DEFLATING -> ON (aborted)");
            }
          } else if(now >= stateTimer[addr][pix]){
            // Deflate complete, go to OFF
            bubbleState[addr][pix] = 0;
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": DEFLATING -> OFF");
            }
          }
          break;

        case 4:  // FORCED REVERSE (stays here until desired changes)
          if(want == 0){
            bubbleState[addr][pix] = 0;  // Go to OFF
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": FORCED REVERSE -> OFF");
            }
          } else if(want >= 1 && want <= 4){
            bubbleState[addr][pix] = 1;  // Go to preflating
            stateTimer[addr][pix] = now + T_PREFLATE;
            needsUpdate = true;
            if(DEBUG_SERIAL){
              Serial.print("Addr ");
              Serial.print(addr);
              Serial.print(" Pix ");
              Serial.print(pix);
              Serial.println(": FORCED REVERSE -> PREFLATING");
            }
          }
          break;
      }
    }
  }

  if(needsUpdate){
    applyValveCommands();
  }
}

// ========================= Apply Valve Commands =========================
// Convert states to valve commands and shift out
void applyValveCommands() {
  for(int addr=0; addr<NUM_HV507_CHIPS; addr++){
    for(int pix=0; pix<32; pix++){
      uint8_t state = bubbleState[addr][pix];

      // Map states to valve positions
      if(state == 1){
        // PREFLATING: finger=1, reservoir=0 (REVERSE/DEFLATE - pre-vent before inflate)
        vals[addr][ finger[addr][pix] ]    = 1;
        vals[addr][ reservoir[addr][pix] ] = 0;
      } else if(state == 2){
        // ON: finger=0, reservoir=1 (FORWARD/INFLATE - hold pressure)
        vals[addr][ finger[addr][pix] ]    = 0;
        vals[addr][ reservoir[addr][pix] ] = 1;
      } else if(state == 3 || state == 4){
        // DEFLATING / FORCED REVERSE: finger=1, reservoir=0 (REVERSE - vent air)
        vals[addr][ finger[addr][pix] ]    = 1;
        vals[addr][ reservoir[addr][pix] ] = 0;
      } else {
        // OFF: finger=0, reservoir=0 (both closed)
        vals[addr][ finger[addr][pix] ]    = 0;
        vals[addr][ reservoir[addr][pix] ] = 0;
      }
    }

    // Pack bits for this address
    packBits(vals[addr], output[addr][0], output[addr][1]);
  }

  // Shift out all chips
  shiftOut384();
}

// ========================= Watchdog =========================
#if WATCHDOG_ENABLED
void updateWatchdog() {
  unsigned long now = millis();
  bool anyTriggered = false;

  for(int addr=0; addr<NUM_HV507_CHIPS; addr++){
    for(int pix=0; pix<32; pix++){
      // Only check actuators in ON state (state 2)
      if(bubbleState[addr][pix] == 2 && !watchdogLocked[addr][pix]) {
        if(now - onStartTime[addr][pix] >= WATCHDOG_TIMEOUT_MS) {
          // Force OFF and lock
          desired[addr][pix] = 0;
          watchdogLocked[addr][pix] = true;
          watchdogTriggerCount[addr][pix]++;
          anyTriggered = true;

          if(DEBUG_SERIAL){
            Serial.print("[WATCHDOG] Forced OFF: Addr ");
            Serial.print(addr);
            Serial.print(" Pix ");
            Serial.print(pix);
            Serial.print(" (trigger #");
            Serial.print(watchdogTriggerCount[addr][pix]);
            Serial.println(")");
          }
        }
      }

      // Unlock when state returns to OFF (state 0)
      if(watchdogLocked[addr][pix] && bubbleState[addr][pix] == 0) {
        watchdogLocked[addr][pix] = false;
        if(DEBUG_SERIAL){
          Serial.print("[WATCHDOG] Unlocked: Addr ");
          Serial.print(addr);
          Serial.print(" Pix ");
          Serial.println(pix);
        }
      }
    }
  }

  if(anyTriggered){
    // State machine will handle the transition on next update
  }
}
#endif

// ========================= Parse SET Command =========================
void parseSetCommand(String line) {
  char buf[128];
  line.toCharArray(buf, 128);

  int tokens[64];
  int tokenCount=0;
  char *p = strtok(buf," ");
  while(p && tokenCount<64) {
    tokens[tokenCount++] = atoi(p);
    p = strtok(NULL," ");
  }

  if(tokenCount<3) {
    Serial.println("Format error. e.g. SET 0 0 1 or SET 0 0 1 1 0");
    return;
  }

  int address = tokens[0];
  if(address<0 || address>=NUM_HV507_CHIPS) {
    Serial.print("Invalid address (must be 0-");
    Serial.print(NUM_HV507_CHIPS-1);
    Serial.print("): ");
    Serial.println(address);
    return;
  }

  // Parse pixel/level pairs
  if(((tokenCount - 1) % 2)!=0) {
    Serial.println("Pixel/level pairs mismatch!");
    return;
  }

  for(int i=1; i<tokenCount; i+=2) {
    int pix = tokens[i];
    int lvl = tokens[i+1];

    if(pix<0 || pix>=32) {
      Serial.print("Pixel out of range: ");
      Serial.println(pix);
      continue;
    }

    if(lvl<0 || lvl>5) {
      Serial.print("Level out of range (0-5): ");
      Serial.println(lvl);
      continue;
    }

    #if WATCHDOG_ENABLED
      // Reject ON commands if watchdog locked (but allow level 5 = force reverse)
      if(lvl >= 1 && lvl <= 4 && watchdogLocked[address][pix]) {
        if(DEBUG_SERIAL){
          Serial.print("[WATCHDOG] Rejected ON: Addr ");
          Serial.print(address);
          Serial.print(" Pix ");
          Serial.print(pix);
          Serial.println(" (locked)");
        }
        continue;
      }
    #endif

    // Set desired state: 0=OFF, 1-4=ON, 5=FORCE REVERSE
    if(lvl == 5){
      desired[address][pix] = 5;
    } else {
      desired[address][pix] = (lvl > 0) ? 1 : 0;
    }
  }
}

// ========================= SPI Functions =========================
void packBits(const uint8_t arr[64], uint32_t &lowOut, uint32_t &highOut) {
  uint32_t temp=0;
  for(int j=0; j<32; j++){
    temp |= ((arr[j] & 1) << j);
  }
  lowOut = temp;

  temp=0;
  for(int j=32; j<64; j++){
    temp |= ((arr[j] & 1) << (j-32));
  }
  highOut = temp;
}

void shiftOut384() {
  digitalWriteFast(PIN_SPI_LE, LOW);
  SPI.beginTransaction(hv507SPI);

  for(int addr=NUM_HV507_CHIPS-1; addr>=0; addr--){
    SPI.transfer(byte(output[addr][0] >> 0));
    SPI.transfer(byte(output[addr][0] >> 8));
    SPI.transfer(byte(output[addr][0] >> 16));
    SPI.transfer(byte(output[addr][0] >> 24));
    SPI.transfer(byte(output[addr][1] >> 0));
    SPI.transfer(byte(output[addr][1] >> 8));
    SPI.transfer(byte(output[addr][1] >> 16));
    SPI.transfer(byte(output[addr][1] >> 24));
  }

  SPI.endTransaction();
  digitalWriteFast(PIN_SPI_LE, HIGH);
}

// ========================= HV Control =========================
void enableHV() {
  digitalWrite(PIN_HV_EN, HIGH);
  delay(1000); // let HV ramp
  digitalWrite(PIN_HV_CTRL, HIGH);
}

void disableHV() {
  digitalWrite(PIN_HV_EN, LOW);
  digitalWrite(PIN_HV_CTRL, LOW);
}
