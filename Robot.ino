#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_PWMServoDriver.h>
#include "HX711.h"
#include <ArduinoJson.h>
#include <Preferences.h>

// ============================================================
//   NETWORK & MQTT
// ============================================================
const char* ssid        = "fyp lab";
const char* password    = "123456789";
const char* mqtt_server = "10.9.9.215";
const int   mqtt_port   = 1883;

WiFiClient   espClient;
PubSubClient client(espClient);

// ============================================================
//   PCA9685
// ============================================================
Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver(0x40);

#define CH_LEFT_HIP    0
#define CH_LEFT_KNEE   1
#define CH_LEFT_ANKLE  2
#define CH_RIGHT_HIP   3
#define CH_RIGHT_KNEE  4
#define CH_RIGHT_ANKLE 5

#define SERVO_MIN 125
#define SERVO_MAX 575

// ============================================================
//   PIN DEFINITIONS
// ============================================================
const int L_DOUT = 4,  L_SCK  = 5;
const int R_DOUT = 16, R_SCK  = 17;

const int EMG_LEFT  = 34;
const int EMG_RIGHT = 35;

// ============================================================
//   ANGLE VARIABLES
//   t = target (الهدف)
//   c = current (ما يُرسل فعلياً للسيرفو)
// ============================================================
float tLH=90, tLK=90, tLA=90, tRH=90, tRK=90, tRA=90;
float cLH=90, cLK=90, cLA=90, cRH=90, cRK=90, cRA=90;

const float LERP = 0.12f;

// ============================================================
//   LOAD CELLS
// ============================================================
HX711 scaleLeft;
HX711 scaleRight;

// ============================================================
//   PREFERENCES
// ============================================================
Preferences prefs;

// ============================================================
//   SYSTEM STATE
// ============================================================
String currentExercise = "walk";
int    currentPhase    = 1;
bool   exerciseRunning = false;
float  maxWeight       = 3.0;
String childName       = "";

const float EMG_THRESHOLD = 120.0;

// ============================================================
//   TIMING VARIABLES
// ============================================================
unsigned long lastPublish             = 0;
const unsigned long PUBLISH_INTERVAL  = 4000;

unsigned long lastReconnectAttempt    = 0;
const unsigned long RECONNECT_INTERVAL = 5000;

unsigned long lastGaitStep = 0;
const unsigned long GAIT_STEP_INTERVAL = 15;

unsigned long lastHighKneeStep = 0;
const unsigned long HIGH_KNEE_INTERVAL = 350;
bool highKneeUp = false;

float gaitT   = 0.0;
int   gaitLeg = 0;

// ============================================================
//   مساعد
// ============================================================
uint16_t angleToPWM(float angle) {
  angle = constrain(angle, 0, 180);
  return (uint16_t)map((long)angle, 0, 180, SERVO_MIN, SERVO_MAX);
}

void writeServos() {
  cLH += (tLH - cLH) * LERP;
  cLK += (tLK - cLK) * LERP;
  cLA += (tLA - cLA) * LERP;
  cRH += (tRH - cRH) * LERP;
  cRK += (tRK - cRK) * LERP;
  cRA += (tRA - cRA) * LERP;
  pca.setPWM(CH_LEFT_HIP,   0, angleToPWM(cLH));
  pca.setPWM(CH_LEFT_KNEE,  0, angleToPWM(cLK));
  pca.setPWM(CH_LEFT_ANKLE, 0, angleToPWM(cLA));
  pca.setPWM(CH_RIGHT_HIP,  0, angleToPWM(cRH));
  pca.setPWM(CH_RIGHT_KNEE, 0, angleToPWM(cRK));
  pca.setPWM(CH_RIGHT_ANKLE,0, angleToPWM(cRA));
}
// ============================================================
//   PREFERENCES — حفظ وقراءة الزوايا
// ============================================================
void saveAngles() {
  prefs.begin("angles", false);
  prefs.putFloat("cLH", cLH);
  prefs.putFloat("cLK", cLK);
  prefs.putFloat("cLA", cLA);
  prefs.putFloat("cRH", cRH);
  prefs.putFloat("cRK", cRK);
  prefs.putFloat("cRA", cRA);
  prefs.end();
}
void loadAngles() {
  prefs.begin("angles", true);
  cLH = prefs.getFloat("cLH", 90);
  cLK = prefs.getFloat("cLK", 90);
  cLA = prefs.getFloat("cLA", 90);
  cRH = prefs.getFloat("cRH", 90);
  cRK = prefs.getFloat("cRK", 90);
  cRA = prefs.getFloat("cRA", 90);
  prefs.end();

  // ابدأ المستهدف من نفس الزاوية المحفوظة
  tLH = cLH; tLK = cLK; tLA = cLA;
  tRH = cRH; tRK = cRK; tRA = cRA;

  // أرسل للسيرفو مباشرة بدون LERP
  pca.setPWM(CH_LEFT_HIP,   0, angleToPWM(cLH));
  pca.setPWM(CH_LEFT_KNEE,  0, angleToPWM(cLK));
  pca.setPWM(CH_LEFT_ANKLE, 0, angleToPWM(cLA));
  pca.setPWM(CH_RIGHT_HIP,  0, angleToPWM(cRH));
  pca.setPWM(CH_RIGHT_KNEE, 0, angleToPWM(cRK));
  pca.setPWM(CH_RIGHT_ANKLE,0, angleToPWM(cRA));

  Serial.printf("📂 Loaded angles: LH=%.1f LK=%.1f LA=%.1f | RH=%.1f RK=%.1f RA=%.1f\n",
                cLH, cLK, cLA, cRH, cRK, cRA);
}
// ============================================================
//   NEUTRAL POSITION — بهدوء عبر LERP
// ============================================================
void neutralPosition() {
  tLH = tLK = tLA = 90;
  tRH = tRK = tRA = 90;

  for (int i = 0; i < 80; i++) {
    writeServos();
    delay(20);
  }

  saveAngles();
}
// ============================================================
//   SENSOR READING
// ============================================================
float readEMG(int pin) {
  int val = analogRead(pin);
  if (val < 10) return 0.0;
  return constrain((val / 4095.0) * 100.0, 0.0, 100.0);
}
float readWeight(HX711 &scale) {
  if (!scale.is_ready()) return 0.0;
  float w = scale.get_units(3);
  return (w < 0) ? 0.0 : w;
}
// ============================================================
//   EXERCISE 1 — WALKING PHASE 1 (Passive, Non-blocking)
// ============================================================
void walking_phase1() {
  unsigned long now = millis();
  if (now - lastGaitStep < GAIT_STEP_INTERVAL) return;
  lastGaitStep = now;

  static float phase = 0.0;
  phase += 0.018;
  if (phase > TWO_PI) phase -= TWO_PI;

  float phL = phase;
  float phR = phase + PI;

  const float HIP_FLEX   = 25.0;
  const float HIP_EXT    = 10.0;
  const float KNEE_SWING = 35.0;
  const float ANKLE_PUSH = 10.0;
  const float ANKLE_PULL = 8.0;

  // ================== LEFT LEG ==================
  float sL     = sin(phL);
  float swingL = constrain(sL * 5.0f, 0.0f, 1.0f);

  tLH = 90 + (sL > 0 ? HIP_FLEX : HIP_EXT) * sL;
  tLK = 90 - KNEE_SWING * sin(phL) * max(0.0f, sL) * swingL;
  tLA = 90 + (sL > 0 ? -ANKLE_PULL * sL : ANKLE_PUSH * (-sL)) * swingL;

  // ================== RIGHT LEG (MIRRORED) ==================
  float sR     = sin(phR);
  float swingR = constrain(sR * 5.0f, 0.0f, 1.0f);

  float rawRH = 90 + (sR > 0 ? HIP_FLEX : HIP_EXT) * sR;
  float rawRK = 90 - KNEE_SWING * sin(phR) * max(0.0f, sR) * swingR;
  float rawRA = 90 + (sR > 0 ? -ANKLE_PULL * sR : ANKLE_PUSH * (-sR)) * swingR;

  // 🔁 mirror
  tRH = 180 - rawRH;
  tRK = 180 - rawRK;
  tRA = 180 - rawRA;

  writeServos();
}
// ============================================================
//   EXERCISE 2 — WALKING PHASE 2 (EMG Assist)
// ============================================================
void walking_phase2() {
  float emgAvg = (readEMG(EMG_LEFT) + readEMG(EMG_RIGHT)) / 2.0;

  if (emgAvg < EMG_THRESHOLD) { neutralPosition(); return; }

  unsigned long now = millis();
  if (now - lastGaitStep < GAIT_STEP_INTERVAL) return;
  lastGaitStep = now;

  float al = constrain((emgAvg - EMG_THRESHOLD) / (100.0 - EMG_THRESHOLD), 0.0, 1.0);

  gaitT += 0.04 * (0.5 + al);
  if (gaitT > 1.0) { gaitT = 0.0; gaitLeg = 1 - gaitLeg; }

  float swing = sin(gaitT * PI);
  float amp   = 20 + al * 30;

  if (gaitLeg == 0) {
    tRH = 90 + 20 * gaitT * al; tRK = 90 - amp * swing; tRA = 90 + 8 * swing;
    tLH = 90; tLK = 90; tLA = 90;
  } else {
    tLH = 90 + 20 * gaitT * al; tLK = 90 - amp * swing; tLA = 90 + 8 * swing;
    tRH = 90; tRK = 90; tRA = 90;
  }
  writeServos();
}
// ============================================================
//   EXERCISE 3 — HIGH KNEE PHASE 1 (Passive, Non-blocking)
// ============================================================
void highknee_phase1() {
  unsigned long now = millis();
  if (now - lastHighKneeStep < HIGH_KNEE_INTERVAL) return;
  lastHighKneeStep = now;

  highKneeUp = !highKneeUp;
  if (highKneeUp) { tLK = 130; tRK = 130; tLH = 80; tRH = 80; }
  else            { tLK = 90;  tRK = 90;  tLH = 90; tRH = 90; }
  tLA = 90; tRA = 90;
  writeServos();
}

// ============================================================
//   EXERCISE 4 — HIGH KNEE PHASE 2 (EMG Assist)
// ============================================================
void highknee_phase2() {
  float emgAvg = (readEMG(EMG_LEFT) + readEMG(EMG_RIGHT)) / 2.0;

  if (emgAvg < EMG_THRESHOLD) { neutralPosition(); return; }

  float al = constrain((emgAvg - EMG_THRESHOLD) / (100.0 - EMG_THRESHOLD), 0.0, 1.0);

  tLK = tRK = 90 + al * 50;
  tLH = tRH = 90 - al * 15;
  tLA = tRA = 90;
  writeServos();
}

// ============================================================
//   PUBLISH SENSOR DATA
// ============================================================
void publishSensorData() {
  if (!client.connected()) return;

  float emgL    = readEMG(EMG_LEFT);
  float emgR    = readEMG(EMG_RIGHT);
  float weightL = readWeight(scaleLeft);
  float weightR = readWeight(scaleRight);

  float totalW = weightL + weightR;
  float emgAvg = (emgL + emgR) / 2.0;

  String payload = "{";
  payload += "\"joint_angles\":{";
  payload += "\"Left_Hip\":"    + String(cLH,1) + ",";
  payload += "\"Left_Knee\":"   + String(cLK,1) + ",";
  payload += "\"Left_Ankle\":"  + String(cLA,1) + ",";
  payload += "\"Right_Hip\":"   + String(cRH,1) + ",";
  payload += "\"Right_Knee\":"  + String(cRK,1) + ",";
  payload += "\"Right_Ankle\":" + String(cRA,1);
  payload += "},";
  payload += "\"currentWeight\":"  + String(totalW,2) + ",";
  payload += "\"muscleStrength\":" + String(emgAvg,1);
  payload += "}";

  client.publish("BushraIzzaHafsa/robot/sensor_data", payload.c_str());

  Serial.println("\n📤 Published Payload:");
  Serial.println(payload);
}

// ============================================================
//   MQTT COMMAND HANDLER
// ============================================================
void processCommand(String topic, String message) {

  Serial.printf("\n📩 Topic: %s\nPayload: %s\n", topic.c_str(), message.c_str());

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.println("❌ JSON Parsing Failed");
    return;
  }

  // ================= START =================
  if (topic == "BushraIzzaHafsa/robot/control/start") {

    String action = doc["action"] | "";

    if (action == "start") {

      currentExercise = doc["exercise"] | "walk";

      if (doc["phase"].is<const char*>())
        currentPhase = String((const char*)doc["phase"]).toInt();
      else
        currentPhase = doc["phase"] | 1;

      maxWeight = doc["max_weight"] | 30.0;

      neutralPosition();

      gaitT      = 0.0;
      gaitLeg    = 0;
      highKneeUp = false;

      exerciseRunning = true;
      lastPublish     = millis();

      Serial.printf("▶️ START → %s | Phase:%d | MaxW:%.1f\n",
                    currentExercise.c_str(), currentPhase, maxWeight);

      client.publish("BushraIzzaHafsa/robot/status", "{\"status\":\"started\"}");
      publishSensorData();
    }
    return;
  }

  // ================= STOP =================
  if (topic == "BushraIzzaHafsa/robot/control/stop") {

    exerciseRunning = false;
    neutralPosition();

    gaitT      = 0.0;
    gaitLeg    = 0;
    highKneeUp = false;

    Serial.println("⏹️ STOP");

    client.publish("BushraIzzaHafsa/robot/status", "{\"status\":\"stopped\"}");
    return;
  }

  // ================= EMERGENCY =================
  if (topic == "BushraIzzaHafsa/robot/control/emergency_stop") {

    exerciseRunning = false;
    neutralPosition();

    gaitT      = 0.0;
    gaitLeg    = 0;
    highKneeUp = false;

    Serial.println("🆘 EMERGENCY STOP");

    client.publish("BushraIzzaHafsa/robot/status", "{\"status\":\"stopped\"}");
    return;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  processCommand(String(topic), message);
}

// ============================================================
//   WIFI
// ============================================================
void setup_wifi() {
  Serial.printf("📶 Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500); Serial.print("."); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n✅ WiFi Connected | IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\n❌ WiFi Failed");
}

// ============================================================
//   MQTT RECONNECT
// ============================================================
boolean reconnect() {
  String clientId = "ESP32Rehab_" + String(random(0xffff), HEX);
  Serial.printf("🔄 MQTT connecting as %s ...", clientId.c_str());

  if (client.connect(clientId.c_str())) {
    Serial.println(" ✅");
    client.subscribe("BushraIzzaHafsa/robot/control/start");
    client.subscribe("BushraIzzaHafsa/robot/control/stop");
    client.subscribe("BushraIzzaHafsa/robot/control/emergency_stop");
    client.publish("BushraIzzaHafsa/robot/status", "{\"status\":\"inactive\"}");
    return true;
  }
  Serial.println(" ❌");
  return false;
}

// ============================================================
//   SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n🚀 Smart Rehabilitation System — Starting...");

  // PCA9685
  pca.begin();
  pca.setOscillatorFrequency(27000000);
  pca.setPWMFreq(50);
  delay(200);

  // اقرأ الزوايا المحفوظة وابدأ منها مباشرة
  loadAngles();
  delay(500);
  Serial.println("✅ PCA9685 ready");

  // Load Cells
  scaleLeft.begin(L_DOUT, L_SCK);
  scaleRight.begin(R_DOUT, R_SCK);

  if (scaleLeft.is_ready()) {
    scaleLeft.set_scale(2280.f); scaleLeft.tare();
    Serial.println("✅ Left Load Cell ready");
  } else Serial.println("⚠️  Left Load Cell not detected → 0");

  if (scaleRight.is_ready()) {
    scaleRight.set_scale(2280.f); scaleRight.tare();
    Serial.println("✅ Right Load Cell ready");
  } else Serial.println("⚠️  Right Load Cell not detected → 0");

  randomSeed(analogRead(36));
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("\n✅ System Ready — Waiting for Node-RED...");
  Serial.println("==============================================\n");
}

// ============================================================
//   LOOP
// ============================================================
void loop() {

  // 1. MQTT
  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      reconnect();
    }
  } else {
    client.loop();
  }

  // 2. MOTION
  if (exerciseRunning) {
    if (currentExercise == "walk") {
      if (currentPhase == 1) walking_phase1();
      else                   walking_phase2();
    }
    else if (currentExercise == "knee") {
      if (currentPhase == 1) highknee_phase1();
      else                   highknee_phase2();
    }
  }

  // 3. PUBLISH كل 4 ثواني
  if (exerciseRunning && millis() - lastPublish >= PUBLISH_INTERVAL) {
    publishSensorData();
    lastPublish = millis();
  }
}