/*
  ============================================================================
  EJERCICIO 11: TERMOSTATO CON HISTERESIS Y PANTALLA LCD
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor DHT11 (modulo de 3 terminales)
                - Modulo rele Keyestudio de 5 V (marcas S, V, G)
                - Modulo LED de 3 W Keyestudio (marcas S, V, G): la "carga"
                  que simula un ventilador
                - Pantalla LCD 16x2 con adaptador I2C (GND, VCC, SDA, SCL)
                - Fuente externa de 5 V (1 A o mas) para la carga

  CONEXIONES:   DHT11   + -> 5V   OUT -> D2   - -> GND
                LCD I2C GND -> GND   VCC -> 5V   SDA -> A4   SCL -> A5
                Rele    S -> D7   V -> 5V   G -> GND
                Contactos del rele (lado de la carga, NO van al Arduino):
                  fuente 5 V (+) -> COM del rele
                  NO del rele    -> V del modulo LED de 3 W
                  fuente 5 V (+) -> S del modulo LED de 3 W
                  fuente 5 V (-) -> G del modulo LED de 3 W

  QUE HACE:     Mide la temperatura cada 2 segundos y decide con HISTERESIS:
                  - Si la temperatura SUPERA el umbral alto, enciende la carga.
                  - La carga permanece encendida hasta que la temperatura
                    BAJA del umbral bajo.
                  - Entre los dos umbrales NO cambia nada (zona de histeresis).
                La LCD muestra temperatura, humedad y el estado de la carga.

  SEGURIDAD:    Solo se conmutan cargas de BAJA TENSION (5 V). No conecte
                aqui la red electrica de la casa.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS
// ============================================================================

// Bus I2C (incluida en el nucleo de la placa).
#include <Wire.h>
// Sensor DHT11 (biblioteca "DHT sensor library" de Adafruit).
#include <DHT.h>
// Pantalla LCD (biblioteca "hd44780" de Bill Perry).
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

// Pin de datos del DHT11 (terminal OUT).
const int PIN_DHT = 2;

// Pin de senal del modulo rele (terminal S).
const int PIN_RELE = 7;

// Nivel que ENCIENDE el rele. El modulo Keyestudio se activa con HIGH;
// si su modulo se activa con LOW, cambiar estos dos valores entre si.
const int NIVEL_RELE_ENCENDIDO = HIGH;
const int NIVEL_RELE_APAGADO   = LOW;

// Umbral alto: si la temperatura lo SUPERA, se enciende la carga (grados C).
const float UMBRAL_ALTO_C = 30.0;

// Umbral bajo: si la temperatura BAJA de este valor, se apaga la carga (grados C).
const float UMBRAL_BAJO_C = 28.0;

// Cada cuantos ms se lee el sensor: 2000 ms (el DHT11 no debe leerse
// mas de una vez por segundo).
const unsigned long INTERVALO_LECTURA_MS = 2000UL;

// Cuantas lecturas fallidas seguidas se toleran antes de apagar la carga.
const int FALLOS_MAXIMOS = 3;

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

// Objeto del sensor (pin y tipo de sensor).
DHT sensor(PIN_DHT, DHT11);

// Objeto de la pantalla; la biblioteca busca sola la direccion I2C.
hd44780_I2Cexp lcd;

// Indica si la carga (el rele) esta encendida.
bool cargaEncendida = false;

// Cuantas lecturas fallidas seguidas hay.
int fallos = 0;

// Cuantas veces ha cambiado de estado el rele desde el encendido.
unsigned long conmutaciones = 0;

// Instante (millis) de la ultima lectura.
unsigned long ultimaLectura = 0;

// Indica si la pantalla respondio al iniciarse.
bool lcdDisponible = false;

// ============================================================================
// 4. FUNCIONES PROPIAS
// ============================================================================

// Escribe un texto en una fila de la LCD, completado con espacios hasta
// 16 caracteres para borrar restos del texto anterior (sin usar clear()).
void escribirFila(int fila, String texto) {
  // Si no hay pantalla, no se hace nada.
  if (!lcdDisponible) {
    return;
  }
  // Rellena con espacios hasta 16 caracteres.
  while (texto.length() < 16) {
    texto += ' ';
  }
  // Escribe solo los primeros 16 caracteres.
  lcd.setCursor(0, fila);
  lcd.print(texto.substring(0, 16));
}

// Enciende o apaga la carga y lleva la cuenta de las conmutaciones.
void cambiarCarga(bool encender) {
  // Pone el pin del rele en el nivel que corresponde.
  digitalWrite(PIN_RELE, encender ? NIVEL_RELE_ENCENDIDO : NIVEL_RELE_APAGADO);
  // Solo cuenta si realmente hubo un cambio de estado.
  if (encender != cargaEncendida) {
    conmutaciones++;
  }
  cargaEncendida = encender;
}

// Texto corto del estado de la carga para la LCD.
String textoEstado() {
  if (cargaEncendida) {
    return "ON";
  }
  return "OFF";
}

// ============================================================================
// 5. setup(): SE EJECUTA UNA SOLA VEZ AL ENCENDER
// ============================================================================
void setup() {
  // Abre la comunicacion con el Monitor Serial a 9600 baudios.
  Serial.begin(9600);
  delay(1500);

  // El rele se deja APAGADO antes de configurar el pin como salida,
  // para que no se active por un instante al encender.
  digitalWrite(PIN_RELE, NIVEL_RELE_APAGADO);
  pinMode(PIN_RELE, OUTPUT);

  // Inicia el sensor DHT11.
  sensor.begin();

  // Inicia la pantalla (16 columnas, 2 filas). Devuelve 0 si la encontro.
  int estado = lcd.begin(16, 2);
  if (estado == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  // Mensajes de bienvenida.
  Serial.println("TERMOSTATO CON HISTERESIS - ARDUINO UNO R4 WIFI");
  Serial.println("-----------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  Serial.print("Carga ON si la temperatura supera ");
  Serial.print(UMBRAL_ALTO_C, 1);
  Serial.print(" C; OFF si baja de ");
  Serial.print(UMBRAL_BAJO_C, 1);
  Serial.println(" C");

  // Muestra los umbrales en la LCD durante 2 segundos.
  escribirFila(0, "Termostato UAM");
  escribirFila(1, "ON>" + String((int)UMBRAL_ALTO_C) + " OFF<" + String((int)UMBRAL_BAJO_C) + " C");
  delay(2000);
}

// ============================================================================
// 6. loop(): SE REPITE CONTINUAMENTE
// ============================================================================
void loop() {
  // Lee el reloj una sola vez por vuelta.
  unsigned long ahora = millis();

  // Solo se mide cada 2 segundos.
  if (ahora - ultimaLectura < INTERVALO_LECTURA_MS) {
    return;
  }
  ultimaLectura = ahora;

  // Lee temperatura (grados C) y humedad relativa (%).
  float temperatura = sensor.readTemperature();
  float humedad = sensor.readHumidity();

  // ---------------------------------------------------------------------
  // LECTURA NO VALIDA
  // ---------------------------------------------------------------------
  if (isnan(temperatura) || isnan(humedad)) {
    fallos++;
    Serial.print("Error al leer el DHT11 (");
    Serial.print(fallos);
    Serial.println(" seguidos)");

    // Con varios fallos seguidos, por seguridad se apaga la carga.
    if (fallos >= FALLOS_MAXIMOS) {
      if (cargaEncendida) {
        cambiarCarga(false);
        Serial.println(">>> APAGADA: el sensor no responde");
      }
      escribirFila(0, "Error DHT11");
      escribirFila(1, "Carga: " + textoEstado());
    }
    return;
  }
  // Lectura valida: se reinicia el contador de fallos.
  fallos = 0;

  // ---------------------------------------------------------------------
  // DECISION CON HISTERESIS
  // ---------------------------------------------------------------------
  if (!cargaEncendida && temperatura > UMBRAL_ALTO_C) {
    // Estaba apagada y la temperatura supero el umbral alto: se enciende.
    cambiarCarga(true);
    Serial.print(">>> ENCENDIDA: ");
    Serial.print(temperatura, 1);
    Serial.print(" C supera el umbral alto (");
    Serial.print(UMBRAL_ALTO_C, 1);
    Serial.println(" C)");
  } else if (cargaEncendida && temperatura < UMBRAL_BAJO_C) {
    // Estaba encendida y la temperatura bajo del umbral bajo: se apaga.
    cambiarCarga(false);
    Serial.print(">>> APAGADA: ");
    Serial.print(temperatura, 1);
    Serial.print(" C bajo del umbral bajo (");
    Serial.print(UMBRAL_BAJO_C, 1);
    Serial.println(" C)");
  }
  // En cualquier otro caso (incluida la zona entre los dos umbrales)
  // el estado de la carga no cambia.

  // ---------------------------------------------------------------------
  // PANTALLA LCD
  // ---------------------------------------------------------------------
  // Fila 1: "Temp: 28.5 C ON"
  String fila1 = "Temp: ";
  fila1 += String(temperatura, 1);
  fila1 += " C ";
  fila1 += textoEstado();
  escribirFila(0, fila1);

  // Fila 2: "Hum: 60 % 28-30C" (humedad y banda de histeresis)
  String fila2 = "Hum: ";
  fila2 += String((int)(humedad + 0.5));
  fila2 += " % ";
  fila2 += String((int)UMBRAL_BAJO_C);
  fila2 += "-";
  fila2 += String((int)UMBRAL_ALTO_C);
  fila2 += "C";
  escribirFila(1, fila2);

  // ---------------------------------------------------------------------
  // MONITOR SERIAL
  // ---------------------------------------------------------------------
  Serial.print("Temp: ");
  Serial.print(temperatura, 1);
  Serial.print(" C | Hum: ");
  Serial.print(humedad, 0);
  Serial.print(" % | Carga: ");
  Serial.print(cargaEncendida ? "ENCENDIDA" : "APAGADA");
  Serial.print(" | Conmutaciones: ");
  Serial.println(conmutaciones);
}