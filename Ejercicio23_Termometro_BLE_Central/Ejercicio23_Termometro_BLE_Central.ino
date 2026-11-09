/*
  ============================================================================
  EJERCICIO 23 - ARDUINO B (CENTRAL): PANTALLA Y ALARMA DEL TERMOMETRO
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi (la placa "B", con pantalla y alarma)
  COMPONENTES:  - LCD 1602 con modulo I2C
                - Buzzer pasivo (se maneja con tone())
                - LED rojo de 5 mm y resistencia de 1 kOhm
  BIBLIOTECAS:  "ArduinoBLE" y "hd44780" (Bill Perry), desde el Administrador
                de bibliotecas del IDE.

  CONEXIONES:   LCD I2C   GND -> GND   VCC -> 5V   SDA -> SDA (A4)   SCL -> SCL (A5)
                Buzzer    (-) -> GND   (+) o S -> D5
                LED       D7 -> resistencia de 1 kOhm -> LED (pata larga, +)
                          LED (pata corta, -) -> GND

  QUE HACE:     - Busca por BLE la placa A (por el UUID de su servicio), se
                  conecta y se suscribe a la caracteristica de temperatura.
                - Muestra la temperatura en la LCD ("Temp: 28 C") y el estado.
                - Si la temperatura SUPERA el umbral (30 C), enciende el LED y
                  hace sonar el buzzer con pitidos cada segundo.
                - La alarma se apaga cuando la temperatura baja a 29 C o menos
                  (1 grado de margen para que no se prenda y apague en el limite).
                - Si A envia 255 (fallo del sensor), muestra "Error sensor".
                - Si pasan 8 s sin recibir datos, muestra "Sin datos".
                - Si se pierde la conexion, apaga la alarma, avisa en la LCD y
                  vuelve a buscar a A.

  IMPORTANTE:   Los UUID de este programa deben ser IGUALES a los del
                programa de la placa A (Ejercicio23_Termometro_BLE_Periferico).
                Si se pierde la conexion, la alarma NO suena: la LCD muestra
                "Sin conexion". No use este montaje como alarma de seguridad.

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

const int PIN_BUZZER = 5;             // Buzzer pasivo (se maneja con tone()).
// OJO: no se llama PIN_LED porque la placa UNO R4 WiFi ya define ese nombre.
const int PIN_LED_EXTERNO = 7;        // LED externo con resistencia de 1 kOhm.

// ---- Alarma ----
const int UMBRAL_ALARMA_C = 30;       // La alarma se activa si la temp. lo SUPERA.
const int HISTERESIS_C = 1;           // Se apaga cuando temp <= umbral - 1.
const int TONO_ALARMA_HZ = 2000;
const unsigned long DURACION_PITIDO_MS = 200UL;
const unsigned long PERIODO_PITIDO_MS = 1000UL;

// Valor que A envia cuando el sensor no da una lectura valida.
const byte VALOR_SIN_LECTURA = 255;

// Si pasa este tiempo sin datos de A, se muestra "Sin datos".
const unsigned long TIEMPO_SIN_DATOS_MS = 8000UL;

// Cada cuantos ms se avisa que se sigue buscando.
const unsigned long INTERVALO_AVISO_MS = 3000UL;

// ---- Identificadores BLE: deben ser IGUALES a los de la placa A ----
const char* UUID_SERVICIO    = "8226e03e-9446-4065-9f04-d8ff1cc1f2ec";
const char* UUID_TEMPERATURA = "65aa3c1d-c08d-4d95-b38a-019e2b6a0c49";

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

hd44780_I2Cexp lcd;
bool lcdDisponible = false;
String filaActual[2] = {"", ""};      // Lo que ya se escribio en cada fila.

// Estados de la lectura mostrada.
const int LECTURA_NINGUNA = 0;        // Aun no llega ningun dato.
const int LECTURA_VALIDA  = 1;        // Hay una temperatura valida.
const int LECTURA_ERROR   = 2;        // A informa un fallo del sensor.
const int LECTURA_VENCIDA = 3;        // Pasaron 8 s sin datos.

int estadoLectura = LECTURA_NINGUNA;
byte temperaturaActual = 0;
bool alarmaActiva = false;
unsigned long ultimoDato = 0;
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

// Muestra la temperatura y el estado segun la lectura actual.
void actualizarPantalla() {
  if (estadoLectura == LECTURA_VALIDA) {
    escribirFila(0, "Temp: " + String(temperaturaActual) + " C");
    if (alarmaActiva) {
      escribirFila(1, "ALARMA > " + String(UMBRAL_ALARMA_C) + " C");
    } else {
      escribirFila(1, "Normal");
    }
  } else if (estadoLectura == LECTURA_ERROR) {
    escribirFila(0, "Temp: -- C");
    escribirFila(1, "Error sensor");
  } else if (estadoLectura == LECTURA_VENCIDA) {
    escribirFila(0, "Temp: -- C");
    escribirFila(1, "Sin datos");
  } else {
    escribirFila(0, "Temp: -- C");
    escribirFila(1, "Esperando dato");
  }
}

// ============================================================================
// 5. FUNCIONES PROPIAS: ALARMA
// ============================================================================

// Apaga LED y buzzer.
void apagarAlarma() {
  alarmaActiva = false;
  digitalWrite(PIN_LED_EXTERNO, LOW);
  noTone(PIN_BUZZER);
}

// Decide si la alarma debe estar activa (con histeresis).
void actualizarAlarma() {
  if (estadoLectura != LECTURA_VALIDA) {
    apagarAlarma();
    return;
  }
  if (!alarmaActiva && temperaturaActual > UMBRAL_ALARMA_C) {
    alarmaActiva = true;
    ultimoPitido = millis() - PERIODO_PITIDO_MS;   // El primer pitido es ya.
    Serial.println("ALARMA: temperatura sobre el umbral");
  } else if (alarmaActiva && temperaturaActual <= UMBRAL_ALARMA_C - HISTERESIS_C) {
    apagarAlarma();
    Serial.println("Alarma apagada: la temperatura bajo");
  }
}

// Mantiene el LED encendido y los pitidos mientras la alarma este activa.
void atenderAlarma() {
  if (!alarmaActiva) {
    return;
  }
  digitalWrite(PIN_LED_EXTERNO, HIGH);
  if (millis() - ultimoPitido >= PERIODO_PITIDO_MS) {
    ultimoPitido = millis();
    tone(PIN_BUZZER, TONO_ALARMA_HZ, DURACION_PITIDO_MS);
  }
}

// ============================================================================
// 6. FUNCIONES PROPIAS: DATOS RECIBIDOS
// ============================================================================

// Procesa el byte recibido de la placa A.
void procesarTemperatura(byte valor) {
  ultimoDato = millis();
  if (valor == VALOR_SIN_LECTURA) {
    estadoLectura = LECTURA_ERROR;
    Serial.println("Dato recibido: 255 (A informa fallo del sensor)");
  } else {
    estadoLectura = LECTURA_VALIDA;
    temperaturaActual = valor;
    Serial.print("Temperatura recibida: ");
    Serial.print(valor);
    Serial.println(" C");
  }
  actualizarAlarma();
  actualizarPantalla();
}

// Si A deja de enviar datos, no se sigue mostrando una temperatura vieja.
void revisarDatosVencidos() {
  if (estadoLectura == LECTURA_VENCIDA) {
    return;
  }
  if (millis() - ultimoDato >= TIEMPO_SIN_DATOS_MS) {
    estadoLectura = LECTURA_VENCIDA;
    Serial.println("Aviso: no llegan datos de A desde hace 8 s");
    actualizarAlarma();
    actualizarPantalla();
  }
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
void atenderPeriferico(BLEDevice periferico) {
  Serial.print("Placa A encontrada: ");
  Serial.print(periferico.address());
  Serial.print("  (RSSI ");
  Serial.print(periferico.rssi());
  Serial.println(" dBm)");

  BLE.stopScan();                   // Durante la conexion no se busca.

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

  BLECharacteristic caracTemp = periferico.characteristic(UUID_TEMPERATURA);
  if (!caracTemp) {
    Serial.println("A no tiene la caracteristica esperada (revise los UUID).");
    periferico.disconnect();
    return;
  }
  if (!caracTemp.canSubscribe() || !caracTemp.subscribe()) {
    Serial.println("No se pudo suscribir a las notificaciones.");
    periferico.disconnect();
    return;
  }

  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("Suscrito. Esperando la temperatura de A...");

  // Se lee el valor actual para no esperar al siguiente aviso.
  byte valor = VALOR_SIN_LECTURA;
  caracTemp.readValue(valor);
  ultimoDato = millis();
  if (valor == VALOR_SIN_LECTURA) {
    estadoLectura = LECTURA_NINGUNA;
    actualizarAlarma();
    actualizarPantalla();
  } else {
    procesarTemperatura(valor);
  }

  // Mientras A siga conectada se atienden sus avisos y la alarma.
  while (periferico.connected()) {
    BLE.poll();
    if (caracTemp.valueUpdated()) {
      caracTemp.readValue(valor);
      procesarTemperatura(valor);
    }
    revisarDatosVencidos();
    atenderAlarma();
  }

  // Se perdio la conexion: se apaga la alarma y se avisa en la pantalla.
  Serial.println("Se perdio la conexion con A.");
  digitalWrite(LED_BUILTIN, LOW);
  apagarAlarma();
  estadoLectura = LECTURA_NINGUNA;
  escribirFila(0, "Sin conexion");
  escribirFila(1, "Buscando a A...");
}

// ============================================================================
// 8. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_EXTERNO, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(PIN_LED_EXTERNO, LOW);
  digitalWrite(LED_BUILTIN, LOW);

  // Pantalla LCD.
  if (lcd.begin(16, 2) == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  Serial.println("ARDUINO B (CENTRAL): PANTALLA Y ALARMA DEL TERMOMETRO");
  Serial.println("-----------------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  escribirFila(0, "Termometro UAM");
  escribirFila(1, "Iniciando BLE...");

  if (!BLE.begin()) {
    Serial.println("ERROR: no se pudo iniciar el BLE.");
    escribirFila(0, "ERROR BLE");
    escribirFila(1, "Reinicie la placa");
    while (true) {
      // Se detiene aqui.
    }
  }

  escribirFila(0, "Buscando a A...");
  escribirFila(1, "");
  iniciarBusqueda();
}

// ============================================================================
// 9. LOOP
// ============================================================================

void loop() {
  // available() devuelve una placa cuando el escaneo encuentra una.
  BLEDevice periferico = BLE.available();

  if (periferico) {
    atenderPeriferico(periferico);
    iniciarBusqueda();              // Vuelve a buscar a A.
  }

  if (millis() - ultimoAviso >= INTERVALO_AVISO_MS) {
    ultimoAviso = millis();
    Serial.println("Buscando la placa A...");
  }
}