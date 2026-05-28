// ═══════════════════════════════════════════════════════════════
//  SENSOR DE PULSO CARDÍACO CON ESP32 + TELEGRAM
//  Diseño Biomédico — Universidad Autónoma de Manizales
// ═══════════════════════════════════════════════════════════════

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PulseSensorPlayground.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ── WiFi ───────────────────────────────────────────────────────
const char* SSID      = "CAROLINA";
const char* WIFI_PASS = "1055359765";

// ── Telegram ───────────────────────────────────────────────────
const char* BOT_TOKEN = "8866876780:AAFWlL1YNr1-vvY8iHtzGj6UYrnG6xHxbps";
const char* CHAT_ID   = "8758590307";

// ── Pines ──────────────────────────────────────────────────────
#define PULSE_PIN   34
#define BUZZER_PIN  25
#define LED_LATIDO  26   // Rojo     — parpadea con cada pulso
#define LED_BRADI   33   // Amarillo — BPM < 60
#define LED_TAQUI   32   // Naranja  — BPM > 100

// ── OLED ───────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Sensor ─────────────────────────────────────────────────────
const int THRESHOLD = 2100;
PulseSensorPlayground pulseSensor;

// ── Variables ──────────────────────────────────────────────────
int  bpm                     = 0;
bool latido                  = false;
unsigned long ultimoBip          = 0;
unsigned long ultimoMedicion     = 0;
unsigned long ultimoReporte      = 0;

const long INTERVALO_REPORTE      = 30000;   // Reporte cada 30 seg
const long INTERVALO_RECORDATORIO = 300000;  // Recordatorio cada 5 min

// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_LATIDO, OUTPUT);
  pinMode(LED_BRADI,  OUTPUT);
  pinMode(LED_TAQUI,  OUTPUT);

  // OLED primero
  Wire.begin(3, 23);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR: OLED no encontrada");
    while (true) {
      digitalWrite(LED_LATIDO, HIGH); delay(200);
      digitalWrite(LED_LATIDO, LOW);  delay(200);
    }
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  conectarWiFi();

  pulseSensor.analogInput(PULSE_PIN);
  pulseSensor.blinkOnPulse(LED_LATIDO);
  pulseSensor.setThreshold(THRESHOLD);
  pulseSensor.begin();

  pantallaInicio();
  enviarTelegram("Sistema de monitoreo cardiaco iniciado. Coloca el dedo en el sensor.");
  Serial.println("Sistema listo");
}

// ══════════════════════════════════════════════════════════════
void loop() {
  unsigned long ahora = millis();

  latido = pulseSensor.sawStartOfBeat();

  if (latido) {
    bpm = pulseSensor.getBeatsPerMinute();
    Serial.printf("[PULSO] BPM: %d — %s\n", bpm, clasificarBPM(bpm).c_str());
    ultimoMedicion = ahora;

    // Buzzer
    if (ahora - ultimoBip > 300) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(80);
      digitalWrite(BUZZER_PIN, LOW);
      ultimoBip = ahora;
    }

    // Reporte periódico cada 30 segundos
    if (ahora - ultimoReporte > INTERVALO_REPORTE) {
      String msg = "Medicion en curso\n";
      msg += "BPM: " + String(bpm) + "\n";
      msg += "Estado: " + clasificarBPM(bpm);
      enviarTelegram(msg);
      ultimoReporte = ahora;
    }

    // Alerta inmediata si hay anomalia
    if (bpm > 0 && (bpm < 40 || bpm > 120)) {
      String alerta = "ALERTA CARDIACA\n";
      alerta += "BPM: " + String(bpm) + "\n";
      alerta += "Estado: " + clasificarBPM(bpm) + "\n";
      alerta += "Consulta a tu medico si persiste.";
      enviarTelegram(alerta);
      delay(10000);
    }

    actualizarPantalla(bpm, true);
    actualizarLEDs(bpm);

  } else {
    actualizarPantalla(bpm, false);

    // Recordatorio si no ha medido en 5 minutos
    if (ahora - ultimoMedicion > INTERVALO_RECORDATORIO && ultimoMedicion > 0) {
      enviarTelegram("Recordatorio: Han pasado 5 minutos sin medicion. Coloca el dedo en el sensor.");
      ultimoMedicion = ahora;
    }
  }

  delay(20);
}

// ══════════════════════════════════════════════════════════════
//  FUNCIONES
// ══════════════════════════════════════════════════════════════

void conectarWiFi() {
  WiFi.begin(SSID, WIFI_PASS);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 24);
  display.println("Conectando WiFi...");
  display.display();

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Conectado: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Sin conexion — modo local");
  }
}

void enviarTelegram(String mensaje) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Telegram] Sin WiFi — mensaje no enviado");
    return;
  }

  // Codificar caracteres especiales
  mensaje.replace(" ", "%20");
  mensaje.replace("\n", "%0A");
  mensaje.replace(":", "%3A");
  mensaje.replace("—", "-");
  mensaje.replace("á", "a");
  mensaje.replace("é", "e");
  mensaje.replace("í", "i");
  mensaje.replace("ó", "o");
  mensaje.replace("ú", "u");

  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += BOT_TOKEN;
  url += "/sendMessage?chat_id=";
  url += CHAT_ID;
  url += "&text=";
  url += mensaje;

  http.begin(url);
  http.setTimeout(10000);
  int codigo = http.GET();
  Serial.printf("[Telegram] codigo: %d\n", codigo);

  if (codigo != 200) {
    String respuesta = http.getString();
    Serial.println("[Telegram] Error: " + respuesta);
  }

  http.end();
}

void actualizarLEDs(int b) {
  digitalWrite(LED_BRADI, LOW);
  digitalWrite(LED_TAQUI, LOW);

  if (b > 0 && b < 60)  digitalWrite(LED_BRADI, HIGH);
  else if (b > 100)     digitalWrite(LED_TAQUI, HIGH);
}

String clasificarBPM(int b) {
  if (b <= 0)   return "Sin lectura";
  if (b < 60)   return "Bradicardia";
  if (b <= 100) return "Normal";
  if (b <= 150) return "Taquicardia";
  return "Muy alto";
}

void pantallaInicio() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(18, 8);
  display.println("Sensor de Pulso");
  display.setCursor(22, 22);
  display.println("Cardiaco ESP32");
  display.drawLine(0, 34, 127, 34, SSD1306_WHITE);
  display.setCursor(14, 44);
  display.println("Coloca tu dedo");
  display.setCursor(22, 56);
  display.println("en el sensor");
  display.display();
  delay(2000);
}

void actualizarPantalla(int b, bool hayLatido) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(28, 0);
  display.println("PULSO CARDIACO");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(2, 16);
  display.println(hayLatido ? "<3" : "  ");

  display.setTextSize(3);
  display.setCursor(38, 14);
  display.println(b > 0 ? String(b) : "---");

  display.setTextSize(1);
  display.setCursor(38, 46);
  display.println("BPM");
  display.setCursor(62, 46);
  display.println(clasificarBPM(b));

  display.setCursor(0, 56);
  display.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "Sin WiFi");

  display.display();
}
