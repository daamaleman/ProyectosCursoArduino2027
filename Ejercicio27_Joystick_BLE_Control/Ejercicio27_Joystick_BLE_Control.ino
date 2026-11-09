/*
  ============================================================================
  EJERCICIO 27 - ARDUINO A (PERIFERICO): CONTROL REMOTO CON JOYSTICK POR BLE
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "A", el control remoto)
  COMPONENTES:  - Modulo joystick Keyestudio (marcas Y, X, B, V, G)
  BIBLIOTECAS:  "ArduinoBLE" (Administrador de bibliotecas del IDE).

  CONEXIONES:   Joystick  G -> GND   V -> 5V
                          Y -> A1    (eje horizontal, "VRY" en otras guias)
                          B -> D2    (boton, "SW" en otras guias)
                (el eje X, vertical, no se usa en este ejercicio)

  QUE HACE:     - Lee el eje horizontal del joystick y lo convierte en un
                  angulo de 0 a 180 grados (centro = 90).
                - Publica por BLE un paquete de 3 bytes:
                      byte 0: angulo (0 a 180)
                      byte 1: bloqueo (0 = libre, 1 = bloqueado)
                      byte 2: contador de envios (0 a 255, vuelve a 0)
                - AHORRA TRAFICO: solo envia cuando el angulo cambia MAS de
                  3 grados, o cuando se cambia el bloqueo.
                - LATIDO: aunque no haya cambios, envia el paquete cada 500 ms.
                  Asi la placa B sabe que el enlace sigue vivo. El contador
                  hace que cada paquete sea distinto.
                - El boton del joystick ALTERNA el bloqueo: al pulsarlo, B
                  deja de mover el servo; al pulsarlo otra vez, lo libera.
                - El LED de la placa se enciende mientras B esta conectada.

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del programa
                de la placa B (Ejercicio27_Joystick_BLE_Receptor).
                Si el servo gira al reves, cambie SENTIDO_HORIZONTAL a -1.

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

const int PIN_HORIZONTAL = A1;       // Joystick: eje horizontal (marca Y).
const int PIN_BOTON = 2;             // Joystick: boton (marca B), pulsado = LOW.

// Lectura analogica de 10 bits (0 a 1023).
const int LECTURA_MAXIMA = 1023;

// Angulo que se envia (0 a 180). Cambie a -1 si el servo gira al reves.
const int SENTIDO_HORIZONTAL = 1;
const int ANGULO_MAXIMO = 180;

// Solo se envia un angulo nuevo si cambia MAS de esta cantidad de grados.
const int UMBRAL_CAMBIO_GRADOS = 3;

// Tiempo minimo entre dos envios por cambio (evita saturar el enlace).
const unsigned long INTERVALO_MINIMO_MS = 50UL;

// Cada cuanto se envia el paquete aunque no haya cambios (latido).
const unsigned long INTERVALO_LATIDO_MS = 500UL;

// Antirrebote del boton.
const unsigned long TIEMPO_ANTIRREBOTE_MS = 30UL;

// ---- Identificadores BLE (UUID propios, generados una sola vez) ----
// Deben ser IGUALES en la placa A y en la placa B.
const char* UUID_SERVICIO = "e959981e-4cb1-42f3-822a-7f77ad26e2f3";
const char* UUID_MANDO    = "a103b281-b2a9-4f83-969b-d76ae2320f67";
const char* NOMBRE_BLE    = "JOYSTICK_UAM";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

// Servicio y caracteristica de 3 bytes (se puede leer y avisa cada envio).
BLEService servicioJoystick(UUID_SERVICIO);
BLECharacteristic caracMando(UUID_MANDO, BLERead | BLENotify, 3);

int ultimoAnguloEnviado = 90;
bool bloqueado = false;
byte contador = 0;
unsigned long ultimoEnvio = 0;

// Boton (antirrebote).
bool botonAnterior = false;
bool botonEstable = false;
unsigned long instanteBoton = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS
// ============================================================================

// Lee el joystick y lo convierte en un angulo de 0 a 180.
int leerAngulo() {
  int lectura = analogRead(PIN_HORIZONTAL);
  int angulo = map(lectura, 0, LECTURA_MAXIMA, 0, ANGULO_MAXIMO);
  angulo = constrain(angulo, 0, ANGULO_MAXIMO);
  if (SENTIDO_HORIZONTAL < 0) {
    angulo = ANGULO_MAXIMO - angulo;
  }
  return angulo;
}

// Arma y publica el paquete de 3 bytes: angulo, bloqueo y contador.
void enviarPaquete(int angulo, const char* motivo) {
  byte paquete[3];
  paquete[0] = (byte)angulo;
  paquete[1] = bloqueado ? 1 : 0;
  paquete[2] = contador;
  contador++;                                  // Vuelve a 0 despues de 255.
  caracMando.writeValue(paquete, 3);
  ultimoAnguloEnviado = angulo;
  ultimoEnvio = millis();
  Serial.print("Envio (");
  Serial.print(motivo);
  Serial.print("): angulo ");
  Serial.print(angulo);
  Serial.print(" | bloqueo ");
  Serial.println(bloqueado ? "SI" : "no");
}

// Se ejecuta una vez por cada pulsacion del boton: alterna el bloqueo.
void alPulsarBoton() {
  bloqueado = !bloqueado;
  Serial.println(bloqueado ? "Boton: BLOQUEADO (B ignora el movimiento)"
                           : "Boton: LIBRE (B vuelve a seguir el joystick)");
  enviarPaquete(leerAngulo(), "boton");
}

// Lee el boton con antirrebote y detecta el momento de pulsarlo.
void atenderBoton() {
  bool lectura = (digitalRead(PIN_BOTON) == LOW);

  if (lectura != botonAnterior) {              // Algo cambio: empieza a contar.
    botonAnterior = lectura;
    instanteBoton = millis();
  }

  if (lectura != botonEstable && millis() - instanteBoton >= TIEMPO_ANTIRREBOTE_MS) {
    botonEstable = lectura;                    // El cambio se mantuvo: es real.
    if (botonEstable) {
      alPulsarBoton();                         // Solo al PULSAR, no al soltar.
    }
  }
}

// Decide si hay que enviar: por cambio de angulo o por latido.
void atenderEnvio() {
  int angulo = leerAngulo();
  unsigned long ahora = millis();

  if (abs(angulo - ultimoAnguloEnviado) > UMBRAL_CAMBIO_GRADOS
      && ahora - ultimoEnvio >= INTERVALO_MINIMO_MS) {
    enviarPaquete(angulo, "cambio");
  } else if (ahora - ultimoEnvio >= INTERVALO_LATIDO_MS) {
    enviarPaquete(angulo, "latido");           // Dice "sigo aqui" con el angulo actual.
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

  analogReadResolution(10);          // Lecturas de 0 a 1023.
  pinMode(PIN_BOTON, INPUT_PULLUP);  // Pulsado = LOW.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("ARDUINO A (PERIFERICO): CONTROL REMOTO CON JOYSTICK POR BLE");
  Serial.println("-----------------------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    while (true) {
      // Se detiene aqui.
    }
  }

  BLE.setLocalName(NOMBRE_BLE);
  BLE.setAdvertisedService(servicioJoystick);    // Asi B lo encuentra.
  servicioJoystick.addCharacteristic(caracMando);
  BLE.addService(servicioJoystick);

  // Paquete inicial: centro, libre, contador 0.
  byte inicial[3] = {90, 0, 0};
  caracMando.writeValue(inicial, 3);

  BLE.setEventHandler(BLEConnected, alConectar);
  BLE.setEventHandler(BLEDisconnected, alDesconectar);

  BLE.advertise();
  Serial.print("Anunciandose como ");
  Serial.println(NOMBRE_BLE);
  ultimoEnvio = millis();
}

// ============================================================================
// 6. LOOP
// ============================================================================

void loop() {
  BLE.poll();                       // Atiende los eventos BLE.
  atenderBoton();
  atenderEnvio();
}