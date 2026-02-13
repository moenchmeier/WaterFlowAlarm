#include <M5Atom.h>
#include <driver/i2s.h>
#include <math.h>

// === Pins ===
#define FLOW_SENSOR_PIN 32
#define AMP_ENABLE_PIN  26
#define I2S_DOUT        22
#define I2S_BCLK        19
#define I2S_WS          33

// === Flow timing ===
#define ALARM_TIME 60000  // 60 seconds

// === Audio state ===
enum AudioState {
  IDLE,
  PLAYING_START,
  PLAYING_BACKGROUND,
  PLAYING_ALARM
};

AudioState currentAudioState = IDLE;

// === Flow state ===
bool waterFlowing = false;
bool alarmTriggered = false;
bool lastSwitchState = false;
unsigned long flowStartTime = 0;
unsigned long lastSwitchCheck = 0;

// === LED update ===
unsigned long lastLEDUpdate = 0;

// === Volume settings ===
const int VOLUME_BACKGROUND = 5000;
const int VOLUME_START = 12000;
const int VOLUME_ALARM = 10000;

// === Melody definitions ===
const int FLOW_START_MELODY[] = {323, 387, 470};          // C5 D5 G5
const int FLOW_START_DURATIONS[] = {100, 100, 100};

const int FLOW_BEEP_FREQ = 440;  // A4
const int FLOW_BEEP_DURATION = 100;

const int ALARM_HIGH = 500;     // C6
const int ALARM_LOW = 600;       // F#5
const int ALARM_DURATION = 100;

// === Setup I2S ===
void initI2S() {
  const i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ALL_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  const i2s_pin_config_t pin_cfg = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_cfg);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// === Play tone via I2S ===
void playTone(int freq, int duration_ms, int amplitude = 6000) {
  const int sampleRate = 44100;
  const int samples = (sampleRate * duration_ms) / 1000;
  const float twoPiF = 2.0 * 3.14159f * freq / sampleRate;

  for (int i = 0; i < samples; ++i) {
    int16_t sample = amplitude * sinf(i * twoPiF);
    size_t bytes_written;
    i2s_write(I2S_NUM_0, &sample, sizeof(sample), &bytes_written, portMAX_DELAY);
  }
}

// === Sound logic ===
void playStartSound() {
  Serial.println("[Audio] Playing start melody...");
  for (int i = 0; i < 3; i++) {
    playTone(FLOW_START_MELODY[i], FLOW_START_DURATIONS[i], VOLUME_START);
    delay(50);
  }
}

void playBackgroundTone() {
  static unsigned long lastBeep = 0;
  if (millis() - lastBeep > 6000) {
    Serial.println("[Audio] Background beep...");
    playTone(FLOW_BEEP_FREQ, FLOW_BEEP_DURATION, VOLUME_BACKGROUND);
    lastBeep = millis();
  }
}

void playAlarmSound() {
  static unsigned long lastAlarm = 0;
  static bool isHigh = true;
  if (millis() - lastAlarm > ALARM_DURATION + 100) {
    playTone(isHigh ? ALARM_HIGH : ALARM_LOW, ALARM_DURATION, VOLUME_ALARM);
    Serial.println("[Audio] Alarm tone: " + String(isHigh ? ALARM_HIGH : ALARM_LOW));
    isHigh = !isHigh;
    lastAlarm = millis();
  }
}

// === LED feedback ===
void updateLED() {
  if (millis() - lastLEDUpdate < 100) return;

  if (waterFlowing) {
    if (alarmTriggered) {
      static bool ledState = false;
      static unsigned long lastBlink = 0;
      if (millis() - lastBlink > 500) {
        ledState = !ledState;
        M5.dis.drawpix(0, ledState ? 0xff0000 : 0x000000);
        lastBlink = millis();
      }
    } else {
      M5.dis.drawpix(0, 0x0000ff);  // Blue
    }
  } else {
    M5.dis.drawpix(0, 0x00ff00);    // Green
  }

  lastLEDUpdate = millis();
}

// === Setup ===
void setup() {
  M5.begin(true, false, true);
  Serial.begin(115200);
  delay(500);

  Serial.println("=== Atom Echo Water Flow Monitor ===");

  // Enable amplifier
  pinMode(AMP_ENABLE_PIN, OUTPUT);
  digitalWrite(AMP_ENABLE_PIN, HIGH);

  initI2S();

  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  M5.dis.drawpix(0, 0x00ff00);
  Serial.println("System ready. Waiting for water...");
}

// === Loop ===
void loop() {
  M5.update();

  // Switch check
  if (millis() - lastSwitchCheck > 50) {
    bool currentSwitchState = !digitalRead(FLOW_SENSOR_PIN); // Inverted logic

    if (currentSwitchState && !lastSwitchState && !waterFlowing) {
      waterFlowing = true;
      alarmTriggered = false;
      flowStartTime = millis();
      currentAudioState = PLAYING_START;
      Serial.println("*** Water flow STARTED! ***");
      playStartSound();

    } else if (!currentSwitchState && lastSwitchState && waterFlowing) {
      waterFlowing = false;
      alarmTriggered = false;
      currentAudioState = IDLE;
      Serial.println("*** Water flow STOPPED ***");
      Serial.println("Status: Waiting for water...");
    }

    lastSwitchState = currentSwitchState;
    lastSwitchCheck = millis();
  }

  // Audio logic
  if (waterFlowing) {
    unsigned long duration = millis() - flowStartTime;

    if (duration > ALARM_TIME && !alarmTriggered) {
      alarmTriggered = true;
      currentAudioState = PLAYING_ALARM;
      Serial.println("[ALARM] Water has been flowing too long!");
    }

    if (currentAudioState == PLAYING_START && duration > 1000) {
      currentAudioState = PLAYING_BACKGROUND;
      Serial.println("[Audio] Switching to background beeping.");
    }

    if (currentAudioState == PLAYING_BACKGROUND && !alarmTriggered) {
      playBackgroundTone();
    }

    if (currentAudioState == PLAYING_ALARM) {
      playAlarmSound();
    }

    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 5000) {
      Serial.println("Water flowing for " + String(duration / 1000) + " seconds");
      lastStatus = millis();
    }
  }

  updateLED();

  // Debug: button state
  if (M5.Btn.wasPressed()) {
    bool switchState = !digitalRead(FLOW_SENSOR_PIN);
    Serial.println("=== BUTTON TEST ===");
    Serial.println("Switch: " + String(switchState ? "CLOSED (flowing)" : "OPEN (stopped)"));
    Serial.println("Water flowing: " + String(waterFlowing ? "YES" : "NO"));
    Serial.println("===================");
  }

  delay(10);
}