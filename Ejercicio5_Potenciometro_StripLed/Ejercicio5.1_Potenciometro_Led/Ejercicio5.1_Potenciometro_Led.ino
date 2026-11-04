/*
  ====================================================================
  EJERCICIO 5: DIMMER CON POTENCIOMETRO
  ====================================================================
  Placa:       Arduino UNO R4 WiFi
  Componentes: Modulo potenciometro Keyestudio (VR Sensor, pines S, V, G)
               LED externo (por ejemplo rojo o blanco de 5 mm)
               Resistencia de 1 kOhm (1000 ohmios) en serie con el LED
  Libreria:    Ninguna. Solo se usan funciones incluidas en Arduino.

  CONEXIONES
    Potenciometro:  S -> A0     V -> 5V     G -> GND
    LED externo:    D9 -> resistencia 1 kOhm -> anodo (+) del LED
                    catodo (-) del LED -> GND
    (D9 es un pin con PWM, marcado con "~" en la placa)

  QUE HACE EL PROGRAMA
    Al girar el potenciometro cambia el brillo del LED, desde apagado
    (0 %) hasta el maximo (100 %). El Monitor Serial muestra la
    lectura del potenciometro, el brillo y el porcentaje.
  ====================================================================
*/


// ---------------------------------------------------------------
// 1. PINES Y CONSTANTES
// ---------------------------------------------------------------

// Pin analogico A0: recibe la senal del potenciometro.
const int PIN_POT = A0;

// Pin digital D9: entrega la senal PWM al LED (a traves de la
// resistencia). Solo algunos pines pueden generar PWM: en la UNO R4
// WiFi son D3, D5, D6, D9, D10 y D11.
// OJO: no se llama PIN_LED porque la placa ya define ese nombre.
const int PIN_LED_EXTERNO = 9;

// Cada cuantos milisegundos se lee el potenciometro (50 ms = 20
// veces por segundo).
const unsigned long INTERVALO_LECTURA = 50;

// Cuantas lecturas se promedian. Reduce el temblor de la lectura.
const int MUESTRAS = 8;

// Si se invierte, al girar hacia el otro lado el LED brilla mas.
// false = el brillo sube al girar hacia el lado que sube la lectura.
// true  = el sentido se invierte.
const bool INVERTIR_GIRO = false;


// ---------------------------------------------------------------
// 2. VARIABLES DE ESTADO
// ---------------------------------------------------------------

// Instante (en ms) de la ultima lectura.
unsigned long ultimaLectura = 0;

// Ultimo porcentaje mostrado en el Monitor Serial. Se inicia en -1
// (un valor imposible) para que la primera lectura se muestre.
int porcentajeAnterior = -1;


// ---------------------------------------------------------------
// 3. FUNCION leerPotenciometro()
// ---------------------------------------------------------------
// Lee el potenciometro varias veces y devuelve el promedio, un
// numero de 0 a 1023.
int leerPotenciometro() {
  // "long" es un entero grande para sumar sin desbordar.
  long suma = 0;

  // Se repite la lectura y se suman los resultados.
  // analogRead() devuelve 0 (0 V) a 1023 (5 V).
  for (int i = 0; i < MUESTRAS; i++) {
    suma = suma + analogRead(PIN_POT);
  }

  // Promedio = suma entre numero de lecturas.
  return suma / MUESTRAS;
}


// ---------------------------------------------------------------
// 4. FUNCION setup()
// ---------------------------------------------------------------
// setup() se ejecuta UNA sola vez al encender o reiniciar la placa.
void setup() {
  // El LED recibe la senal de la placa: es una salida.
  // (A0 es entrada por defecto, no hace falta configurarlo.)
  pinMode(PIN_LED_EXTERNO, OUTPUT);

  // analogWrite(pin, 0) mantiene el pin siempre en 0 V: LED apagado.
  analogWrite(PIN_LED_EXTERNO, 0);

  // Se abre el Monitor Serial a 9600 baudios.
  Serial.begin(9600);

  // Se espera 1,5 s para que la conexion USB quede establecida.
  delay(1500);

  // Mensajes iniciales.
  Serial.println("DIMMER CON POTENCIOMETRO - ARDUINO UNO R4 WIFI");
  Serial.println("----------------------------------------------");
  Serial.println("Gire el potenciometro para cambiar el brillo.");
}


// ---------------------------------------------------------------
// 5. FUNCION loop()
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
    // map(valor, minEntrada, maxEntrada, minSalida, maxSalida)
    // convierte proporcionalmente un numero de un rango a otro.
    // analogWrite() acepta de 0 (apagado) a 255 (brillo maximo).
    int brillo = map(lectura, 0, 1023, 0, 255);

    // El mismo valor expresado de 0 a 100 para mostrarlo en %.
    int porcentaje = map(lectura, 0, 1023, 0, 100);

    // ---- PASO 3: aplicar el brillo al LED ----
    // analogWrite() genera una senal PWM: el pin se enciende y se
    // apaga muy rapido y el brillo depende del tiempo encendido.
    analogWrite(PIN_LED_EXTERNO, brillo);

    // ---- PASO 4: informar solo cuando el porcentaje cambia ----
    // "!=" significa "distinto de". Asi el Monitor Serial no se
    // llena de mensajes repetidos.
    if (porcentaje != porcentajeAnterior) {
      Serial.print("Lectura: ");
      Serial.print(lectura);
      Serial.print(" | Brillo (0-255): ");
      Serial.print(brillo);
      Serial.print(" | Brillo: ");
      Serial.print(porcentaje);
      Serial.println(" %");

      // Se recuerda el porcentaje mostrado.
      porcentajeAnterior = porcentaje;
    }
  }
}