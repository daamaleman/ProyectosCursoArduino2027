/*
  ============================================================================
  EJERCICIO 21: DIAGNOSTICO DE RED WIFI CON SEMAFORO
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (WiFi de 2,4 GHz)
  COMPONENTES:  - Modulo semaforo Keyestudio (GND, G, Y, R)
  BIBLIOTECAS:  Solo WiFiS3, que viene con el paquete "Arduino UNO R4 Boards".
  ARCHIVOS:     Necesita "arduino_secrets.h" en la misma carpeta (nombre y
                clave de la red WiFi). Ese archivo NO se comparte.

  CONEXIONES:   Semaforo   GND -> GND   G (verde) -> D2
                           Y (amarillo) -> D3   R (rojo) -> D4

  QUE HACE:     - Se conecta a la red del aula.
                - Mide la intensidad de senal (RSSI, en dBm) cada 0,4 s y
                  promedia las ultimas 5 lecturas.
                - Cada 2 s escribe en el Monitor Serial el nombre de la red,
                  la direccion IP, el RSSI, el promedio y la calidad.
                - El semaforo indica el estado de la senal:
                    VERDE    senal fuerte (promedio de -65 dBm o mejor)
                    AMARILLO senal debil (peor que -65 dBm)
                    ROJO     sin conexion (y la placa intenta reconectarse)
                - Para volver a verde se exige 3 dB mas que para quedarse en
                  verde (histeresis), asi el semaforo no parpadea.
                - Si la red se cae, reintenta cada 10 s.

  PARA RECORRER EL AULA: Con la placa alimentada por un cable USB largo o una
                bateria externa, camine por el aula y compare el promedio y el
                color del semaforo en distintos puntos.

  IMPORTANTE:   El RSSI depende del router, la antena, las paredes y hasta de
                la posicion de su cuerpo. Los umbrales son una guia de aula, no
                una medicion de laboratorio. No comparta capturas del Monitor
                Serial que muestren la IP o el nombre de la red.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

#include <WiFiS3.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_VERDE    = 2;    // Semaforo: verde.
const int PIN_AMARILLO = 3;    // Semaforo: amarillo.
const int PIN_ROJO     = 4;    // Semaforo: rojo.

// ---- Umbrales de senal (en dBm; mas cerca de 0 es mejor) ----
// Verde si el promedio es de -65 dBm o mejor.
const float UMBRAL_FUERTE_DBM = -65.0;
// Para VOLVER a verde desde amarillo se pide este margen extra (histeresis).
const float HISTERESIS_DB = 3.0;
// Por debajo de este valor la senal se llama "muy debil" (sigue en amarillo).
const float UMBRAL_MUY_DEBIL_DBM = -80.0;
// Para la calidad en %: -100 dBm = 0 %, -50 dBm = 100 %.
const long RSSI_CALIDAD_MIN = -100;
const long RSSI_CALIDAD_MAX = -50;

// ---- Tiempos ----
// Cada cuantos ms se lee el RSSI (5 lecturas = 2 s).
const unsigned long INTERVALO_LECTURA_MS = 400UL;
// Cada cuantos ms se escribe el informe en el Monitor Serial.
const unsigned long INTERVALO_INFORME_MS = 2000UL;
// Cada cuantos ms se reintenta la conexion si la red esta caida.
const unsigned long INTERVALO_REINTENTO_MS = 10000UL;
// Cuanto se espera, como maximo, a que conecte en cada intento.
const unsigned long ESPERA_CONEXION_MS = 5000UL;
const unsigned long PASO_ESPERA_MS = 250UL;
// Intentos al encender la placa.
const int INTENTOS_INICIALES = 3;

// Cuantas lecturas se promedian.
const int NUM_LECTURAS = 5;

// Estados de la senal.
const int SENAL_SIN_CONEXION = 0;
const int SENAL_DEBIL        = 1;
const int SENAL_FUERTE       = 2;

// ============================================================================
// 3. VARIABLES DE ESTADO
// ============================================================================

int estadoSenal = SENAL_SIN_CONEXION;

// Ultimas lecturas de RSSI (buffer circular).
long lecturas[NUM_LECTURAS];
int cantidadLecturas = 0;
int indiceLectura = 0;
long ultimoRssi = 0;
float promedioRssi = 0.0;

unsigned long ultimaLectura = 0;
unsigned long ultimoInforme = 0;
unsigned long ultimoIntento = 0;

// Conteo de intentos de conexion y momento en que se perdio la red.
unsigned long numeroIntento = 0;
unsigned long inicioCaida = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: LECTURA Y CLASIFICACION
// ============================================================================

// Guarda una lectura en el buffer circular y recalcula el promedio.
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
}

// Borra las lecturas (al perder la red).
void borrarLecturas() {
  cantidadLecturas = 0;
  indiceLectura = 0;
  ultimoRssi = 0;
  promedioRssi = 0.0;
}

// Elige VERDE o AMARILLO con histeresis, segun el promedio.
void actualizarEstadoSenal() {
  if (estadoSenal == SENAL_FUERTE) {
    if (promedioRssi < UMBRAL_FUERTE_DBM) {
      estadoSenal = SENAL_DEBIL;
    }
  } else {
    // Desde amarillo (o recien conectado) se pide un margen extra.
    float umbralSubida = UMBRAL_FUERTE_DBM + HISTERESIS_DB;
    if (promedioRssi >= umbralSubida) {
      estadoSenal = SENAL_FUERTE;
    } else {
      estadoSenal = SENAL_DEBIL;
    }
  }
}

// Calidad de 0 a 100 % a partir del promedio.
int calidadPorcentaje() {
  long valor = (long)promedioRssi;
  long porcentaje = map(valor, RSSI_CALIDAD_MIN, RSSI_CALIDAD_MAX, 0L, 100L);
  return (int)constrain(porcentaje, 0L, 100L);
}

// Texto de la senal.
const char* textoSenal() {
  if (estadoSenal == SENAL_SIN_CONEXION) {
    return "SIN CONEXION (rojo)";
  }
  if (estadoSenal == SENAL_FUERTE) {
    return "FUERTE (verde)";
  }
  if (promedioRssi < UMBRAL_MUY_DEBIL_DBM) {
    return "MUY DEBIL (amarillo)";
  }
  return "DEBIL (amarillo)";
}

// ============================================================================
// 5. FUNCIONES PROPIAS: SEMAFORO
// ============================================================================

// Verde = senal fuerte, amarillo = senal debil, rojo = sin conexion.
void actualizarSemaforo() {
  digitalWrite(PIN_VERDE, (estadoSenal == SENAL_FUERTE) ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, (estadoSenal == SENAL_DEBIL) ? HIGH : LOW);
  digitalWrite(PIN_ROJO, (estadoSenal == SENAL_SIN_CONEXION) ? HIGH : LOW);
}

// ============================================================================
// 6. FUNCIONES PROPIAS: RED
// ============================================================================

// Un intento de conexion: espera hasta ESPERA_CONEXION_MS a que conecte.
// Mientras espera, el semaforo queda en rojo.
bool intentarConexion() {
  numeroIntento++;
  Serial.print("Intento de conexion ");
  Serial.print(numeroIntento);
  Serial.print(" a la red ");
  Serial.print(ssid);
  Serial.println("...");
  ultimoIntento = millis();
  WiFi.begin(ssid, pass);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < ESPERA_CONEXION_MS) {
    delay(PASO_ESPERA_MS);
  }
  return (WiFi.status() == WL_CONNECTED);
}

// Se ejecuta al conectar (al arrancar o al reconectar).
void alConectar() {
  borrarLecturas();
  guardarLectura((long)WiFi.RSSI());
  estadoSenal = SENAL_DEBIL;
  actualizarEstadoSenal();
  actualizarSemaforo();
  ultimaLectura = millis();
  Serial.print("Conectado a la red ");
  Serial.print(WiFi.SSID());
  Serial.print(" con la IP ");
  Serial.println(WiFi.localIP());
  if (inicioCaida != 0) {
    Serial.print("Red recuperada despues de ");
    Serial.print((millis() - inicioCaida) / 1000UL);
    Serial.println(" s");
    inicioCaida = 0;
  }
}

// Se ejecuta al detectar que la red se cayo.
void alPerderConexion() {
  estadoSenal = SENAL_SIN_CONEXION;
  borrarLecturas();
  inicioCaida = millis();
  // El primer reintento es inmediato.
  ultimoIntento = millis() - INTERVALO_REINTENTO_MS;
  actualizarSemaforo();
  Serial.println("ALERTA: se perdio la conexion con la red");
}

// ============================================================================
// 7. FUNCIONES PROPIAS: INFORME
// ============================================================================

// Escribe el informe de diagnostico en el Monitor Serial.
void imprimirInforme() {
  if (estadoSenal == SENAL_SIN_CONEXION) {
    unsigned long pasado = millis() - ultimoIntento;
    unsigned long falta = (pasado >= INTERVALO_REINTENTO_MS) ? 0UL : (INTERVALO_REINTENTO_MS - pasado) / 1000UL;
    Serial.print("Red: ");
    Serial.print(ssid);
    Serial.print(" | ");
    Serial.print(textoSenal());
    Serial.print(" | intentos: ");
    Serial.print(numeroIntento);
    Serial.print(" | proximo intento en ");
    Serial.print(falta);
    Serial.println(" s");
    return;
  }
  Serial.print("Red: ");
  Serial.print(WiFi.SSID());
  Serial.print(" | IP: ");
  Serial.print(WiFi.localIP());
  Serial.print(" | RSSI: ");
  Serial.print(ultimoRssi);
  Serial.print(" dBm | Promedio(");
  Serial.print(cantidadLecturas);
  Serial.print("): ");
  Serial.print(promedioRssi, 1);
  Serial.print(" dBm | Calidad: ");
  Serial.print(calidadPorcentaje());
  Serial.print(" % | Senal: ");
  Serial.println(textoSenal());
}

// ============================================================================
// 8. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  actualizarSemaforo();   // Empieza en rojo: aun no hay conexion.

  Serial.println("DIAGNOSTICO DE RED WIFI - ARDUINO UNO R4 WIFI");
  Serial.println("---------------------------------------------");

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: no se detecta el modulo WiFi.");
    while (true) {
      // Se queda aqui con el semaforo en rojo.
    }
  }
  String version = WiFi.firmwareVersion();
  Serial.print("Firmware del modulo WiFi: ");
  Serial.println(version);
  if (version < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Aviso: hay un firmware mas nuevo (Herramientas > Firmware Updater).");
  }

  for (int intento = 1; intento <= INTENTOS_INICIALES; intento++) {
    if (intentarConexion()) {
      alConectar();
      break;
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se pudo conectar: se seguira intentando cada 10 s.");
    inicioCaida = millis();
  }
  ultimoInforme = millis();
}

// ============================================================================
// 9. LOOP
// ============================================================================

void loop() {
  unsigned long ahora = millis();
  bool conectado = (WiFi.status() == WL_CONNECTED);

  if (conectado) {
    // Se acaba de recuperar la red.
    if (estadoSenal == SENAL_SIN_CONEXION) {
      alConectar();
    }
    // Lectura del RSSI y clasificacion.
    if (ahora - ultimaLectura >= INTERVALO_LECTURA_MS) {
      ultimaLectura = ahora;
      guardarLectura((long)WiFi.RSSI());
      actualizarEstadoSenal();
    }
  } else {
    // Se acaba de perder la red.
    if (estadoSenal != SENAL_SIN_CONEXION) {
      alPerderConexion();
    }
    // Reintento cada INTERVALO_REINTENTO_MS.
    if (ahora - ultimoIntento >= INTERVALO_REINTENTO_MS) {
      if (intentarConexion()) {
        alConectar();
      } else {
        Serial.println("Sin exito: se reintentara mas tarde.");
      }
    }
  }

  actualizarSemaforo();

  // Informe cada 2 s.
  if (millis() - ultimoInforme >= INTERVALO_INFORME_MS) {
    ultimoInforme = millis();
    imprimirInforme();
  }
}