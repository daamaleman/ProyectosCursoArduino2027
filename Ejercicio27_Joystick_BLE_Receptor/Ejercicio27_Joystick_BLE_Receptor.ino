/*
  ============================================================================
  EJERCICIO 27 - ARDUINO B (CENTRAL): SERVO REMOTO CON RESPUESTA SEGURA
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "B", con el servo)
  COMPONENTES:  - Servomotor SG90 con fuente externa de 5 V
                - LED rojo con resistencia de 330 ohm (LED de estado)
  BIBLIOTECAS:  "ArduinoBLE" y "Servo" (esta ultima ya viene con el IDE).

  CONEXIONES:   Servo     senal (amarillo) -> D9
                          rojo   -> positivo de la fuente externa de 5 V
                          marron -> negativo de la fuente externa de 5 V
                Fuente externa: negativo unido a GND del Arduino (tierra comun)
                LED       D8 -> resistencia 330 ohm -> LED (pata larga, +)
                          LED (pata corta, -) -> GND

  QUE HACE:     - Busca por BLE la placa A (por el UUID de su servicio), se
                  conecta y se suscribe al paquete de 3 bytes del joystick:
                  angulo, bloqueo y contador.
                - Mueve el servo al angulo recibido (limitado a 10..170 grados
                  para no forzar los topes).
                - Si el bloqueo es 1, IGNORA el movimiento: el servo se queda
                  donde esta.
                - RESPUESTA SEGURA: si pasan 2 s sin recibir ningun paquete (o
                  se pierde la conexion), devuelve el servo al CENTRO (90) y
                  ENCIENDE el LED de "sin enlace". Cuando vuelven los datos,
                  apaga el LED y retoma el control.
                - LED de estado:  encendido fijo  = SIN ENLACE
                                  parpadeo lento  = enlace activo y BLOQUEADO
                                  apagado         = enlace activo y libre

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del
                programa de la placa A (Ejercicio27_Joystick_BLE_Control).
                Alimente el servo con una fuente externa, no con el pin 5V
                del Arduino, y una las tierras.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS
// ============================================================================

#include <ArduinoBLE.h>
#include <Servo.h>

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_SERVO = 9;              // Servo: cable de senal.
// OJO: no se llama PIN_LED porque la placa UNO R4 WiFi ya define ese nombre.
const int PIN_LED_EXTERNO = 8;        // LED de estado con resistencia de 330 ohm.

// Angulos permitidos del servo (se evita llegar a los topes).
const int ANGULO_MINIMO = 10;
const int ANGULO_MAXIMO = 170;
const int ANGULO_CENTRO = 90;

// Si pasa este tiempo sin recibir paquetes, se considera "sin enlace".
const unsigned long TIEMPO_SIN_ENLACE_MS = 2000UL;

// Parpadeo del LED cuando esta bloqueado.
const unsigned long PERIODO_PARPADEO_MS = 600UL;

// Cada cuantos ms se avisa que se sigue buscando.
const unsigned long INTERVALO_AVISO_MS = 3000UL;

// ---- Identificadores BLE: deben ser IGUALES a los de la placa A ----
const char* UUID_SERVICIO = "e959981e-4cb1-42f3-822a-7f77ad26e2f3";
const char* UUID_MANDO    = "a103b281-b2a9-4f83-969b-d76ae2320f67";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

Servo servo;

bool enlaceActivo = false;            // Llegan paquetes a tiempo.
bool bloqueado = false;               // A pidio ignorar el movimiento.
int anguloActual = ANGULO_CENTRO;     // Ultimo angulo ordenado al servo.
unsigned long ultimoPaquete = 0;
unsigned long ultimoAviso = 0;
unsigned long ultimoParpadeo = 0;
bool ledParpadeo = false;

// ============================================================================
// 4. FUNCIONES PROPIAS: SERVO Y LED
// ============================================================================

// Ordena un angulo al servo (limitado a 10..170).
void moverServo(int angulo) {
  angulo = constrain(angulo, ANGULO_MINIMO, ANGULO_MAXIMO);
  if (angulo != anguloActual) {
    servo.write(angulo);
    anguloActual = angulo;
  }
}

// Actualiza el LED segun el estado del enlace y del bloqueo.
void actualizarLed() {
  if (!enlaceActivo) {
    digitalWrite(PIN_LED_EXTERNO, HIGH);           // Sin enlace: fijo.
  } else if (bloqueado) {
    if (millis() - ultimoParpadeo >= PERIODO_PARPADEO_MS) {
      ultimoParpadeo = millis();
      ledParpadeo = !ledParpadeo;
      digitalWrite(PIN_LED_EXTERNO, ledParpadeo ? HIGH : LOW);
    }
  } else {
    digitalWrite(PIN_LED_EXTERNO, LOW);            // Enlace libre: apagado.
  }
}

// Estado seguro: servo al centro y LED de "sin enlace".
void irAEstadoSeguro(const char* motivo) {
  if (enlaceActivo) {
    Serial.print("SIN ENLACE (");
    Serial.print(motivo);
    Serial.println("): servo al centro.");
  }
  enlaceActivo = false;
  moverServo(ANGULO_CENTRO);
  actualizarLed();
}

// ============================================================================
// 5. FUNCIONES PROPIAS: DATOS RECIBIDOS
// ============================================================================

// Procesa un paquete de 3 bytes: angulo, bloqueo y contador.
void procesarPaquete(const byte paquete[3]) {
  ultimoPaquete = millis();

  if (!enlaceActivo) {
    enlaceActivo = true;
    Serial.println("ENLACE ACTIVO: llegan paquetes de A.");
  }

  bool nuevoBloqueo = (paquete[1] == 1);
  if (nuevoBloqueo != bloqueado) {
    bloqueado = nuevoBloqueo;
    Serial.println(bloqueado ? "BLOQUEADO: se ignora el movimiento."
                             : "LIBRE: el servo sigue al joystick.");
  }

  if (paquete[0] > 180) {
    Serial.print("Angulo fuera de rango ignorado: ");
    Serial.println(paquete[0]);
  } else if (!bloqueado) {
    moverServo(paquete[0]);
    Serial.print("Angulo recibido: ");
    Serial.print(paquete[0]);
    Serial.print(" -> servo en ");
    Serial.println(anguloActual);
  }
  actualizarLed();
}

// Si A deja de enviar paquetes por 2 s, se pasa al estado seguro.
void revisarTiempoSinEnlace() {
  if (enlaceActivo && millis() - ultimoPaquete >= TIEMPO_SIN_ENLACE_MS) {
    irAEstadoSeguro("2 s sin datos");
  }
}

// ============================================================================
// 6. FUNCIONES PROPIAS: BLE
// ============================================================================

// Empieza a buscar placas que anuncien el servicio de este ejercicio.
void iniciarBusqueda() {
  BLE.scanForUuid(UUID_SERVICIO);
  ultimoAviso = millis();
  Serial.println("Buscando la placa A...");
}

// Maneja la conexion con la placa A mientras dure. Devuelve cuando se pierde.
void atenderControl(BLEDevice control) {
  Serial.print("Placa A encontrada: ");
  Serial.print(control.address());
  Serial.print("  (RSSI ");
  Serial.print(control.rssi());
  Serial.println(" dBm)");

  BLE.stopScan();                   // Durante la conexion no se busca.

  if (!control.connect()) {
    Serial.println("No se pudo conectar.");
    return;
  }
  Serial.println("Conectado. Buscando la caracteristica...");

  if (!control.discoverAttributes()) {
    Serial.println("No se pudieron leer los servicios de A.");
    control.disconnect();
    return;
  }

  BLECharacteristic caracMando = control.characteristic(UUID_MANDO);
  if (!caracMando) {
    Serial.println("A no tiene la caracteristica esperada (revise los UUID).");
    control.disconnect();
    return;
  }
  if (!caracMando.canSubscribe() || !caracMando.subscribe()) {
    Serial.println("No se pudo suscribir a las notificaciones.");
    control.disconnect();
    return;
  }

  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("Suscrito. Esperando paquetes de A...");
  ultimoPaquete = millis();         // La cuenta de 2 s empieza aqui.

  byte paquete[3];

  // Mientras A siga conectada se atienden sus paquetes y el tiempo de espera.
  while (control.connected()) {
    BLE.poll();
    if (caracMando.valueUpdated()) {
      int largo = caracMando.readValue(paquete, 3);
      if (largo == 3) {
        procesarPaquete(paquete);
      }
    }
    revisarTiempoSinEnlace();
    actualizarLed();
  }

  // Se perdio la conexion: estado seguro y a buscar de nuevo.
  Serial.println("Se perdio la conexion con A.");
  digitalWrite(LED_BUILTIN, LOW);
  irAEstadoSeguro("conexion perdida");
}

// ============================================================================
// 7. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED_EXTERNO, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(PIN_LED_EXTERNO, HIGH);       // Arranca "sin enlace".

  servo.attach(PIN_SERVO);
  servo.write(ANGULO_CENTRO);                // El servo arranca en el centro.

  Serial.println("ARDUINO B (CENTRAL): SERVO REMOTO CON RESPUESTA SEGURA");
  Serial.println("-----------------------------------------------------");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    while (true) {
      // Se detiene aqui con el LED de "sin enlace" encendido.
    }
  }

  iniciarBusqueda();
}

// ============================================================================
// 8. LOOP
// ============================================================================

void loop() {
  // available() devuelve una placa cuando el escaneo encuentra una.
  BLEDevice control = BLE.available();

  if (control) {
    atenderControl(control);
    iniciarBusqueda();              // Vuelve a buscar a A.
  }

  actualizarLed();

  if (millis() - ultimoAviso >= INTERVALO_AVISO_MS) {
    ultimoAviso = millis();
    Serial.println("Buscando la placa A...");
  }
}