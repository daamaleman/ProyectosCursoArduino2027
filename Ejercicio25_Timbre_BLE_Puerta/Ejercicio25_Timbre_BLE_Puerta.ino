/*
  ============================================================================
  EJERCICIO 25 - ARDUINO A (PERIFERICO): SENSOR DE PUERTA CON REED POR BLE
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "A", junto a la puerta)
  COMPONENTES:  - Modulo reed (interruptor magnetico) con su iman (S, V, G)
  BIBLIOTECAS:  "ArduinoBLE" (Administrador de bibliotecas del IDE).

  CONEXIONES:   Reed   S -> D2   V -> 5V   G -> GND

  QUE HACE:     - Vigila la puerta: el modulo se fija en el marco y el iman en
                  la puerta (juntos con la puerta cerrada).
                - Publica por BLE el estado en una caracteristica de UN byte:
                      0 = puerta CERRADA        1 = puerta ABIERTA
                - Envia el dato SOLO CUANDO EL ESTADO CAMBIA (no cada segundo).
                - Antirrebote: el cambio debe mantenerse 50 ms para aceptarse.
                - Si la placa B se conecta despues, lee el estado actual: la
                  caracteristica siempre guarda el ultimo estado.
                - El LED de la placa se enciende mientras B esta conectada.
                - El Monitor Serial muestra cada cambio.

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del programa
                de la placa B (Ejercicio25_Timbre_BLE_Timbre).
                Segun la documentacion de Keyestudio, con el iman cerca el
                modulo entrega LOW (abierta = HIGH). Si su modulo es al reves,
                cambie NIVEL_PUERTA_ABIERTA a LOW.
                Este montaje es didactico: no lo use como alarma de seguridad.

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

const int PIN_REED = 2;                      // Modulo reed: terminal S.

// Nivel que entrega el modulo cuando la PUERTA ESTA ABIERTA (iman lejos).
const int NIVEL_PUERTA_ABIERTA = HIGH;

// Valores que se envian por BLE.
const byte PUERTA_CERRADA = 0;
const byte PUERTA_ABIERTA = 1;

// Tiempo que un cambio debe mantenerse para aceptarse (antirrebote).
const unsigned long TIEMPO_ANTIRREBOTE_MS = 50UL;

// ---- Identificadores BLE (UUID propios, generados una sola vez) ----
// Deben ser IGUALES en la placa A y en la placa B.
const char* UUID_SERVICIO = "d5ae2c35-82c8-4171-8d5e-3972d3a0e4a4";
const char* UUID_ESTADO   = "595a93dc-ec09-4942-87e3-d70e6706c9f8";
const char* NOMBRE_BLE    = "PUERTA_UAM";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

// Servicio y caracteristica de 1 byte (se puede leer y avisa cada envio).
BLEService servicioPuerta(UUID_SERVICIO);
BLEByteCharacteristic caracEstado(UUID_ESTADO, BLERead | BLENotify);

bool lecturaAnterior = false;        // Ultima lectura del pin (true = abierta).
bool estadoEstable = false;          // Estado aceptado tras el antirrebote.
unsigned long instanteCambio = 0;    // Cuando cambio la lectura del pin.

// ============================================================================
// 4. FUNCIONES PROPIAS
// ============================================================================

// true si el modulo indica puerta abierta.
bool leerPuertaAbierta() {
  return digitalRead(PIN_REED) == NIVEL_PUERTA_ABIERTA;
}

// Guarda el estado en la caracteristica (si B esta conectada, recibe el aviso).
void publicarEstado(bool abierta) {
  caracEstado.writeValue(abierta ? PUERTA_ABIERTA : PUERTA_CERRADA);
  Serial.print("Estado enviado: ");
  Serial.println(abierta ? "1 (puerta ABIERTA)" : "0 (puerta CERRADA)");
}

// Antirrebote: acepta un cambio solo si dura TIEMPO_ANTIRREBOTE_MS.
// Si hay un cambio nuevo y estable, lo publica.
void vigilarPuerta() {
  bool lectura = leerPuertaAbierta();

  if (lectura != lecturaAnterior) {          // Algo cambio: empieza a contar.
    lecturaAnterior = lectura;
    instanteCambio = millis();
  }

  if (lectura != estadoEstable && millis() - instanteCambio >= TIEMPO_ANTIRREBOTE_MS) {
    estadoEstable = lectura;                 // El cambio se mantuvo: es real.
    publicarEstado(estadoEstable);           // Se envia solo cuando cambia.
  }
}

// Se ejecuta cuando una placa central se conecta.
void alConectar(BLEDevice central) {
  Serial.print("Placa B conectada: ");
  Serial.println(central.address());
  digitalWrite(LED_BUILTIN, HIGH);
}

// Se ejecuta cuando la conexion se pierde.
void alDesconectar(BLEDevice central) {
  Serial.print("Placa B desconectada: ");
  Serial.println(central.address());
  digitalWrite(LED_BUILTIN, LOW);
  BLE.advertise();                  // Vuelve a anunciarse para que B regrese.
}

// ============================================================================
// 5. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_REED, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("ARDUINO A (PERIFERICO): SENSOR DE PUERTA POR BLE");
  Serial.println("------------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    while (true) {
      // Se detiene aqui.
    }
  }

  BLE.setLocalName(NOMBRE_BLE);
  BLE.setAdvertisedService(servicioPuerta);      // Asi B lo encuentra.
  servicioPuerta.addCharacteristic(caracEstado);
  BLE.addService(servicioPuerta);

  // Estado inicial: se lee la puerta y se guarda antes de anunciarse.
  lecturaAnterior = leerPuertaAbierta();
  estadoEstable = lecturaAnterior;
  publicarEstado(estadoEstable);

  BLE.setEventHandler(BLEConnected, alConectar);
  BLE.setEventHandler(BLEDisconnected, alDesconectar);

  BLE.advertise();
  Serial.print("Anunciandose como ");
  Serial.println(NOMBRE_BLE);
}

// ============================================================================
// 6. LOOP
// ============================================================================

void loop() {
  BLE.poll();                       // Atiende los eventos BLE.
  vigilarPuerta();
}