/*
  ============================================================================
  EJERCICIO 22 - ARDUINO B (CENTRAL): LED CONTROLADO POR EL BOTON DE A
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "B", con el LED)
  COMPONENTES:  - LED rojo de 5 mm y resistencia de 1 kOhm
  BIBLIOTECAS:  "ArduinoBLE" (Administrador de bibliotecas del IDE).

  CONEXIONES:   D7 -> resistencia de 1 kOhm -> LED (pata larga, +)
                LED (pata corta, -) -> GND

  QUE HACE:     - Busca por BLE una placa que anuncie el servicio con el UUID
                  de este programa (la placa A).
                - Se conecta, se suscribe a la caracteristica y lee su valor.
                - Valor 1 = enciende el LED. Valor 0 = lo apaga.
                - El Monitor Serial muestra el valor recibido.
                - Si se pierde la conexion, apaga el LED y vuelve a buscar a
                  la placa A automaticamente.
                - El LED de la propia placa (LED_BUILTIN) indica si B esta
                  conectada a A.

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del
                programa de la placa A (Ejercicio22_Boton_BLE_Periferico).
                Encienda primero la placa A; B tambien la encuentra si A se
                enciende despues, porque B sigue buscando.

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

// OJO: no se llama PIN_LED porque la placa UNO R4 WiFi ya define ese nombre.
const int PIN_LED_EXTERNO = 7;        // LED externo con resistencia de 1 kOhm.

// ---- Identificadores BLE: deben ser IGUALES a los de la placa A ----
const char* UUID_SERVICIO = "0546293c-6525-4941-b2b5-80e04b42999e";
const char* UUID_VALOR    = "bd777524-fe09-45b6-81d7-4788f1d00933";

// Cada cuantos ms se avisa que se sigue buscando.
const unsigned long INTERVALO_AVISO_MS = 3000UL;

// ============================================================================
// 3. VARIABLES DE ESTADO
// ============================================================================

unsigned long ultimoAviso = 0;
bool ledEncendido = false;

// ============================================================================
// 4. FUNCIONES PROPIAS
// ============================================================================

// Enciende o apaga el LED externo y lo muestra en el Monitor Serial.
void aplicarValor(byte valor) {
  ledEncendido = (valor == 1);
  digitalWrite(PIN_LED_EXTERNO, ledEncendido ? HIGH : LOW);
  Serial.print("Valor recibido: ");
  Serial.print(valor);
  Serial.println(ledEncendido ? "  -> LED encendido" : "  -> LED apagado");
}

// Empieza a buscar placas que anuncien el servicio de este ejercicio.
void iniciarBusqueda() {
  BLE.scanForUuid(UUID_SERVICIO);
  ultimoAviso = millis();
  Serial.println("Buscando la placa A...");
}

// Maneja la conexion con la placa A mientras dure. Devuelve cuando se pierde.
void atenderPeriferico(BLEDevice periferico) {
  Serial.print("Placa A encontrada: ");
  Serial.print(periferico.address());
  Serial.print("  (RSSI ");
  Serial.print(periferico.rssi());
  Serial.println(" dBm)");

  // Durante la conexion no se busca.
  BLE.stopScan();

  if (!periferico.connect()) {
    Serial.println("No se pudo conectar.");
    return;
  }
  Serial.println("Conectado. Buscando la caracteristica...");

  if (!periferico.discoverAttributes()) {
    Serial.println("No se pudieron leer los servicios de A.");
    periferico.disconnect();
    return;
  }

  BLECharacteristic caracValor = periferico.characteristic(UUID_VALOR);
  if (!caracValor) {
    Serial.println("A no tiene la caracteristica esperada (revise los UUID).");
    periferico.disconnect();
    return;
  }
  if (!caracValor.canSubscribe() || !caracValor.subscribe()) {
    Serial.println("No se pudo suscribir a las notificaciones.");
    periferico.disconnect();
    return;
  }

  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("Suscrito. Esperando pulsaciones del boton de A...");

  // Se lee el valor actual para empezar sincronizados con A.
  byte valor = 0;
  caracValor.readValue(valor);
  aplicarValor(valor);

  // Mientras A siga conectada se atienden sus avisos.
  while (periferico.connected()) {
    BLE.poll();
    if (caracValor.valueUpdated()) {
      caracValor.readValue(valor);
      aplicarValor(valor);
    }
  }

  // Se perdio la conexion: se apaga el LED por seguridad.
  Serial.println("Se perdio la conexion con A.");
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(PIN_LED_EXTERNO, LOW);
  ledEncendido = false;
}

// ============================================================================
// 5. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED_EXTERNO, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(PIN_LED_EXTERNO, LOW);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("ARDUINO B (CENTRAL): LED CONTROLADO POR BLE");
  Serial.println("-------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    while (true) {
      // Se detiene aqui.
    }
  }

  iniciarBusqueda();
}

// ============================================================================
// 6. LOOP
// ============================================================================

void loop() {
  // available() devuelve una placa cuando el escaneo encuentra una.
  BLEDevice periferico = BLE.available();

  if (periferico) {
    atenderPeriferico(periferico);
    iniciarBusqueda();              // Extension: volver a buscar a A.
  }

  if (millis() - ultimoAviso >= INTERVALO_AVISO_MS) {
    ultimoAviso = millis();
    Serial.println("Buscando la placa A...");
  }
}
