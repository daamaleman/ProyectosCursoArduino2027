/*
  ============================================================================
  EJERCICIO 23 - ARDUINO A (PERIFERICO): TERMOMETRO INALAMBRICO POR BLE
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "A", con el sensor)
  COMPONENTES:  - Sensor DHT11 (modulo de 3 pines: S, V, G)
  BIBLIOTECAS:  "ArduinoBLE" y "DHT sensor library" (Adafruit), ambas desde el
                Administrador de bibliotecas del IDE. La DHT pide instalar
                tambien "Adafruit Unified Sensor": acepte la instalacion.

  CONEXIONES:   DHT11   S (OUT) -> D2   V (+) -> 5V   G (-) -> GND

  QUE HACE:     - Mide la temperatura cada 2 s.
                - La publica por BLE en una caracteristica de UN byte, en
                  grados Celsius (por ejemplo, 28 = 28 C).
                - Si el sensor falla, envia el valor 255 (significa "sin
                  lectura valida").
                - El Monitor Serial muestra cada lectura enviada.

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del
                programa de la placa B (Ejercicio23_Termometro_BLE_Central).
                Un byte sin signo guarda de 0 a 254: el DHT11 mide de 0 a 50 C,
                asi que alcanza. Una temperatura bajo 0 C se enviaria como 0.
                Este programa no comprueba quien se conecta: cualquier central
                que conozca el UUID puede leer la temperatura.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS
// ============================================================================

#include <ArduinoBLE.h>
#include <DHT.h>

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_DHT = 2;                       // DHT11: terminal S (OUT).

// El DHT11 no debe leerse mas rapido que una vez por segundo.
const unsigned long INTERVALO_MEDICION_MS = 2000UL;

// Valor que significa "sin lectura valida" (no es una temperatura real).
const byte VALOR_SIN_LECTURA = 255;

// ---- Identificadores BLE (UUID propios, generados una sola vez) ----
// Deben ser IGUALES en la placa A y en la placa B.
const char* UUID_SERVICIO    = "8226e03e-9446-4065-9f04-d8ff1cc1f2ec";
const char* UUID_TEMPERATURA = "65aa3c1d-c08d-4d95-b38a-019e2b6a0c49";
const char* NOMBRE_BLE       = "TERMOMETRO_UAM";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

DHT sensor(PIN_DHT, DHT11);

// Servicio y caracteristica de 1 byte (se puede leer y avisa cada envio).
BLEService servicioTermometro(UUID_SERVICIO);
BLEByteCharacteristic caracTemperatura(UUID_TEMPERATURA, BLERead | BLENotify);

unsigned long ultimaMedicion = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS
// ============================================================================

// Lee el DHT11 y publica la temperatura por BLE.
void medirYPublicar() {
  float temperatura = sensor.readTemperature();   // En grados Celsius.

  byte valor;
  if (isnan(temperatura)) {
    valor = VALOR_SIN_LECTURA;
    Serial.println("Error: no se pudo leer el DHT11. Se envia 255 (sin lectura).");
  } else {
    // Se redondea al entero mas cercano y se limita a 0..254.
    long redondeada = lround(temperatura);
    valor = (byte)constrain(redondeada, 0L, 254L);
    Serial.print("Temperatura medida: ");
    Serial.print(temperatura, 1);
    Serial.print(" C -> byte enviado: ");
    Serial.println(valor);
  }
  caracTemperatura.writeValue(valor);
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

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  sensor.begin();

  Serial.println("ARDUINO A (PERIFERICO): TERMOMETRO INALAMBRICO BLE");
  Serial.println("--------------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    while (true) {
      // Se detiene aqui.
    }
  }

  BLE.setLocalName(NOMBRE_BLE);
  BLE.setAdvertisedService(servicioTermometro);  // Asi B lo encuentra.
  servicioTermometro.addCharacteristic(caracTemperatura);
  BLE.addService(servicioTermometro);

  caracTemperatura.writeValue(VALOR_SIN_LECTURA);  // Aun no hay lectura.

  BLE.setEventHandler(BLEConnected, alConectar);
  BLE.setEventHandler(BLEDisconnected, alDesconectar);

  BLE.advertise();
  Serial.print("Anunciandose como ");
  Serial.println(NOMBRE_BLE);

  ultimaMedicion = millis();        // La primera lectura sera en 2 s.
}

// ============================================================================
// 6. LOOP
// ============================================================================

void loop() {
  BLE.poll();                       // Atiende los eventos BLE.

  if (millis() - ultimaMedicion >= INTERVALO_MEDICION_MS) {
    ultimaMedicion = millis();
    medirYPublicar();
  }
}