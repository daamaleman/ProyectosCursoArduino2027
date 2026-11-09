/*
  ============================================================================
  EJERCICIO 25 - ARDUINO B (CENTRAL): TIMBRE DE PUERTA CON LCD, BUZZER Y BOTON
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "B", el timbre)
  COMPONENTES:  - LCD 1602 con modulo I2C
                - Buzzer pasivo (se maneja con tone())
                - Modulo boton (boton de silencio)
  BIBLIOTECAS:  "ArduinoBLE" y "hd44780" (Bill Perry), desde el Administrador
                de bibliotecas del IDE.

  CONEXIONES:   LCD I2C   GND -> GND   VCC -> 5V   SDA -> SDA (A4)   SCL -> SCL (A5)
                Buzzer    (-) -> GND   (+) o S -> D5
                Boton     S -> D2   V -> 5V   G -> GND

  QUE HACE:     - Busca por BLE la placa A (por el UUID de su servicio), se
                  conecta y se suscribe a la caracteristica de estado.
                - Estado 0 (cerrada): la LCD muestra "Puerta cerrada".
                - Estado 1 (abierta): la LCD muestra "Puerta ABIERTA" y el
                  buzzer pita (200 ms cada 500 ms).
                - El boton SILENCIA el buzzer, pero la LCD sigue mostrando
                  "Puerta ABIERTA": el estado de la puerta no se pierde.
                - Si la puerta se cierra, el buzzer se apaga y el silencio se
                  borra: la proxima apertura volvera a sonar.
                - Si se pierde la conexion con A, el buzzer se apaga y la LCD
                  muestra "Sin conexion" (no se sabe como esta la puerta).
                  Al volver, B lee el estado actual y, si la puerta sigue
                  abierta y ya estaba silenciada, conserva el silencio.

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del programa
                de la placa A (Ejercicio25_Timbre_BLE_Puerta).
                El boton entrega HIGH al pulsarlo (modulo Keyestudio). Si el
                suyo es al reves, cambie NIVEL_PULSADO a LOW.
                Con la conexion perdida el timbre NO suena: no lo use como
                alarma de seguridad.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS
// ============================================================================

#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <ArduinoBLE.h>

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_BOTON  = 2;             // Boton de silencio: terminal S.
const int PIN_BUZZER = 5;             // Buzzer pasivo (se maneja con tone()).

// Nivel que entrega el modulo cuando el boton esta PULSADO.
const int NIVEL_PULSADO = HIGH;

// Antirrebote del boton.
const unsigned long TIEMPO_ANTIRREBOTE_MS = 30UL;

// ---- Timbre ----
const int TONO_TIMBRE_HZ = 2000;
const unsigned long DURACION_PITIDO_MS = 200UL;
const unsigned long PERIODO_PITIDO_MS  = 500UL;

// Valores que envia la placa A.
const byte PUERTA_CERRADA = 0;
const byte PUERTA_ABIERTA = 1;

// Cada cuantos ms se avisa que se sigue buscando.
const unsigned long INTERVALO_AVISO_MS = 3000UL;

// ---- Identificadores BLE: deben ser IGUALES a los de la placa A ----
const char* UUID_SERVICIO = "d5ae2c35-82c8-4171-8d5e-3972d3a0e4a4";
const char* UUID_ESTADO   = "595a93dc-ec09-4942-87e3-d70e6706c9f8";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

hd44780_I2Cexp lcd;
bool lcdDisponible = false;
String filaActual[2] = {"", ""};      // Lo que ya se escribio en cada fila.

// Estado de la puerta segun el ultimo dato recibido.
bool estadoConocido = false;          // Ya llego algun dato de A.
bool puertaAbierta = false;
bool silenciada = false;              // El boton silencio esta apertura.
bool enlaceActivo = false;            // Hay conexion con A.

// Boton (antirrebote).
bool botonAnterior = false;
bool botonEstable = false;
unsigned long instanteBoton = 0;

unsigned long ultimoPitido = 0;
unsigned long ultimoAviso = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: LCD
// ============================================================================

// Escribe una fila de la LCD solo si cambio (rellena a 16 caracteres).
void escribirFila(int fila, String texto) {
  if (!lcdDisponible) {
    return;
  }
  while (texto.length() < 16) {
    texto += ' ';
  }
  texto = texto.substring(0, 16);
  if (texto == filaActual[fila]) {
    return;
  }
  filaActual[fila] = texto;
  lcd.setCursor(0, fila);
  lcd.print(texto);
}

// Muestra el estado de la puerta segun la situacion actual.
void actualizarPantalla() {
  if (!enlaceActivo) {
    escribirFila(0, "Sin conexion");
    escribirFila(1, "Buscando puerta");
  } else if (!estadoConocido) {
    escribirFila(0, "Puerta: ?");
    escribirFila(1, "Esperando dato");
  } else if (puertaAbierta) {
    escribirFila(0, "Puerta ABIERTA");
    escribirFila(1, silenciada ? "Silenciada" : "Pulse: silenciar");
  } else {
    escribirFila(0, "Puerta cerrada");
    escribirFila(1, "Todo en orden");
  }
}

// ============================================================================
// 5. FUNCIONES PROPIAS: BUZZER Y BOTON
// ============================================================================

// true si el timbre debe estar sonando ahora.
bool debeSonar() {
  return enlaceActivo && estadoConocido && puertaAbierta && !silenciada;
}

// Hace los pitidos mientras el timbre deba sonar; si no, calla el buzzer.
void atenderBuzzer() {
  if (!debeSonar()) {
    noTone(PIN_BUZZER);
    return;
  }
  if (millis() - ultimoPitido >= PERIODO_PITIDO_MS) {
    ultimoPitido = millis();
    tone(PIN_BUZZER, TONO_TIMBRE_HZ, DURACION_PITIDO_MS);
  }
}

// Se ejecuta una vez por cada pulsacion del boton de silencio.
void alPulsarBoton() {
  if (debeSonar()) {
    silenciada = true;
    noTone(PIN_BUZZER);
    Serial.println("Boton: timbre silenciado (la puerta sigue abierta).");
    actualizarPantalla();
  } else {
    Serial.println("Boton pulsado: no hay nada que silenciar.");
  }
}

// Lee el boton con antirrebote y detecta el momento de pulsarlo.
void atenderBoton() {
  bool lectura = (digitalRead(PIN_BOTON) == NIVEL_PULSADO);

  if (lectura != botonAnterior) {            // Algo cambio: empieza a contar.
    botonAnterior = lectura;
    instanteBoton = millis();
  }

  if (lectura != botonEstable && millis() - instanteBoton >= TIEMPO_ANTIRREBOTE_MS) {
    botonEstable = lectura;                  // El cambio se mantuvo: es real.
    if (botonEstable) {
      alPulsarBoton();                       // Solo al PULSAR, no al soltar.
    }
  }
}

// ============================================================================
// 6. FUNCIONES PROPIAS: DATOS RECIBIDOS
// ============================================================================

// Procesa el byte de estado recibido de la placa A.
void procesarEstado(byte valor) {
  if (valor != PUERTA_CERRADA && valor != PUERTA_ABIERTA) {
    Serial.print("Dato desconocido recibido: ");
    Serial.println(valor);
    return;
  }

  bool nuevaAbierta = (valor == PUERTA_ABIERTA);

  // Un estado distinto (o el primero) borra el silencio. Si llega el mismo
  // estado (por ejemplo, al reconectar), el silencio se conserva.
  if (!estadoConocido || nuevaAbierta != puertaAbierta) {
    silenciada = false;
    ultimoPitido = millis() - PERIODO_PITIDO_MS;   // El primer pitido es ya.
  }
  estadoConocido = true;
  puertaAbierta = nuevaAbierta;

  Serial.print("Estado recibido: ");
  Serial.println(puertaAbierta ? "1 (puerta ABIERTA)" : "0 (puerta CERRADA)");
  actualizarPantalla();
}

// ============================================================================
// 7. FUNCIONES PROPIAS: BLE
// ============================================================================

// Empieza a buscar placas que anuncien el servicio de este ejercicio.
void iniciarBusqueda() {
  BLE.scanForUuid(UUID_SERVICIO);
  ultimoAviso = millis();
  Serial.println("Buscando la placa A...");
}

// Maneja la conexion con la placa A mientras dure. Devuelve cuando se pierde.
void atenderPuerta(BLEDevice puerta) {
  Serial.print("Placa A encontrada: ");
  Serial.print(puerta.address());
  Serial.print("  (RSSI ");
  Serial.print(puerta.rssi());
  Serial.println(" dBm)");

  BLE.stopScan();                   // Durante la conexion no se busca.

  if (!puerta.connect()) {
    Serial.println("No se pudo conectar.");
    return;
  }
  Serial.println("Conectado. Buscando la caracteristica...");

  if (!puerta.discoverAttributes()) {
    Serial.println("No se pudieron leer los servicios de A.");
    puerta.disconnect();
    return;
  }

  BLECharacteristic caracEstado = puerta.characteristic(UUID_ESTADO);
  if (!caracEstado) {
    Serial.println("A no tiene la caracteristica esperada (revise los UUID).");
    puerta.disconnect();
    return;
  }
  if (!caracEstado.canSubscribe() || !caracEstado.subscribe()) {
    Serial.println("No se pudo suscribir a las notificaciones.");
    puerta.disconnect();
    return;
  }

  enlaceActivo = true;
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("Suscrito. Leyendo el estado actual de la puerta...");

  // Se lee el valor actual: asi se sabe el estado sin esperar un cambio.
  byte valor = 255;
  caracEstado.readValue(valor);
  procesarEstado(valor);
  actualizarPantalla();

  // Mientras A siga conectada se atienden sus avisos, el boton y el buzzer.
  while (puerta.connected()) {
    BLE.poll();
    if (caracEstado.valueUpdated()) {
      caracEstado.readValue(valor);
      procesarEstado(valor);
    }
    atenderBoton();
    atenderBuzzer();
  }

  // Se perdio la conexion: se apaga el timbre y se avisa en la pantalla.
  // Se conserva el ultimo estado para decidir el silencio al volver.
  Serial.println("Se perdio la conexion con A.");
  enlaceActivo = false;
  digitalWrite(LED_BUILTIN, LOW);
  noTone(PIN_BUZZER);
  actualizarPantalla();
}

// ============================================================================
// 8. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_BOTON, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Pantalla LCD.
  if (lcd.begin(16, 2) == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  Serial.println("ARDUINO B (CENTRAL): TIMBRE DE PUERTA POR BLE");
  Serial.println("---------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  escribirFila(0, "Timbre UAM");
  escribirFila(1, "Iniciando BLE...");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    escribirFila(0, "ERROR BLE");
    escribirFila(1, "Reinicie la placa");
    while (true) {
      // Se detiene aqui.
    }
  }

  actualizarPantalla();             // "Sin conexion / Buscando puerta".
  iniciarBusqueda();
}

// ============================================================================
// 9. LOOP
// ============================================================================

void loop() {
  // available() devuelve una placa cuando el escaneo encuentra una.
  BLEDevice puerta = BLE.available();

  if (puerta) {
    atenderPuerta(puerta);
    iniciarBusqueda();              // Vuelve a buscar a A.
  }

  atenderBoton();                   // El boton se atiende siempre.

  if (millis() - ultimoAviso >= INTERVALO_AVISO_MS) {
    ultimoAviso = millis();
    Serial.println("Buscando la placa A...");
  }
}