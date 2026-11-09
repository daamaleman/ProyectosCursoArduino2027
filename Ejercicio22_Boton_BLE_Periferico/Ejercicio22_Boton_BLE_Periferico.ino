/*
  ============================================================================
  EJERCICIO 22 - ARDUINO A (PERIFERICO): BOTON INALAMBRICO POR BLE
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "A", con el boton)
  COMPONENTES:  - Modulo boton Keyestudio (S, V, G)
  BIBLIOTECAS:  "ArduinoBLE" (Administrador de bibliotecas del IDE).

  CONEXIONES:   Boton   G -> GND   V -> 5V   S -> D2

  QUE HACE:     - La placa se anuncia por BLE con el nombre BOTON_UAM y un
                  servicio propio (identificado por su UUID).
                - El servicio tiene UNA caracteristica de 1 byte: 0 o 1.
                - Cada vez que se pulsa el boton, el valor cambia de 0 a 1 o
                  de 1 a 0 y se avisa (notificacion) a la placa B.
                - El LED de la propia placa (LED_BUILTIN) indica si hay una
                  placa B conectada.
                - El Monitor Serial muestra cada valor enviado.

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del
                programa de la placa B (Ejercicio22_Boton_BLE_Central).
                Si cambia uno aqui, cambielo tambien alla.
                Este programa no comprueba quien se conecta: cualquier central
                que conozca el UUID puede leer el valor.

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

const int PIN_BOTON = 2;              // Boton: terminal S.

// Nivel que entrega el modulo cuando el boton esta PULSADO.
// El modulo del kit entrega HIGH al pulsar. Si en su modulo ocurre lo
// contrario (cada pulsacion se detecta al soltar), cambie HIGH por LOW.
const int NIVEL_PULSADO = HIGH;

const unsigned long TIEMPO_REBOTE_MS = 30UL;   // Ignora rebotes del boton.

// ---- Identificadores BLE (UUID propios, generados una sola vez) ----
// Deben ser IGUALES en la placa A y en la placa B.
const char* UUID_SERVICIO = "0546293c-6525-4941-b2b5-80e04b42999e";
const char* UUID_VALOR    = "bd777524-fe09-45b6-81d7-4788f1d00933";
const char* NOMBRE_BLE    = "BOTON_UAM";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

// Servicio y caracteristica de 1 byte (se puede leer y avisa cada cambio).
BLEService servicioBoton(UUID_SERVICIO);
BLEByteCharacteristic caracValor(UUID_VALOR, BLERead | BLENotify);

byte valorActual = 0;                 // Valor que se envia: 0 o 1.

// Antirrebote del boton.
bool botonPulsado = false;            // Estado ya confirmado del boton.
bool lecturaAnterior = false;         // Ultima lectura sin confirmar.
unsigned long ultimoCambio = 0;

bool hayCentral = false;              // Ya se mostro que hay conexion.

// ============================================================================
// 4. FUNCIONES PROPIAS
// ============================================================================

// Devuelve true UNA sola vez por cada pulsacion (cuando se presiona).
bool botonFuePulsado() {
  bool lectura = (digitalRead(PIN_BOTON) == NIVEL_PULSADO);
  unsigned long ahora = millis();

  if (lectura != lecturaAnterior) {
    lecturaAnterior = lectura;
    ultimoCambio = ahora;           // Hubo un cambio: empieza a contar.
  }
  // Si la lectura se mantiene estable el tiempo suficiente, es valida.
  if (ahora - ultimoCambio >= TIEMPO_REBOTE_MS && lectura != botonPulsado) {
    botonPulsado = lectura;
    if (botonPulsado) {
      return true;                  // Se acaba de pulsar.
    }
  }
  return false;
}

// Cambia el valor entre 0 y 1 y lo publica por BLE.
void alternarValor() {
  valorActual = (valorActual == 0) ? 1 : 0;
  caracValor.writeValue(valorActual);
  Serial.print("Boton pulsado -> valor enviado: ");
  Serial.println(valorActual);
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

  pinMode(PIN_BOTON, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("ARDUINO A (PERIFERICO): BOTON INALAMBRICO BLE");
  Serial.println("---------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    while (true) {
      // Se detiene aqui.
    }
  }

  BLE.setLocalName(NOMBRE_BLE);
  BLE.setAdvertisedService(servicioBoton);   // Asi B lo encuentra por su UUID.
  servicioBoton.addCharacteristic(caracValor);
  BLE.addService(servicioBoton);

  caracValor.writeValue(valorActual);        // Valor inicial: 0.

  BLE.setEventHandler(BLEConnected, alConectar);
  BLE.setEventHandler(BLEDisconnected, alDesconectar);

  BLE.advertise();
  Serial.print("Anunciandose como ");
  Serial.println(NOMBRE_BLE);
  Serial.println("Pulse el boton para cambiar el valor (0/1).");
}

// ============================================================================
// 6. LOOP
// ============================================================================

void loop() {
  BLE.poll();                       // Atiende los eventos BLE.

  if (botonFuePulsado()) {
    alternarValor();
  }
}