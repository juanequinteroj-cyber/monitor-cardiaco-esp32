#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PulseSensorPlayground.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ── WiFi ─────────────────────────────────────
const char* SSID      = "sof";
const char* WIFI_PASS = "1054868481";

// ── Telegram ─────────────────────────────────
const char* BOT_TOKEN = "8866876780:AAFWlL1YNr1-vvY8iHtzGj6UYrnG6xHxbps";
const char* CHAT_ID   = "8758590307";

// ── Pines ────────────────────────────────────
#define PULSE_PIN   34
#define BUZZER_PIN  25
#define LED_LATIDO  26
#define LED_BRADI   33
#define LED_TAQUI   32

// ── OLED ─────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Sensor ───────────────────────────────────
PulseSensorPlayground pulseSensor;

const int THRESHOLD = 1600;

// ── Promediado BPM ───────────────────────────
int lecturasBPM[10] = {0};
int indiceLectura = 0;

// ── Variables ────────────────────────────────
int bpm = 0;
bool latido = false;

unsigned long ultimoBip = 0;
unsigned long ultimoMedicion = 0;
unsigned long ultimoReporte = 0;
unsigned long ultimaAlerta = 0;
unsigned long ultimoSerial = 0;

const long INTERVALO_REPORTE = 30000;
const long INTERVALO_RECORDATORIO = 300000;

// ═════════════════════════════════════════════
void setup() {

  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_LATIDO, OUTPUT);
  pinMode(LED_BRADI, OUTPUT);
  pinMode(LED_TAQUI, OUTPUT);

  // I2C estándar ESP32
  Wire.begin(3, 23);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {

    Serial.println("OLED no encontrada");

    while (true) {
      digitalWrite(LED_LATIDO, HIGH);
      delay(200);
      digitalWrite(LED_LATIDO, LOW);
      delay(200);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  conectarWiFi();

  pulseSensor.analogInput(PULSE_PIN);
  pulseSensor.setThreshold(THRESHOLD);
  pulseSensor.begin();

  pantallaInicio();

  enviarTelegram("Sistema de monitoreo cardiaco iniciado");

  Serial.println("Sistema listo");
}

// ═════════════════════════════════════════════
void loop() {

  unsigned long ahora = millis();

  int signal = pulseSensor.getLatestSample();

  // Debug serial más limpio
  if (ahora - ultimoSerial > 200) {
    Serial.print("Signal: ");
    Serial.println(signal);
    ultimoSerial = ahora;
  }

  // Sin dedo
  if (signal < 1000) {

    bpm = 0;

    actualizarPantalla(0, false);
    actualizarLEDs(0);

    return;
  }

  latido = pulseSensor.sawStartOfBeat();

  if (latido) {

    int nuevoBPM = pulseSensor.getBeatsPerMinute();

    // Filtro fisiológico + filtro de cambios bruscos
    if (nuevoBPM >= 40 &&
        nuevoBPM <= 180 &&
        (bpm == 0 || abs(nuevoBPM - bpm) < 25)) {

      // Guardar lectura
      lecturasBPM[indiceLectura % 10] = nuevoBPM;
      indiceLectura++;

      // Promedio
      int suma = 0;
      int count = 0;

      for (int i = 0; i < 10; i++) {

        if (lecturasBPM[i] > 0) {
          suma += lecturasBPM[i];
          count++;
        }
      }

      bpm = (count > 0) ? suma / count : nuevoBPM;

      Serial.printf("BPM crudo: %d | BPM promedio: %d\n",
                    nuevoBPM, bpm);

      ultimoMedicion = ahora;

      // Buzzer NO bloqueante
      if (ahora - ultimoBip > 300) {

        tone(BUZZER_PIN, 2000, 80);

        ultimoBip = ahora;
      }

      // Reporte periódico
      if (ahora - ultimoReporte > INTERVALO_REPORTE) {

        String msg = "Monitoreo activo\n";
        msg += "BPM: " + String(bpm) + "\n";
        msg += "Estado: " + clasificarBPM(bpm);

        enviarTelegram(msg);

        ultimoReporte = ahora;
      }

      // Alerta cardiaca
      if ((bpm < 45 || bpm > 130) &&
          (ahora - ultimaAlerta > 10000)) {

        String alerta = "ALERTA CARDIACA\n";
        alerta += "BPM: " + String(bpm) + "\n";
        alerta += "Estado: " + clasificarBPM(bpm);

        enviarTelegram(alerta);

        ultimaAlerta = ahora;
      }
    }

    actualizarPantalla(bpm, true);
    actualizarLEDs(bpm);
  }

  else {

    actualizarPantalla(bpm, false);

    // Recordatorio
    if (ahora - ultimoMedicion > INTERVALO_RECORDATORIO &&
        ultimoMedicion > 0) {

      enviarTelegram("Han pasado 5 minutos sin medicion");

      ultimoMedicion = ahora;
    }
  }
}

// ═════════════════════════════════════════════
// FUNCIONES
// ═════════════════════════════════════════════

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

    Serial.println("\nWiFi conectado");
    Serial.println(WiFi.localIP());

  } else {

    Serial.println("\nSin WiFi");
  }
}

// ═════════════════════════════════════════════

void enviarTelegram(String mensaje) {

  if (WiFi.status() != WL_CONNECTED) return;

  mensaje.replace(" ", "%20");
  mensaje.replace("\n", "%0A");

  HTTPClient http;

  String url =
    "https://api.telegram.org/bot" +
    String(BOT_TOKEN) +
    "/sendMessage?chat_id=" +
    String(CHAT_ID) +
    "&text=" +
    mensaje;

  http.begin(url);

  int codigo = http.GET();

  Serial.print("Telegram code: ");
  Serial.println(codigo);

  http.end();
}

// ═════════════════════════════════════════════

void actualizarLEDs(int b) {

  digitalWrite(LED_BRADI, LOW);
  digitalWrite(LED_TAQUI, LOW);

  if (b > 0 && b < 60)
    digitalWrite(LED_BRADI, HIGH);

  else if (b > 100)
    digitalWrite(LED_TAQUI, HIGH);
}

// ═════════════════════════════════════════════

String clasificarBPM(int b) {

  if (b <= 0) return "Sin lectura";

  if (b < 60) return "Bradicardia";

  if (b <= 100) return "Normal";

  if (b <= 130) return "Taquicardia";

  return "Muy alto";
}

// ═════════════════════════════════════════════

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

  display.display();

  delay(2000);
}

// ═════════════════════════════════════════════

void actualizarPantalla(int b, bool hayLatido) {

  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(22, 0);
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

  display.println(
    WiFi.status() == WL_CONNECTED ?
    "WiFi OK" :
    "Sin WiFi"
  );

  display.display();
}
