/*
  AirCare - Monitor IoT de qualidade do ar e conforto termico
  Objetos Inteligentes Conectados - Universidade Presbiteriana Mackenzie
  Autor: Luiz Galdino de Sousa Xavier Reche

  Plataforma : ESP32 DevKit V1 (simulado no Wokwi)
  Sensores   : DHT22 (temperatura e umidade) - GPIO4
               MQ-135 (qualidade do ar), simulado por potenciometro - GPIO34
  Atuadores  : Modulo rele (exaustor, representado por LED) - GPIO26
               Buzzer ativo (alerta sonoro) - GPIO27
  Comunicacao: Wi-Fi + MQTT (broker publico HiveMQ, porta 1883)

  Topicos publicados (JSON):
    aircare/luizreche/sensores   {"temp":..,"umid":..,"ar":..,"estado":"NORMAL","ts":<epoch ms>,"id":".."}
    aircare/luizreche/status     {"exaustor":"ON","buzzer":"OFF","modo":"AUTO"}
    aircare/luizreche/ack        {"id":"..","alvo":"exaustor","valor":"ON","ts":<epoch ms>}
  Topicos assinados:
    aircare/luizreche/exaustor/cmd   {"id":"..","valor":"ON|OFF|AUTO"}
    aircare/luizreche/buzzer/cmd     {"id":"..","valor":"ON|OFF"}
    aircare/luizreche/sensores/ler   {"id":"..","valor":"temp|umid|ar"}  (pedido de leitura imediata)
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>

// ---------- Configuracao de rede ----------
const char* WIFI_SSID   = "Wokwi-GUEST";   // rede do simulador Wokwi
const char* WIFI_PASS   = "";
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;
const char* BASE        = "aircare/luizreche/";

// ---------- Pinos ----------
const int PIN_DHT    = 4;
const int PIN_MQ135  = 34;
const int PIN_RELE   = 26;
const int PIN_BUZZER = 27;

// ---------- Limites ----------
const float TEMP_ATENCAO   = 28.0;   // graus C
const int   AR_ATENCAO     = 1500;   // leitura analogica (0 a 4095)
const int   AR_CRITICO     = 2500;
const unsigned long INTERVALO_LEITURA = 5000;   // ms
const unsigned long HISTERESE_MS      = 60000;  // exaustor so desliga apos 1 min normal

DHT dht(PIN_DHT, DHT22);
WiFiClient espClient;
PubSubClient mqtt(espClient);

bool modoAuto = true;       // AUTO: a logica decide; MANUAL: comando remoto decide
bool exaustorLigado = false;
bool buzzerLigado = false;
unsigned long ultimaLeitura = 0;
unsigned long inicioNormal = 0;
String estado = "NORMAL";

// Ultima leitura valida do DHT22 (o sensor pede pelo menos 2 s entre leituras)
float tempAtual = NAN, umidAtual = NAN;
unsigned long ultimaLeituraDHT = 0;

void lerDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  ultimaLeituraDHT = millis();
  if (!isnan(t) && !isnan(h)) { tempAtual = t; umidAtual = h; }
}

void lerEPublicar(const char* id = "");

// Tempo atual em milissegundos (epoch), sincronizado por NTP
unsigned long long agoraMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (unsigned long long)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

String topico(const char* sufixo) { return String(BASE) + sufixo; }

void setExaustor(bool ligar) {
  exaustorLigado = ligar;
  digitalWrite(PIN_RELE, ligar ? HIGH : LOW);
}

void setBuzzer(bool ligar) {
  buzzerLigado = ligar;
  digitalWrite(PIN_BUZZER, ligar ? HIGH : LOW);
}

void publicarStatus() {
  StaticJsonDocument<128> doc;
  doc["exaustor"] = exaustorLigado ? "ON" : "OFF";
  doc["buzzer"]   = buzzerLigado ? "ON" : "OFF";
  doc["modo"]     = modoAuto ? "AUTO" : "MANUAL";
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(topico("status").c_str(), buf, true);   // retido
}

// Confirma ao painel que o atuador foi acionado (usado para medir o tempo de resposta)
void publicarAck(const char* id, const char* alvo, const char* valor) {
  StaticJsonDocument<160> doc;
  doc["id"] = id;
  doc["alvo"] = alvo;
  doc["valor"] = valor;
  doc["ts"] = agoraMs();
  char buf[160];
  serializeJson(doc, buf);
  mqtt.publish(topico("ack").c_str(), buf);
}

// Recebe comandos do painel
void aoReceber(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<160> doc;
  if (deserializeJson(doc, payload, len)) return;
  const char* id = doc["id"] | "";
  String valor = doc["valor"] | "";
  String t = String(topic);

  if (t == topico("exaustor/cmd")) {
    if (valor == "AUTO") { modoAuto = true; }
    else { modoAuto = false; setExaustor(valor == "ON"); }
    publicarAck(id, "exaustor", valor.c_str());
  } else if (t == topico("buzzer/cmd")) {
    modoAuto = false;
    setBuzzer(valor == "ON");
    publicarAck(id, "buzzer", valor.c_str());
  } else if (t == topico("sensores/ler")) {
    // Pedido de leitura imediata (usado para medir o tempo de resposta dos sensores)
    if (valor != "ar" && millis() - ultimaLeituraDHT >= 2000) lerDHT();
    lerEPublicar(id);
    return;
  }
  Serial.printf("[MQTT] comando %s -> %s\n", t.c_str(), valor.c_str());
  publicarStatus();
}

void conectarWiFi() {
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250); Serial.print(".");
    if (++tentativas % 60 == 0) {          // 15 s sem conectar: tenta de novo
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
    }
  }
  Serial.println(" conectado!");
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // relogio (horario das leituras)
  Serial.print("Sincronizando relogio (NTP)");
  while (time(nullptr) < 1700000000) { delay(250); Serial.print("."); }
  Serial.println(" ok");
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    String id = "aircare-esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("Conectando ao broker MQTT... ");
    if (mqtt.connect(id.c_str())) {
      Serial.println("ok");
      mqtt.subscribe(topico("exaustor/cmd").c_str());
      mqtt.subscribe(topico("buzzer/cmd").c_str());
      mqtt.subscribe(topico("sensores/ler").c_str());
      publicarStatus();
    } else {
      Serial.printf("falhou (rc=%d), nova tentativa em 2 s\n", mqtt.state());
      delay(2000);
    }
  }
}

// Le os sensores, aplica a logica automatica e publica
void lerEPublicar(const char* id) {
  int ar = analogRead(PIN_MQ135);

  if (ar >= AR_CRITICO) estado = "CRITICO";
  else if (ar >= AR_ATENCAO || (!isnan(tempAtual) && tempAtual >= TEMP_ATENCAO)) estado = "ATENCAO";
  else estado = "NORMAL";

  if (modoAuto) {
    if (estado != "NORMAL") {
      setExaustor(true);
      inicioNormal = 0;
    } else if (exaustorLigado) {
      if (inicioNormal == 0) inicioNormal = millis();
      if (millis() - inicioNormal >= HISTERESE_MS) setExaustor(false);   // histerese
    }
    setBuzzer(estado == "CRITICO");
  }

  StaticJsonDocument<200> doc;
  doc["temp"] = round(tempAtual * 10) / 10.0;
  doc["umid"] = round(umidAtual * 10) / 10.0;
  doc["ar"] = ar;
  doc["estado"] = estado;
  doc["ts"] = agoraMs();
  if (id[0]) doc["id"] = id;
  char buf[200];
  serializeJson(doc, buf);
  mqtt.publish(topico("sensores").c_str(), buf);
  publicarStatus();
  Serial.printf("[Sensores] %s\n", buf);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  setExaustor(false);
  setBuzzer(false);
  dht.begin();
  analogReadResolution(12);

  conectarWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(aoReceber);
}

void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();
  if (millis() - ultimaLeituraDHT >= 2500) lerDHT();
  if (millis() - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = millis();
    lerEPublicar();
  }
}
