// Combined: LM335 sauna thermostat + 5-LED button timer
// Board: Wemos D1 Mini Pro (ESP8266)
//
// Thermostat
//   Sensor:  5V --[2.2k]--+-- LM335 + (middle leg)
//                         |
//                         +--[100k]-- A0      (extends A0 range to ~3.96 V)
//                    LM335 - (GND leg) -- GND
//                    LM335 ADJ -- not connected
//   Heater:  D2 (GPIO4) --[330R]-- red LED -- GND
//
// Timer
//   Button:  D1 (GPIO5) -- button -- GND   (internal pull-up)
//   LEDs:    D8, D7, D6, D5, D0 (GPIO15, 13, 12, 14, 16) --[330R]-- LED -- GND

// ===================== THERMOSTAT =====================
const int sensorPin = A0;
const int heaterPin = 4;             // D2 - red LED = heater

const float adcMaxVolt = 3.96;       // A0 range with the extra 100k resistor
float calOffset = 0.0;               // °C, fine trim (or use the r command)

float setPoint   = 25.0;             // °C
float hysteresis = 2.0;              // °C, total band width
bool  heaterOn   = false;

float lastRaw  = 0;
float lastVolt = 0;
float lastTemp = 0;
unsigned long lastPrint = 0;

// ===================== TIMER =====================
const int buttonPin = 5;                       // D1
const int leds[] = {15, 13, 12, 14, 16};       // LED1..LED5 (D8, D7, D6, D5, D0)
const int numLeds = 5;

const unsigned long stepTime  = 2000;          // 2 s per LED
const int blinkCount          = 5;
const unsigned long blinkTime = 300;           // on/off time while blinking

enum TimerState { T_IDLE, T_COUNTDOWN, T_BLINK };
TimerState timerState = T_IDLE;
int timerStep = 0;                             // which LED turns off next
int blinkPhase = 0;                            // counts on/off half-cycles
unsigned long timerMillis = 0;

// ---------------- Thermostat functions ----------------
float readTempC() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {     // average 20 samples to reduce noise
    sum += analogRead(sensorPin);
    delay(1);
  }
  lastRaw  = sum / 20.0;
  lastVolt = lastRaw * adcMaxVolt / 1023.0;
  float kelvin = lastVolt * 100.0;   // LM335: 10 mV per kelvin
  return kelvin - 273.15 + calOffset;
}

void handleSerial() {
  // Commands (Serial Monitor, line ending = New Line):
  //   s25   -> setpoint 25 °C
  //   h1.5  -> hysteresis 1.5 °C
  //   c-2   -> calibration offset -2 °C
  //   r22   -> real temperature is 22 °C, auto-calculate offset
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() < 2) return;
  char c = tolower(cmd.charAt(0));
  float val = cmd.substring(1).toFloat();

  if (c == 's') {
    setPoint = val;
    Serial.printf(">> Setpoint = %.1f C\n", setPoint);
  } else if (c == 'h' && val >= 0) {
    hysteresis = val;
    Serial.printf(">> Hysteresis = %.1f C\n", hysteresis);
  } else if (c == 'c') {
    calOffset = val;
    Serial.printf(">> Cal offset = %.1f C\n", calOffset);
  } else if (c == 'r') {
    calOffset += val - lastTemp;
    Serial.printf(">> Calibrated to %.1f C (offset now %.1f C)\n", val, calOffset);
  } else {
    Serial.println(">> Unknown command. Use s<temp>, h<band>, c<offset>, r<real temp>");
  }
}

void updateThermostat() {
  float t = readTempC();
  lastTemp = t;

  float lowLimit  = setPoint - hysteresis / 2.0;
  float highLimit = setPoint + hysteresis / 2.0;

  if (t < lowLimit)       heaterOn = true;
  else if (t > highLimit) heaterOn = false;
  // between the limits: keep previous state

  digitalWrite(heaterPin, heaterOn ? HIGH : LOW);

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.printf("raw: %4.0f | %.3f V | Temp: %5.1f C | Set: %.1f C (on <%.1f, off >%.1f) | Heater: %s\n",
                  lastRaw, lastVolt, t, setPoint, lowLimit, highLimit, heaterOn ? "ON" : "OFF");
    if (lastRaw >= 1020) {
      Serial.println("   !! A0 maxed out - check sensor wiring");
    }
  }
}

// ---------------- Timer functions ----------------
void allLeds(int state) {
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(leds[i], state);
  }
}

// Returns true once per button press (debounced)
bool buttonPressed() {
  static int lastReading = HIGH;
  static int stableState = HIGH;
  static unsigned long lastChange = 0;

  int reading = digitalRead(buttonPin);
  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }
  if (millis() - lastChange > 30 && reading != stableState) {
    stableState = reading;
    if (stableState == LOW) return true;
  }
  return false;
}

void updateTimer() {
  bool pressed = buttonPressed();
  unsigned long now = millis();

  switch (timerState) {
    case T_IDLE:
      if (pressed) {
        Serial.println("Timer started");
        allLeds(HIGH);
        timerStep = 0;
        timerMillis = now;
        timerState = T_COUNTDOWN;
      }
      break;

    case T_COUNTDOWN:
      if (now - timerMillis >= stepTime) {
        digitalWrite(leds[timerStep], LOW);
        Serial.printf("LED%d off\n", timerStep + 1);
        timerStep++;
        timerMillis = now;
        if (timerStep >= numLeds) {    // all off -> start blinking
          allLeds(HIGH);
          blinkPhase = 0;
          timerState = T_BLINK;
        }
      }
      break;

    case T_BLINK:
      if (now - timerMillis >= blinkTime) {
        timerMillis = now;
        blinkPhase++;
        if (blinkPhase >= blinkCount * 2) {
          allLeds(LOW);
          Serial.println("Timer done - press the button again");
          timerState = T_IDLE;
        } else {
          allLeds(blinkPhase % 2 == 0 ? HIGH : LOW);
        }
      }
      break;
  }
}

// ===================== MAIN =====================
void setup() {
  Serial.begin(115200);

  pinMode(heaterPin, OUTPUT);
  digitalWrite(heaterPin, LOW);

  pinMode(buttonPin, INPUT_PULLUP);
  for (int i = 0; i < numLeds; i++) {
    pinMode(leds[i], OUTPUT);
  }
  allLeds(LOW);

  delay(200);
  Serial.println("\nSauna thermostat + LED timer");
  Serial.println("Thermostat commands: s<setpoint>  h<hysteresis>  c<offset>  r<real temp>");
  Serial.println("Press the button on D1 to start the timer");
}

void loop() {
  handleSerial();
  updateThermostat();
  updateTimer();
}