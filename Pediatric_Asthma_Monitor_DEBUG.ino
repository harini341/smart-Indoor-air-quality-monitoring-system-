
#define BLYNK_TEMPLATE_NAME "Pediatric Asthma Monitoring"
#define BLYNK_TEMPLATE_ID   "TMPL3trYQxMWa"
#define BLYNK_AUTH_TOKEN    "gr7rNE0qQPz5iz3qO4UjE5N3-lowvK2s"
#define BLYNK_PRINT         Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

char ssid[] = "Hello";
char pass[] = "12345678";

#define DHTPIN        4
#define DHTTYPE       DHT11
#define GAS_PIN       34
#define FAN_RELAY_PIN 23
#define DUST_LED_PIN  16
#define DUST_AOUT_PIN 35

DHT               dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
BlynkTimer        timer;

float pm25=0, temperature=0, humidity=0;
int   gasValue=0;
bool  fanOn=false, manualFan=false, wifiOK=false;
int   mlPrediction=0, lcdPage=0;

// SVM Weights
float w_pm25=2.953198f, w_gas=2.839350f, w_temp=1.541088f, w_humidity=1.285390f, svm_bias=2.469373f;
float mean_pm25=40.3449f, std_pm25=50.7125f;
float mean_gas=354.7912f, std_gas=208.2685f;
float mean_temp=27.4031f, std_temp=5.8391f;
float mean_hum=66.1440f,  std_hum=14.9717f;

float normalize(float v, float m, float s) { return s==0?0:(v-m)/s; }

int svmPredict(float p, float g, float t, float h) {
  float score = w_pm25*normalize(p,mean_pm25,std_pm25)
              + w_gas *normalize(g,mean_gas, std_gas)
              + w_temp*normalize(t,mean_temp,std_temp)
              + w_humidity*normalize(h,mean_hum,std_hum)
              + svm_bias;
  return (score > 2.4f) ? 1 : 0;
}

float readPM25() {
  digitalWrite(DUST_LED_PIN, LOW);
  delayMicroseconds(280);
  int raw = analogRead(DUST_AOUT_PIN);
  delayMicroseconds(40);
  digitalWrite(DUST_LED_PIN, HIGH);
  float v = raw*(3.3f/4095.0f);
  float d = (v>=0.6f)?(v-0.6f)/0.005f:0;
  return max(d,0.0f);
}

void preventiveFanControl() {
  static unsigned long safeTimer=0;
  if (manualFan) return;
  if (mlPrediction==1) {
    fanOn=true; digitalWrite(FAN_RELAY_PIN,HIGH); safeTimer=0;
    Serial.println("FAN ON — Asthma risk!");
  } else {
    if (safeTimer==0) safeTimer=millis();
    if (millis()-safeTimer>=60000UL) {
      fanOn=false; digitalWrite(FAN_RELAY_PIN,LOW); safeTimer=0;
      Serial.println("FAN OFF — Air safe 60s");
    }
  }
}

void updateLCD() {
  lcd.clear(); lcdPage=!lcdPage;
  if (lcdPage==0) {
    lcd.setCursor(0,0); lcd.print("PM2.5:"); lcd.print(pm25,1); lcd.print("ug/m3");
    lcd.setCursor(0,1); lcd.print("Temp:"); lcd.print(temperature,1); lcd.print("C");
  } else {
    lcd.setCursor(0,0); lcd.print(mlPrediction==1?"ASTHMA RISK!    ":"AIR SAFE        ");
    lcd.setCursor(0,1); lcd.print("Fan:"); lcd.print(fanOn?"ON ":"OFF");
    lcd.print(" Hum:"); lcd.print((int)humidity); lcd.print("%");
  }
}

void sendSensorData() {
  pm25        = readPM25();
  temperature = dht.readTemperature();
  humidity    = dht.readHumidity();
  gasValue    = analogRead(GAS_PIN);
  if (isnan(temperature)) { temperature=25.0f; }
  if (isnan(humidity))    { humidity=50.0f; }
  mlPrediction = svmPredict(pm25,(float)gasValue,temperature,humidity);
  preventiveFanControl();
  if (wifiOK && Blynk.connected()) {
    Blynk.virtualWrite(V0,temperature);
    Blynk.virtualWrite(V1,humidity);
    Blynk.virtualWrite(V2,pm25);
    Blynk.virtualWrite(V3,gasValue);
    Blynk.virtualWrite(V4,mlPrediction);
    Blynk.virtualWrite(V5,(int)fanOn);
  }
  updateLCD();
  Serial.println("==========================================");
  Serial.print("[PM2.5]      : "); Serial.print(pm25,2);       Serial.println(" ug/m3");
  Serial.print("[Temperature]: "); Serial.print(temperature,1); Serial.println(" degC");
  Serial.print("[Humidity]   : "); Serial.print(humidity,1);    Serial.println(" %");
  Serial.print("[Gas MQ135]  : "); Serial.println(gasValue);
  Serial.print("[ML Result]  : "); Serial.println(mlPrediction==1?"ASTHMA RISK":"SAFE");
  Serial.print("[Fan]        : "); Serial.println(fanOn?"ON":"OFF");
  Serial.print("[WiFi]       : "); Serial.println(wifiOK?"Connected":"Not connected");
  Serial.println("==========================================");
}

BLYNK_WRITE(V5) {
  int val=param.asInt();
  manualFan=fanOn=(val==1);
  digitalWrite(FAN_RELAY_PIN,val);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("==========================================");
  Serial.println(" Pediatric Asthma Monitor Starting...");
  Serial.println(" SVC kernel=linear | Accuracy: 99.50%");
  Serial.println("==========================================");

  pinMode(GAS_PIN,INPUT);
  pinMode(DUST_AOUT_PIN,INPUT);
  pinMode(DUST_LED_PIN,OUTPUT);
  pinMode(FAN_RELAY_PIN,OUTPUT);
  digitalWrite(DUST_LED_PIN,HIGH);
  digitalWrite(FAN_RELAY_PIN,LOW);

  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0); lcd.print("Pediatric Asthma");
  lcd.setCursor(0,1); lcd.print("Monitor v2.0    ");
  delay(2000);
  lcd.clear();

  // WiFi with full debug
  Serial.println("─── WiFi Debug ───────────────────────────");
  Serial.print("Hotspot name : "); Serial.println(ssid);
  Serial.println("Starting WiFi connection...");

  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_STA);
  delay(500);
  WiFi.begin(ssid, pass);
  Serial.println("WiFi.begin() called — checking status every 500ms...");

  unsigned long wifiStart = millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-wifiStart<15000) {
    delay(500);
    int s = WiFi.status();
    Serial.print("Status: "); Serial.print(s); Serial.print(" → ");
    if      (s==0) Serial.println("IDLE — searching for hotspot...");
    else if (s==1) Serial.println("NO SSID — hotspot name not found! Check name!");
    else if (s==3) Serial.println("CONNECTED!");
    else if (s==4) Serial.println("FAILED — wrong password!");
    else if (s==6) Serial.println("DISCONNECTED — retrying...");
    else           Serial.println("Unknown status");
  }

  if (WiFi.status()==WL_CONNECTED) {
    wifiOK=true;
    Serial.println("WiFi Connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    lcd.clear(); lcd.setCursor(0,0); lcd.print("WiFi Connected!");
    lcd.setCursor(0,1); lcd.print(WiFi.localIP());
    delay(1500); lcd.clear();
    Serial.println("Connecting Blynk...");
    Blynk.config(BLYNK_AUTH_TOKEN);
    bool b=Blynk.connect(5000);
    Serial.println(b?"Blynk Connected!":"Blynk failed — sensors still work!");
  } else {
    wifiOK=false;
    Serial.println("==========================================");
    Serial.println("WiFi FAILED! Check:");
    Serial.println("  1. Hotspot name exactly: Galaxy A14");
    Serial.println("  2. Password exactly: swethayagappan");
    Serial.println("  3. Hotspot band: 2.4GHz (not 5GHz)");
    Serial.println("  4. Hotspot is ON on phone");
    Serial.println("Sensors working without WiFi...");
    Serial.println("==========================================");
    lcd.clear(); lcd.setCursor(0,0); lcd.print("WiFi FAILED");
    lcd.setCursor(0,1); lcd.print("Sensor mode ON");
    delay(2000); lcd.clear();
  }

  timer.setInterval(3000L, sendSensorData);
  Serial.println("System Ready! Reading sensors every 3s...");
}

void loop() {
  if (wifiOK && Blynk.connected()) Blynk.run();
  timer.run();
}
