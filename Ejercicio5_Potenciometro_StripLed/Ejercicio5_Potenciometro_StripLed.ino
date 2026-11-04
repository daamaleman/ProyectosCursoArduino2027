/*
  ====================================================================
  EJERCICIO 5B: DIMMER CON POTENCIOMETRO Y STRIP LED
  ====================================================================
  Placa:       Arduino UNO R4 WiFi
  Componentes: Modulo potenciometro Keyestudio (VR Sensor, pines S, V, G)
               Tira LED RGB direccionable de 8 LED (NeoPixel / WS2812)
               Resistencia de 330 ohmios en la linea de datos
  Libreria:    Adafruit NeoPixel (Administrador de bibliotecas)

  CONEXIONES
    Potenciometro:  S -> A0     V -> 5V     G -> GND
    Tira LED:       +5V -> 5V   GND -> GND
                    DIN -> resistencia 330 ohm -> D6

  QUE HACE EL PROGRAMA
    Al girar el potenciometro cambia el brillo de toda la tira, desde
    apagada (0 %) hasta el maximo permitido (100 %). El Monitor Serial
    muestra la lectura, el valor de brillo y el porcentaje.

    A diferencia del ejercicio 5 (LED simple con PWM), aqui el brillo
    se envia como DATOS a la tira: cada LED recibe el valor de rojo,
    verde y azul que debe mostrar.
  ====================================================================
*/

// Se incluye la libreria que sabe hablar con la tira LED.
#include <Adafruit_NeoPixel.h>


// ---------------------------------------------------------------
// 1. PINES Y CONSTANTES
// ---------------------------------------------------------------

// Pin analogico A0: recibe la senal del potenciometro.
const int PIN_POT = A0;

// Pin digital D6: envia los datos a la tira (terminal DIN).
const int PIN_TIRA = 6;

// Cantidad de LED de la tira.
const int NUM_LEDS = 8;

// Color base de la tira, con valores de 0 a 255 para rojo, verde y
// azul. 255, 255, 255 = blanco. Ejemplos: 255, 0, 0 = rojo;
// 0, 0, 255 = azul; 255, 120, 0 = naranja.
const int COLOR_ROJO  = 255;
const int COLOR_VERDE = 255;
const int COLOR_AZUL  = 255;

// Brillo maximo permitido (de 0 a 255). Cada LED consume mucha
// corriente en blanco y a brillo maximo; este limite protege el
// puerto USB. Se recomienda no pasar de 120 con alimentacion USB.
const int BRILLO_MAXIMO = 120;

// Cada cuantos milisegundos se lee el potenciometro (50 ms = 20
// veces por segundo).
const unsigned long INTERVALO_LECTURA = 50;

// Cuantas lecturas se promedian. Reduce el temblor de la lectura.
const int MUESTRAS = 8;

// true invierte el sentido de giro del potenciometro.
const bool INVERTIR_GIRO = false;


// ---------------------------------------------------------------
// 2. OBJETO TIRA Y VARIABLES DE ESTADO
// ---------------------------------------------------------------

// Se crea el objeto "tira": numero de LED, pin de datos y tipo de
// LED (orden de colores GRB y velocidad de 800 kHz).
Adafruit_NeoPixel tira(NUM_LEDS, PIN_TIRA, NEO_GRB + NEO_KHZ800);

// Instante (en ms) de la ultima lectura.
unsigned long ultimaLectura = 0;

// Ultimo brillo enviado a la tira. Se inicia en -1 (valor imposible)
// para que el primer valor siempre se envie.
int brilloAnterior = -1;


// ---------------------------------------------------------------
// 3. FUNCION leerPotenciometro()
// ---------------------------------------------------------------
// Lee el potenciometro varias veces y devuelve el promedio, un
// numero de 0 a 1023.
int leerPotenciometro() {
  // "long" es un entero grande para sumar sin desbordar.
  long suma = 0;

  // Se repite la lectura y se suman los resultados.
  for (int i = 0; i < MUESTRAS; i++) {
    suma = suma + analogRead(PIN_POT);
  }

  // Promedio = suma entre numero de lecturas.
  return suma / MUESTRAS;
}


// ---------------------------------------------------------------
// 4. FUNCION aplicarBrillo()
// ---------------------------------------------------------------
// Pinta toda la tira con el color base multiplicado por el brillo.
// Recibe un brillo de 0 (apagado) a 255.
void aplicarBrillo(int brillo) {
  // Cada componente de color se escala: color * brillo / 255.
  // Se calcula siempre a partir del color base para no perder
  // precision (no se usa setBrightness() de la libreria porque
  // acumularia errores al cambiarlo muchas veces).
  int rojo  = (long)COLOR_ROJO  * brillo / 255;
  int verde = (long)COLOR_VERDE * brillo / 255;
  int azul  = (long)COLOR_AZUL  * brillo / 255;

  // Color() junta rojo, verde y azul en un solo numero de color.
  uint32_t color = tira.Color(rojo, verde, azul);

  // Se asigna el mismo color a cada LED, del 0 al ultimo.
  for (int i = 0; i < NUM_LEDS; i++) {
    tira.setPixelColor(i, color);
  }

  // show() envia los datos a la tira: hasta este momento los LED
  // no cambian.
  tira.show();
}


// ---------------------------------------------------------------
// 5. FUNCION setup()
// ---------------------------------------------------------------
// setup() se ejecuta UNA sola vez al encender o reiniciar la placa.
void setup() {
  // Se prepara la tira para recibir datos.
  tira.begin();

  // Se apagan todos los LED y se envia la orden.
  tira.clear();
  tira.show();

  // Se abre el Monitor Serial a 9600 baudios.
  Serial.begin(9600);

  // Se espera 1,5 s para que la conexion USB quede establecida.
  delay(1500);

  // Mensajes iniciales.
  Serial.println("DIMMER CON STRIP LED - ARDUINO UNO R4 WIFI");
  Serial.println("------------------------------------------");
  Serial.println("Gire el potenciometro para cambiar el brillo.");
}


// ---------------------------------------------------------------
// 6. FUNCION loop()
// ---------------------------------------------------------------
// loop() se repite continuamente mientras la placa tenga energia.
void loop() {
  // Se guarda el tiempo actual en milisegundos.
  unsigned long ahora = millis();

  // Solo se lee si pasaron al menos 50 ms desde la ultima lectura.
  if (ahora - ultimaLectura >= INTERVALO_LECTURA) {
    // Se guarda el instante de esta lectura.
    ultimaLectura = ahora;

    // ---- PASO 1: leer el potenciometro (0 a 1023) ----
    int lectura = leerPotenciometro();

    // Si se pidio invertir el giro, se da vuelta la escala.
    if (INVERTIR_GIRO) {
      lectura = 1023 - lectura;
    }

    // ---- PASO 2: convertir la lectura en brillo ----
    // map() convierte proporcionalmente un numero de un rango a
    // otro. La lectura 0-1023 pasa a 0-BRILLO_MAXIMO.
    int brillo = map(lectura, 0, 1023, 0, BRILLO_MAXIMO);

    // El mismo valor expresado de 0 a 100 para mostrarlo en %.
    int porcentaje = map(lectura, 0, 1023, 0, 100);

    // ---- PASO 3: enviar el brillo a la tira solo si cambio ----
    // "!=" significa "distinto de". Enviar datos solo cuando hace
    // falta evita trabajo innecesario.
    if (brillo != brilloAnterior) {
      aplicarBrillo(brillo);

      // ---- PASO 4: informar por el Monitor Serial ----
      Serial.print("Lectura: ");
      Serial.print(lectura);
      Serial.print(" | Brillo (0-");
      Serial.print(BRILLO_MAXIMO);
      Serial.print("): ");
      Serial.print(brillo);
      Serial.print(" | Brillo: ");
      Serial.print(porcentaje);
      Serial.println(" %");

      // Se recuerda el brillo enviado.
      brilloAnterior = brillo;
    }
  }
}