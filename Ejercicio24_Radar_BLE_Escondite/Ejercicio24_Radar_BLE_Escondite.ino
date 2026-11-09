/*
  ============================================================================
  EJERCICIO 24 - ARDUINO B (BUSCADOR): RADAR DE PROXIMIDAD "FRIO-CALIENTE"
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "B", la que busca)
  COMPONENTES:  - Buzzer pasivo (se maneja con tone())
                - Modulo semaforo Keyestudio (GND, G, Y, R)
  BIBLIOTECAS:  "ArduinoBLE" (Administrador de bibliotecas del IDE).

  CONEXIONES:   Semaforo   GND -> GND   G (verde) -> D2   Y (amarillo) -> D3
                           R (rojo) -> D4
                Buzzer     (-) -> GND   (+) o S -> D5

  QUE HACE:     - Escucha los anuncios BLE de la placa A (la busca por el UUID
                  de su servicio) y mide la intensidad de la senal (RSSI).
                - Promedia las ultimas 5 lecturas para reducir el ruido.
                - Cuanto mas CERCA esta A, mas RAPIDO pita el buzzer.
                - El semaforo indica la zona:
                    VERDE    lejos        (promedio peor que -70 dBm)
                    AMARILLO medio        (entre -70 y -55 dBm)
                    ROJO     muy cerca    (promedio de -55 dBm o mejor)
                - Para cambiar de zona hacia "mas lejos" se pide un margen
                  extra de 3 dB (histeresis), asi no parpadea en el limite.
                - Si pasan 3 s sin recibir anuncios de A, apaga el semaforo y el
                  buzzer y avisa que perdio la senal.
                - Estima una distancia APROXIMADA con el RSSI a 1 m que usted
                  mida (ver RSSI_A_1M_DBM).

  JUEGO:        Un estudiante esconde la placa A y otro busca con la placa B.

  IMPORTANTE:   El RSSI cambia mucho con paredes, cuerpos, muebles y la
                orientacion de la placa. Los umbrales y la distancia son una
                guia, no una medicion. Esta placa no se conecta a A.
                El UUID de este programa debe ser IGUAL al de la placa A.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS
// ============================================================================

#include <ArduinoBLE.h>

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_VERDE    = 2;    // Semaforo: verde.
const int PIN_AMARILLO = 3;    // Semaforo: amarillo.
const int PIN_ROJO     = 4;    // Semaforo: rojo.
const int PIN_BUZZER   = 5;    // Buzzer pasivo (se maneja con tone()).

// ---- Identificador BLE: debe ser IGUAL al de la placa A ----
const char* UUID_SERVICIO = "f6dac622-2aa5-4353-8066-8cae00c4d656";

// ---- Zonas (RSSI promedio, en dBm; mas cerca de 0 es mas cerca de A) ----
const float UMBRAL_CERCA_DBM  = -55.0;   // Este valor o mejor: MUY CERCA (rojo).
const float UMBRAL_MEDIO_DBM  = -70.0;   // Este valor o mejor: MEDIO (amarillo).
const float HISTERESIS_DB     = 3.0;     // Margen para pasar a una zona mas lejana.

// ---- Buzzer ----
const int TONO_HZ = 2000;
const unsigned long DURACION_PITIDO_MS = 60UL;
const unsigned long PERIODO_LEJOS_MS   = 1500UL;   // Pitido mas lento (RSSI = -90).
const unsigned long PERIODO_CERCA_MS   = 150UL;    // Pitido mas rapido (RSSI = -40).
const long RSSI_PITIDO_LENTO_DBM  = -90;
const long RSSI_PITIDO_RAPIDO_DBM = -40;

// ---- Promedio y tiempos ----
const int NUM_LECTURAS = 5;                        // Lecturas que se promedian.
const unsigned long TIEMPO_SIN_SENAL_MS = 3000UL;  // Sin anuncios = senal perdida.
const unsigned long INTERVALO_INFORME_MS = 500UL;  // Cada cuanto se informa.

// ---- Distancia aproximada (modelo simple de perdida de senal) ----
// RSSI_A_1M_DBM: RSSI que usted mide con A a 1 m de B (ver la guia).
// EXPONENTE_N: 2 en espacio libre; entre 2,5 y 4 dentro de una casa o un aula.
const float RSSI_A_1M_DBM = -59.0;
const float EXPONENTE_N   = 2.5;

// Zonas.
const int ZONA_SIN_SENAL = 0;
const int ZONA_LEJOS     = 1;
const int ZONA_MEDIO     = 2;
const int ZONA_CERCA     = 3;

// ============================================================================
// 3. VARIABLES DE ESTADO
// ============================================================================

int zonaActual = ZONA_SIN_SENAL;

// Ultimas lecturas de RSSI (buffer circular).
long lecturas[NUM_LECTURAS];
int cantidadLecturas = 0;
int indiceLectura = 0;
long ultimoRssi = 0;
float promedioRssi = 0.0;

unsigned long ultimoAnuncio = 0;     // Cuando llego el ultimo anuncio de A.
unsigned long ultimoPitido = 0;
unsigned long ultimoInforme = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: MEDICION Y ZONAS
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

// Borra las lecturas (al perder la senal).
void borrarLecturas() {
  cantidadLecturas = 0;
  indiceLectura = 0;
  ultimoRssi = 0;
  promedioRssi = 0.0;
}

// Elige la zona segun el promedio, con histeresis al ALEJARSE.
void actualizarZona() {
  // Se repite dos veces por si A pasa de lejos a muy cerca de golpe.
  for (int vez = 0; vez < 2; vez++) {
    if (zonaActual == ZONA_SIN_SENAL || zonaActual == ZONA_LEJOS) {
      if (promedioRssi >= UMBRAL_MEDIO_DBM) {
        zonaActual = ZONA_MEDIO;
      } else {
        zonaActual = ZONA_LEJOS;
      }
    } else if (zonaActual == ZONA_MEDIO) {
      if (promedioRssi >= UMBRAL_CERCA_DBM) {
        zonaActual = ZONA_CERCA;
      } else if (promedioRssi < UMBRAL_MEDIO_DBM - HISTERESIS_DB) {
        zonaActual = ZONA_LEJOS;
      }
    } else if (zonaActual == ZONA_CERCA) {
      if (promedioRssi < UMBRAL_CERCA_DBM - HISTERESIS_DB) {
        zonaActual = ZONA_MEDIO;
      }
    }
  }
}

// Texto de la zona.
const char* textoZona() {
  if (zonaActual == ZONA_CERCA) {
    return "MUY CERCA (rojo)";
  }
  if (zonaActual == ZONA_MEDIO) {
    return "MEDIO (amarillo)";
  }
  if (zonaActual == ZONA_LEJOS) {
    return "LEJOS (verde)";
  }
  return "SIN SENAL";
}

// Distancia aproximada en metros: d = 10 ^ ((RSSI_1m - RSSI) / (10 * n)).
float distanciaAproximadaM() {
  return pow(10.0, (RSSI_A_1M_DBM - promedioRssi) / (10.0 * EXPONENTE_N));
}

// ============================================================================
// 5. FUNCIONES PROPIAS: SEMAFORO Y BUZZER
// ============================================================================

// Verde = lejos, amarillo = medio, rojo = muy cerca. Sin senal: todo apagado.
void actualizarSemaforo() {
  digitalWrite(PIN_VERDE, (zonaActual == ZONA_LEJOS) ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, (zonaActual == ZONA_MEDIO) ? HIGH : LOW);
  digitalWrite(PIN_ROJO, (zonaActual == ZONA_CERCA) ? HIGH : LOW);
}

// Tiempo entre pitidos: mas cerca = mas corto.
unsigned long periodoPitido() {
  long rssi = constrain((long)promedioRssi, RSSI_PITIDO_LENTO_DBM, RSSI_PITIDO_RAPIDO_DBM);
  return (unsigned long)map(rssi, RSSI_PITIDO_LENTO_DBM, RSSI_PITIDO_RAPIDO_DBM,
                            (long)PERIODO_LEJOS_MS, (long)PERIODO_CERCA_MS);
}

// Hace sonar un pitido cuando toca; sin senal no suena.
void atenderBuzzer() {
  if (zonaActual == ZONA_SIN_SENAL) {
    noTone(PIN_BUZZER);
    return;
  }
  if (millis() - ultimoPitido >= periodoPitido()) {
    ultimoPitido = millis();
    tone(PIN_BUZZER, TONO_HZ, DURACION_PITIDO_MS);
  }
}

// ============================================================================
// 6. FUNCIONES PROPIAS: INFORME
// ============================================================================

void imprimirInforme() {
  if (zonaActual == ZONA_SIN_SENAL) {
    return;
  }
  Serial.print("RSSI: ");
  Serial.print(ultimoRssi);
  Serial.print(" dBm | Promedio(");
  Serial.print(cantidadLecturas);
  Serial.print("): ");
  Serial.print(promedioRssi, 1);
  Serial.print(" dBm | Zona: ");
  Serial.print(textoZona());
  Serial.print(" | Pitido cada ");
  Serial.print(periodoPitido());
  Serial.print(" ms | Distancia aprox.: ");
  Serial.print(distanciaAproximadaM(), 1);
  Serial.println(" m");
}

// ============================================================================
// 7. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  actualizarSemaforo();            // Todo apagado al empezar.

  Serial.println("ARDUINO B (BUSCADOR): RADAR DE PROXIMIDAD BLE");
  Serial.println("---------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    digitalWrite(PIN_ROJO, HIGH);
    while (true) {
      // Se detiene aqui con el rojo encendido.
    }
  }

  // El "true" permite recibir cada anuncio de A, no solo el primero.
  BLE.scanForUuid(UUID_SERVICIO, true);
  Serial.println("Buscando la placa A...");
  ultimoInforme = millis();
}

// ============================================================================
// 8. LOOP
// ============================================================================

void loop() {
  // available() devuelve un dispositivo cada vez que llega un anuncio.
  BLEDevice anuncio = BLE.available();

  if (anuncio) {
    if (zonaActual == ZONA_SIN_SENAL) {
      Serial.println("Senal de A encontrada.");
      borrarLecturas();
    }
    ultimoAnuncio = millis();
    guardarLectura((long)anuncio.rssi());
    actualizarZona();
    actualizarSemaforo();
  }

  // Si no llegan anuncios por un tiempo, se pierde la senal.
  if (zonaActual != ZONA_SIN_SENAL && millis() - ultimoAnuncio >= TIEMPO_SIN_SENAL_MS) {
    zonaActual = ZONA_SIN_SENAL;
    borrarLecturas();
    actualizarSemaforo();
    Serial.println("Senal de A perdida: buscando de nuevo...");
  }

  atenderBuzzer();

  if (millis() - ultimoInforme >= INTERVALO_INFORME_MS) {
    ultimoInforme = millis();
    imprimirInforme();
  }
}