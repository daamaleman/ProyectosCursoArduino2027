/*
  ============================================================================
  EJERCICIO 24 - ARDUINO B (BUSCADOR): RADAR BLE (LED 3W MONOCOLOR - 3 PINES)
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Buzzer pasivo
                - Modulo LED 3W Keyestudio (S, V, G)
  BIBLIOTECAS:  "ArduinoBLE"

  CONEXIONES:   LED 3W       S -> D3 (PWM)   V -> 5V    G -> GND
                Buzzer       (+) -> D5       (-) -> GND

  QUE HACE:     - Escucha anuncios BLE y mide el RSSI.
                - Promedia las ultimas 5 lecturas.
                - Cuanto mas CERCA esta A, mas RAPIDO pita el buzzer y mas 
                  BRILLANTE se enciende el LED de 3W.
                - Sin senal: LED apagado y buzzer en silencio.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

#include <ArduinoBLE.h>

// ============================================================================
// PINES Y CONSTANTES
// ============================================================================

const int PIN_LED_3W = 3;     // Pin PWM para controlar brillo (S)
const int PIN_BUZZER = 5;     // Pin para el buzzer pasivo

const char* UUID_SERVICIO = "f6dac622-2aa5-4353-8066-8cae00c4d656";

// RSSI para calculos de frecuencia y brillo
const long RSSI_LEJOS_DBM = -90;  // Lejos (LED tenue, pitido lento)
const long RSSI_CERCA_DBM = -40;  // Cerca (LED a tope, pitido rapido)

// ---- Buzzer ----
const int TONO_HZ = 2000;
const unsigned long DURACION_PITIDO_MS = 60UL;
const unsigned long PERIODO_LEJOS_MS   = 1500UL;
const unsigned long PERIODO_CERCA_MS   = 150UL;

// ---- Promedio y tiempos ----
const int NUM_LECTURAS = 5;
const unsigned long TIEMPO_SIN_SENAL_MS = 3000UL;
const unsigned long INTERVALO_INFORME_MS = 500UL;

const float RSSI_A_1M_DBM = -59.0;
const float EXPONENTE_N   = 2.5;

// ============================================================================
// VARIABLES DE ESTADO
// ============================================================================

long lecturas[NUM_LECTURAS];
int cantidadLecturas = 0;
int indiceLectura = 0;
long ultimoRssi = 0;
float promedioRssi = 0.0;
bool haySenal = false;

unsigned long ultimoAnuncio = 0;
unsigned long ultimoPitido = 0;
unsigned long ultimoInforme = 0;

// ============================================================================
// FUNCIONES DE MEDICION
// ============================================================================

void guardarLectura(long rssi) {
  lecturas[indiceLectura] = rssi;
  indiceLectura = (indiceLectura + 1) % NUM_LECTURAS;
  if (cantidadLecturas < NUM_LECTURAS) {
    cantidadLecturas++;
  }
  long suma = 0;
  for (int i = 0; i < cantidadLecturas; i++) {
    suma += lecturas[i];
  }
  ultimoRssi = rssi;
  promedioRssi = (float)suma / (float)cantidadLecturas;
  haySenal = true;
}

void borrarLecturas() {
  cantidadLecturas = 0;
  indiceLectura = 0;
  ultimoRssi = 0;
  promedioRssi = 0.0;
  haySenal = false;
}

float distanciaAproximadaM() {
  return pow(10.0, (RSSI_A_1M_DBM - promedioRssi) / (10.0 * EXPONENTE_N));
}

// ============================================================================
// CONTROL DE LED Y BUZZER
// ============================================================================

void actualizarLedBrillo() {
  if (!haySenal) {
    analogWrite(PIN_LED_3W, 0); // Apagado sin señal
    return;
  }

  // Mapea el RSSI a intensidad PWM (Brillo minimo ~15 para que sea visible, maximo 255)
  long rssiMapeado = constrain((long)promedioRssi, RSSI_LEJOS_DBM, RSSI_CERCA_DBM);
  int brillo = map(rssiMapeado, RSSI_LEJOS_DBM, RSSI_CERCA_DBM, 15, 255);
  
  analogWrite(PIN_LED_3W, brillo);
}

unsigned long periodoPitido() {
  long rssi = constrain((long)promedioRssi, RSSI_LEJOS_DBM, RSSI_CERCA_DBM);
  return (unsigned long)map(rssi, RSSI_LEJOS_DBM, RSSI_CERCA_DBM,
                            (long)PERIODO_LEJOS_MS, (long)PERIODO_CERCA_MS);
}

void atenderBuzzer() {
  if (!haySenal) {
    noTone(PIN_BUZZER);
    return;
  }
  if (millis() - ultimoPitido >= periodoPitido()) {
    ultimoPitido = millis();
    tone(PIN_BUZZER, TONO_HZ, DURACION_PITIDO_MS);
  }
}

// ============================================================================
// INFORME SERIAL
// ============================================================================

void imprimirInforme() {
  if (!haySenal) return;

  Serial.print("RSSI: ");
  Serial.print(ultimoRssi);
  Serial.print(" dBm | Promedio: ");
  Serial.print(promedioRssi, 1);
  Serial.print(" dBm | Pitido cada ");
  Serial.print(periodoPitido());
  Serial.print(" ms | Distancia aprox.: ");
  Serial.print(distanciaAproximadaM(), 1);
  Serial.println(" m");
}

// ============================================================================
// SETUP Y LOOP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED_3W, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogWrite(PIN_LED_3W, 0);

  Serial.println("ARDUINO B (BUSCADOR): RADAR BLE (LED 3W MONOCOLOR)");
  Serial.println("-------------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    while (true);
  }

  BLE.scanForUuid(UUID_SERVICIO, true);
  Serial.println("Buscando la placa A...");
  ultimoInforme = millis();
}

void loop() {
  BLEDevice anuncio = BLE.available();

  if (anuncio) {
    if (!haySenal) {
      Serial.println("Senal de A encontrada.");
      borrarLecturas();
    }
    ultimoAnuncio = millis();
    guardarLectura((long)anuncio.rssi());
    actualizarLedBrillo();
  }

  if (haySenal && millis() - ultimoAnuncio >= TIEMPO_SIN_SENAL_MS) {
    borrarLecturas();
    actualizarLedBrillo();
    Serial.println("Senal de A perdida: buscando de nuevo...");
  }

  atenderBuzzer();

  if (millis() - ultimoInforme >= INTERVALO_INFORME_MS) {
    ultimoInforme = millis();
    imprimirInforme();
  }
}