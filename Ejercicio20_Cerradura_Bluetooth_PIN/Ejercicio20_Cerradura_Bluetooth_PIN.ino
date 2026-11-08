/*
  ============================================================================
  EJERCICIO 20: CERRADURA CON CONTROL BLUETOOTH Y PIN
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (Bluetooth Low Energy, rol PERIFERICO)
  COMPONENTES:  - Servomotor SG90 (el cerrojo)
                - Fuente regulada de 5 V para el servo
                - Modulo semaforo Keyestudio (GND, G, Y, R)
                - Modulo buzzer PASIVO Keyestudio (S, V, G)
                - Pantalla LCD 16x2 con adaptador I2C (GND, VCC, SDA, SCL)
  BIBLIOTECAS:  "ArduinoBLE" y "hd44780" (Bill Perry). Servo viene con el IDE.
  CLIENTE BLE:  Un celular con la app nRF Connect o LightBlue.

  CONEXIONES:   Servo      senal (naranja) -> D9
                           + (rojo)  -> + de la fuente de 5 V
                           - (marron) -> - de la fuente de 5 V
                           El - de la fuente tambien va a GND del Arduino.
                Semaforo   GND -> GND   G (verde) -> D2
                           Y (amarillo) -> D3   R (rojo) -> D4
                Buzzer     S -> D5   V -> 5V   G -> GND
                LCD I2C    SDA -> A4   SCL -> A5   VCC -> 5V   GND -> GND

  QUE HACE:     La placa se anuncia por Bluetooth como "CERRADURA_UAM" y ofrece
                un servicio con tres caracteristicas:
                  1) PIN (escritura): el celular escribe aqui el PIN en texto.
                  2) ESTADO (lectura y notificacion): 0 = CERRADA, 1 = ABIERTA,
                     2 = BLOQUEADA.
                  3) INTENTOS RESTANTES (lectura y notificacion): de 3 a 0.
                - PIN correcto: el servo abre y se cierra solo a los 5 s.
                - PIN incorrecto: suena un aviso y se cuenta un intento fallido.
                - 3 intentos fallidos seguidos: la cerradura se BLOQUEA 30 s y
                  el buzzer pita cada segundo. Durante el bloqueo se ignoran
                  los PIN, aunque sean correctos.
                - Semaforo: rojo = cerrada, verde = abierta, amarillo = PIN
                  incorrecto, rojo parpadeando = bloqueada.
                - Cada intento queda registrado en el Monitor Serial.
                - El programa NO usa delay(): todo se hace con millis().

  IMPORTANTE:   Es un ejercicio didactico y NO sustituye una cerradura real.
                El PIN es fijo y viaja sin cifrar, sin emparejamiento seguro.
                Cualquiera que escuche la comunicacion puede copiarlo. Cambie
                el PIN de ejemplo antes de la practica. Si reinicia la placa
                se borra el contador de intentos y el bloqueo.

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
#include <Servo.h>

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_VERDE    = 2;    // Semaforo: verde.
const int PIN_AMARILLO = 3;    // Semaforo: amarillo.
const int PIN_ROJO     = 4;    // Semaforo: rojo.
const int PIN_BUZZER   = 5;    // Buzzer pasivo (se maneja con tone()).
const int PIN_SERVO    = 9;    // Servo del cerrojo.

// PIN de ejemplo: CAMBIELO antes de la practica. Solo digitos.
const char* PIN_CORRECTO = "1234";
const int LARGO_PIN = 4;

// Intentos fallidos seguidos que bloquean la cerradura.
const int MAX_INTENTOS = 3;
// Cuanto tiempo queda abierta la cerradura (ms).
const unsigned long TIEMPO_ABIERTA_MS = 5000UL;
// Cuanto dura el bloqueo (ms).
const unsigned long TIEMPO_BLOQUEO_MS = 30000UL;
// Cuanto dura el aviso de PIN incorrecto en el semaforo y en la LCD (ms).
const unsigned long TIEMPO_AVISO_MS = 1500UL;

// Posiciones del servo (grados) y velocidad de movimiento.
const int POSICION_CERRADA = 0;
const int POSICION_ABIERTA = 90;
const int PASO_GRADOS = 3;
const unsigned long INTERVALO_SERVO_MS = 15UL;

// Tonos del buzzer (Hz) y duracion (ms).
const int TONO_ABRIR = 1800;
const unsigned long DURACION_ABRIR_MS = 200UL;
const int TONO_ERROR = 400;
const unsigned long DURACION_ERROR_MS = 300UL;
const int TONO_BLOQUEO = 2000;
const unsigned long DURACION_BLOQUEO_MS = 200UL;
const unsigned long PERIODO_BLOQUEO_MS = 1000UL;

// Parpadeo del rojo cuando esta bloqueada (ms).
const unsigned long MEDIO_PARPADEO_MS = 500UL;

// Estados de la cerradura. Son los valores que lee el celular.
const int ESTADO_CERRADA   = 0;
const int ESTADO_ABIERTA   = 1;
const int ESTADO_BLOQUEADA = 2;

// ---- Identificadores BLE (UUID propios, generados una sola vez) ----
// Si cambia un UUID aqui, debe cambiarlo tambien en la app del celular.
const char* UUID_SERVICIO   = "fbc75c60-ee8c-48c3-82e0-2946823ad8f9";
const char* UUID_PIN        = "bc2819f5-a1e6-4501-841a-9dc4005557ac";
const char* UUID_ESTADO     = "010e6e6f-35c0-473f-ac35-e6cb455351dd";
const char* UUID_INTENTOS   = "2e8ef711-0c7f-4e93-b08c-1fa4cd4f571f";
const char* NOMBRE_BLE      = "CERRADURA_UAM";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

hd44780_I2Cexp lcd;
bool lcdDisponible = false;
String filaActual[2] = {"", ""};

Servo cerrojo;
int posicionServo = POSICION_CERRADA;
int objetivoServo = POSICION_CERRADA;
unsigned long ultimoPasoServo = 0;

// Servicio y caracteristicas BLE.
BLEService servicioCerradura(UUID_SERVICIO);
// El PIN llega como texto de hasta 8 caracteres (solo escritura).
BLEStringCharacteristic caracPin(UUID_PIN, BLEWrite, 8);
// El estado y los intentos restantes se pueden leer y notificar.
BLEByteCharacteristic caracEstado(UUID_ESTADO, BLERead | BLENotify);
BLEByteCharacteristic caracIntentos(UUID_INTENTOS, BLERead | BLENotify);

// Estado de la cerradura.
int estado = ESTADO_CERRADA;
int intentosFallidos = 0;
unsigned long inicioApertura = 0;
unsigned long inicioBloqueo = 0;
unsigned long inicioAviso = 0;
bool avisoActivo = false;
unsigned long ultimoPitidoBloqueo = 0;

// Contador de intentos para el registro del Monitor Serial.
unsigned long numeroIntento = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: REGISTRO Y BLE
// ============================================================================

// Escribe el tiempo transcurrido al inicio de cada linea del registro.
void imprimirMarcaTiempo() {
  unsigned long ahora = millis();
  Serial.print("[");
  Serial.print(ahora / 1000UL);
  Serial.print(".");
  Serial.print((ahora % 1000UL) / 100UL);
  Serial.print(" s] ");
}

// Texto del estado (para el Monitor Serial).
const char* textoEstado() {
  if (estado == ESTADO_ABIERTA) {
    return "ABIERTA";
  }
  if (estado == ESTADO_BLOQUEADA) {
    return "BLOQUEADA";
  }
  return "CERRADA";
}

// Intentos que quedan antes del bloqueo.
int intentosRestantes() {
  int restantes = MAX_INTENTOS - intentosFallidos;
  return (restantes < 0) ? 0 : restantes;
}

// Actualiza las caracteristicas de lectura. Si hay un celular suscrito,
// recibe la notificacion.
void publicarEstado() {
  caracEstado.writeValue((byte)estado);
  caracIntentos.writeValue((byte)intentosRestantes());
}

// Devuelve la direccion del celular conectado (o "desconocido").
String direccionCentral() {
  BLEDevice central = BLE.central();
  if (central) {
    return central.address();
  }
  return String("desconocido");
}

// Se llama sola cuando un celular se conecta.
void alConectar(BLEDevice central) {
  imprimirMarcaTiempo();
  Serial.print("Celular conectado: ");
  Serial.println(central.address());
}

// Se llama sola cuando el celular se desconecta.
void alDesconectar(BLEDevice central) {
  imprimirMarcaTiempo();
  Serial.print("Celular desconectado: ");
  Serial.println(central.address());
  // Se vuelve a anunciar para que otro celular pueda conectarse.
  BLE.advertise();
}

// ============================================================================
// 5. FUNCIONES PROPIAS: CERROJO Y ESTADOS
// ============================================================================

// Mueve el servo de a PASO_GRADOS hacia el objetivo, sin bloquear.
void moverServo() {
  unsigned long ahora = millis();
  if (posicionServo == objetivoServo || ahora - ultimoPasoServo < INTERVALO_SERVO_MS) {
    return;
  }
  ultimoPasoServo = ahora;
  if (posicionServo < objetivoServo) {
    posicionServo = min(posicionServo + PASO_GRADOS, objetivoServo);
  } else {
    posicionServo = max(posicionServo - PASO_GRADOS, objetivoServo);
  }
  cerrojo.write(posicionServo);
}

// Abre la cerradura por TIEMPO_ABIERTA_MS.
void abrirCerradura() {
  estado = ESTADO_ABIERTA;
  inicioApertura = millis();
  objetivoServo = POSICION_ABIERTA;
  tone(PIN_BUZZER, TONO_ABRIR, DURACION_ABRIR_MS);
  publicarEstado();
}

// Cierra la cerradura.
void cerrarCerradura() {
  estado = ESTADO_CERRADA;
  objetivoServo = POSICION_CERRADA;
  publicarEstado();
  imprimirMarcaTiempo();
  Serial.println("Cerradura CERRADA (paso el tiempo)");
}

// Bloquea la cerradura por TIEMPO_BLOQUEO_MS.
void bloquearCerradura() {
  estado = ESTADO_BLOQUEADA;
  inicioBloqueo = millis();
  objetivoServo = POSICION_CERRADA;
  ultimoPitidoBloqueo = millis() - PERIODO_BLOQUEO_MS;   // Suena enseguida.
  publicarEstado();
  imprimirMarcaTiempo();
  Serial.print("Cerradura BLOQUEADA por ");
  Serial.print(TIEMPO_BLOQUEO_MS / 1000UL);
  Serial.println(" s");
}

// Termina el bloqueo y borra los intentos fallidos.
void terminarBloqueo() {
  estado = ESTADO_CERRADA;
  intentosFallidos = 0;
  publicarEstado();
  imprimirMarcaTiempo();
  Serial.println("Bloqueo terminado: cerradura CERRADA, intentos reiniciados");
}

// Comprueba que el texto tenga LARGO_PIN digitos.
bool formatoPinValido(const String& texto) {
  if (texto.length() != LARGO_PIN) {
    return false;
  }
  for (int i = 0; i < LARGO_PIN; i++) {
    char c = texto.charAt(i);
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

// Revisa un PIN recibido por Bluetooth. Nunca se escribe el PIN en el registro.
void procesarPin(const String& recibido) {
  numeroIntento++;
  String quien = direccionCentral();
  imprimirMarcaTiempo();
  Serial.print("Intento ");
  Serial.print(numeroIntento);
  Serial.print(" | celular ");
  Serial.print(quien);
  Serial.print(" | ");

  if (estado == ESTADO_BLOQUEADA) {
    Serial.println("IGNORADO: la cerradura esta bloqueada");
    return;
  }
  if (estado == ESTADO_ABIERTA) {
    Serial.println("IGNORADO: la cerradura ya esta abierta");
    return;
  }

  bool correcto = formatoPinValido(recibido) && (recibido == PIN_CORRECTO);
  if (correcto) {
    intentosFallidos = 0;
    Serial.println("PIN CORRECTO: se abre");
    abrirCerradura();
    return;
  }

  intentosFallidos++;
  Serial.print("PIN INCORRECTO | fallidos ");
  Serial.print(intentosFallidos);
  Serial.print(" de ");
  Serial.println(MAX_INTENTOS);
  tone(PIN_BUZZER, TONO_ERROR, DURACION_ERROR_MS);
  avisoActivo = true;
  inicioAviso = millis();
  if (intentosFallidos >= MAX_INTENTOS) {
    bloquearCerradura();
  } else {
    publicarEstado();
  }
}

// Revisa los tiempos: cierre automatico, fin del bloqueo y fin del aviso.
void revisarTiempos() {
  unsigned long ahora = millis();
  if (estado == ESTADO_ABIERTA && ahora - inicioApertura >= TIEMPO_ABIERTA_MS) {
    cerrarCerradura();
  }
  if (estado == ESTADO_BLOQUEADA && ahora - inicioBloqueo >= TIEMPO_BLOQUEO_MS) {
    terminarBloqueo();
  }
  if (avisoActivo && ahora - inicioAviso >= TIEMPO_AVISO_MS) {
    avisoActivo = false;
  }
}

// ============================================================================
// 6. FUNCIONES PROPIAS: SEMAFORO, BUZZER Y LCD
// ============================================================================

// Semaforo: rojo = cerrada, verde = abierta, amarillo = PIN incorrecto,
// rojo parpadeando = bloqueada.
void actualizarSemaforo() {
  bool verde = false;
  bool amarillo = false;
  bool rojo = false;
  if (estado == ESTADO_ABIERTA) {
    verde = true;
  } else if (estado == ESTADO_BLOQUEADA) {
    rojo = (((millis() / MEDIO_PARPADEO_MS) % 2UL) == 0UL);
  } else if (avisoActivo) {
    amarillo = true;
  } else {
    rojo = true;
  }
  digitalWrite(PIN_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, amarillo ? HIGH : LOW);
  digitalWrite(PIN_ROJO, rojo ? HIGH : LOW);
}

// Durante el bloqueo el buzzer pita una vez por segundo, sin bloquear.
void actualizarBuzzer() {
  if (estado != ESTADO_BLOQUEADA) {
    return;
  }
  unsigned long ahora = millis();
  if (ahora - ultimoPitidoBloqueo >= PERIODO_BLOQUEO_MS) {
    ultimoPitidoBloqueo = ahora;
    tone(PIN_BUZZER, TONO_BLOQUEO, DURACION_BLOQUEO_MS);
  }
}

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

// Segundos que faltan para un tiempo limite (se redondea hacia arriba).
int segundosRestantes(unsigned long inicio, unsigned long duracion) {
  unsigned long pasado = millis() - inicio;
  if (pasado >= duracion) {
    return 0;
  }
  return (int)((duracion - pasado + 999UL) / 1000UL);
}

// Actualiza las dos filas de la LCD.
void actualizarPantalla() {
  if (estado == ESTADO_ABIERTA) {
    escribirFila(0, "ABIERTA");
    escribirFila(1, "Cierra en " + String(segundosRestantes(inicioApertura, TIEMPO_ABIERTA_MS)) + " s");
  } else if (estado == ESTADO_BLOQUEADA) {
    escribirFila(0, "BLOQUEADA");
    escribirFila(1, "Espere " + String(segundosRestantes(inicioBloqueo, TIEMPO_BLOQUEO_MS)) + " s");
  } else {
    escribirFila(0, avisoActivo ? "PIN INCORRECTO" : "CERRADA");
    escribirFila(1, "Intentos: " + String(intentosFallidos) + " de " + String(MAX_INTENTOS));
  }
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

  // El servo arranca en la posicion cerrada.
  cerrojo.attach(PIN_SERVO);
  cerrojo.write(POSICION_CERRADA);

  // Pantalla LCD.
  if (lcd.begin(16, 2) == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  Serial.println("CERRADURA BLUETOOTH CON PIN - ARDUINO UNO R4 WIFI");
  Serial.println("-------------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  escribirFila(0, "Cerradura UAM");
  escribirFila(1, "Iniciando BLE...");

  // Bluetooth: si no arranca, la cerradura queda cerrada y avisa.
  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar Bluetooth BLE.");
    Serial.println("Revise Herramientas > Firmware Updater y la biblioteca ArduinoBLE.");
    escribirFila(0, "ERROR BLE");
    escribirFila(1, "Cerrada");
    digitalWrite(PIN_ROJO, HIGH);
    while (true) {
      // Se queda aqui: sin Bluetooth no hay forma de abrir.
    }
  }

  BLE.setLocalName(NOMBRE_BLE);
  BLE.setAdvertisedService(servicioCerradura);
  servicioCerradura.addCharacteristic(caracPin);
  servicioCerradura.addCharacteristic(caracEstado);
  servicioCerradura.addCharacteristic(caracIntentos);
  BLE.addService(servicioCerradura);
  BLE.setEventHandler(BLEConnected, alConectar);
  BLE.setEventHandler(BLEDisconnected, alDesconectar);
  publicarEstado();
  BLE.advertise();

  Serial.print("Nombre Bluetooth: ");
  Serial.println(NOMBRE_BLE);
  Serial.print("UUID del servicio: ");
  Serial.println(UUID_SERVICIO);
  Serial.println("Esperando un celular y un PIN de 4 digitos...");
}

// ============================================================================
// 8. LOOP
// ============================================================================

void loop() {
  // Atiende Bluetooth: conexiones, desconexiones y escrituras.
  BLE.poll();

  // Si el celular escribio un PIN, se revisa.
  if (caracPin.written()) {
    String recibido = caracPin.value();
    procesarPin(recibido);
  }

  revisarTiempos();
  moverServo();
  actualizarSemaforo();
  actualizarBuzzer();
  actualizarPantalla();
}