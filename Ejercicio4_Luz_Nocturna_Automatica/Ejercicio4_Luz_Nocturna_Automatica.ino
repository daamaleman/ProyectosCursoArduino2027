/*
  ====================================================================
  EJERCICIO 4: LUZ NOCTURNA AUTOMATICA
  ====================================================================
  Placa:       Arduino UNO R4 WiFi
  Componentes: Modulo fotorresistencia Keyestudio (pines S, V, G)
               LED externo (por ejemplo amarillo o blanco de 5 mm)
               Resistencia de 1 kOhm (1000 ohmios) en serie con el LED
  Libreria:    Ninguna. Solo se usan funciones incluidas en Arduino.

  CONEXIONES
    Fotorresistencia:  S -> A0     V -> 5V     G -> GND
    LED externo:       D7 -> resistencia 1 kOhm -> anodo (+) del LED
                       catodo (-) del LED -> GND

  QUE HACE EL PROGRAMA
    Mide la luz ambiente con analogRead() y enciende el LED cuando
    la luz baja de un umbral (se hace de noche o se tapa el sensor).
    Cuando la luz vuelve a subir, el LED se apaga.

  COMO SE USA (CALIBRACION)
    1. Deje MODO_CALIBRACION = true y suba el programa.
    2. Abra el Monitor Serial a 9600 baudios.
    3. Observe las lecturas con LUZ (por ejemplo la luz del salon) y
       con SOMBRA (tape el sensor con la mano). Anote ambos valores.
    4. Si al tapar el sensor el numero SUBE, cambie
       SENSOR_INVERTIDO a true.
    5. Calcule un umbral entre ambos valores (el programa sugiere
       uno) y escribalo en UMBRAL_OSCURO.
    6. Cambie MODO_CALIBRACION a false y suba el programa de nuevo.
  ====================================================================
*/


// ---------------------------------------------------------------
// 1. PINES Y CONSTANTES
// ---------------------------------------------------------------

// Pin analogico A0: recibe la senal del modulo fotorresistencia.
// Un pin analogico puede leer muchos niveles de voltaje, no solo
// HIGH o LOW.
const int PIN_FOTO = A0;

// Pin digital D7: entrega la senal al LED (a traves de la resistencia).
// OJO: no se llama PIN_LED porque la placa UNO R4 WiFi ya define ese
// nombre y el programa no compilaria.
const int PIN_LED_EXTERNO = 7;

// true  = modo calibracion: el LED permanece apagado y el Monitor
//         Serial muestra las lecturas, el minimo y el maximo.
// false = modo normal: el LED responde a la luz.
const bool MODO_CALIBRACION = true;

// Sentido del sensor.
//   false = la lectura SUBE cuando hay MAS luz.
//   true  = la lectura SUBE cuando hay MENOS luz (sensor invertido).
// Se descubre en la calibracion tapando el sensor con la mano.
const bool SENSOR_INVERTIDO = false;

// Umbral de oscuridad, en unidades de luz (0 a 1023). Si la luz
// baja de este valor, se considera de noche. El valor 300 es solo
// un ejemplo: debe calibrarse con SU sensor y SU salon.
const int UMBRAL_OSCURO = 300;

// Margen de histeresis. El LED se enciende cuando la luz baja de
// UMBRAL_OSCURO, pero solo se apaga cuando la luz supera
// UMBRAL_OSCURO + HISTERESIS. Evita que el LED parpadee cuando la
// luz esta justo en el umbral.
const int HISTERESIS = 40;

// Cada cuantos milisegundos se mide la luz (100 ms = 10 veces por
// segundo).
const unsigned long INTERVALO_MEDICION = 100;

// Cada cuantos milisegundos se informa por el Monitor Serial.
// En calibracion se informa mas rapido (500 ms) que en modo normal
// (2000 ms). "? :" elige el primer valor si la condicion es
// verdadera y el segundo si es falsa.
const unsigned long INTERVALO_INFORME = MODO_CALIBRACION ? 500 : 2000;

// Cuantas lecturas se promedian en cada medicion. Promediar reduce
// el ruido de una lectura aislada.
const int MUESTRAS = 8;


// ---------------------------------------------------------------
// 2. VARIABLES DE ESTADO
// ---------------------------------------------------------------

// Estado del LED: true = encendido, false = apagado.
bool ledEncendido = false;

// Ultimo valor de luz medido (0 = oscuro, 1023 = mucha luz).
int luzActual = 0;

// Valores minimo y maximo de luz vistos desde el arranque. Sirven
// para calibrar. Se inician "al reves" para que la primera lectura
// los reemplace.
int luzMinima = 1023;
int luzMaxima = 0;

// Instantes (en ms) de la ultima medicion y del ultimo informe.
unsigned long ultimaMedicion = 0;
unsigned long ultimoInforme = 0;


// ---------------------------------------------------------------
// 3. FUNCION leerLuz()
// ---------------------------------------------------------------
// Lee el sensor varias veces, calcula el promedio y devuelve la
// luz en una escala donde un numero MAYOR significa MAS luz.
int leerLuz() {
  // "long" es un entero grande; se usa para sumar sin desbordar.
  long suma = 0;

  // Repite la lectura MUESTRAS veces y las va sumando.
  // analogRead() devuelve un numero de 0 (0 V) a 1023 (5 V).
  for (int i = 0; i < MUESTRAS; i++) {
    suma = suma + analogRead(PIN_FOTO);
  }

  // Promedio = suma dividida entre el numero de lecturas.
  int lectura = suma / MUESTRAS;

  // Si el sensor esta invertido, se da vuelta la escala:
  // 1023 - lectura convierte "sube en la sombra" en "sube con luz".
  if (SENSOR_INVERTIDO) {
    return 1023 - lectura;
  }
  return lectura;
}


// ---------------------------------------------------------------
// 4. FUNCION ponerLed()
// ---------------------------------------------------------------
// Enciende o apaga el LED y guarda su estado en la variable.
void ponerLed(bool encender) {
  // Se guarda el nuevo estado.
  ledEncendido = encender;

  // HIGH = 5 V = LED encendido; LOW = 0 V = LED apagado.
  if (encender) {
    digitalWrite(PIN_LED_EXTERNO, HIGH);
  } else {
    digitalWrite(PIN_LED_EXTERNO, LOW);
  }
}


// ---------------------------------------------------------------
// 5. FUNCION informar()
// ---------------------------------------------------------------
// Imprime el estado actual por el Monitor Serial.
void informar() {
  // Se muestra la luz medida.
  Serial.print("Luz: ");
  Serial.print(luzActual);

  if (MODO_CALIBRACION) {
    // En calibracion se muestran el minimo, el maximo y un umbral
    // sugerido: el punto medio entre ambos.
    Serial.print(" | Minimo: ");
    Serial.print(luzMinima);
    Serial.print(" | Maximo: ");
    Serial.print(luzMaxima);
    Serial.print(" | Umbral sugerido: ");
    Serial.println((luzMinima + luzMaxima) / 2);
  } else {
    // En modo normal se muestran el umbral y el estado del LED.
    Serial.print(" | Umbral: ");
    Serial.print(UMBRAL_OSCURO);
    if (ledEncendido) {
      Serial.println(" | LED: ENCENDIDO");
    } else {
      Serial.println(" | LED: APAGADO");
    }
  }
}


// ---------------------------------------------------------------
// 6. FUNCION setup()
// ---------------------------------------------------------------
// setup() se ejecuta UNA sola vez al encender o reiniciar la placa.
void setup() {
  // El LED recibe la senal de la placa: es una salida.
  // (El pin analogico A0 es entrada por defecto.)
  pinMode(PIN_LED_EXTERNO, OUTPUT);

  // El LED inicia apagado.
  ponerLed(false);

  // Se abre el Monitor Serial a 9600 baudios.
  Serial.begin(9600);

  // Se espera 1,5 s para que la conexion USB quede establecida.
  delay(1500);

  // Mensajes iniciales.
  Serial.println("LUZ NOCTURNA AUTOMATICA - ARDUINO UNO R4 WIFI");
  Serial.println("---------------------------------------------");
  if (MODO_CALIBRACION) {
    Serial.println("MODO CALIBRACION: el LED no se enciende.");
    Serial.println("Anote la luz con LUZ y con SOMBRA (tape el sensor).");
  } else {
    Serial.println("MODO NORMAL: el LED se enciende en la oscuridad.");
  }
}


// ---------------------------------------------------------------
// 7. FUNCION loop()
// ---------------------------------------------------------------
// loop() se repite continuamente mientras la placa tenga energia.
void loop() {
  // Se guarda el tiempo actual en milisegundos.
  unsigned long ahora = millis();

  // ---- PASO 1: medir la luz cada 100 ms ----
  if (ahora - ultimaMedicion >= INTERVALO_MEDICION) {
    // Se guarda el instante de esta medicion.
    ultimaMedicion = ahora;

    // Se mide la luz (promedio de varias lecturas).
    luzActual = leerLuz();

    // Se actualizan el minimo y el maximo para la calibracion.
    if (luzActual < luzMinima) {
      luzMinima = luzActual;
    }
    if (luzActual > luzMaxima) {
      luzMaxima = luzActual;
    }

    // ---- PASO 2: decidir si se enciende o se apaga el LED ----
    // En calibracion el LED no actua.
    if (!MODO_CALIBRACION) {
      // Si el LED esta apagado y la luz bajo del umbral: de noche.
      if (!ledEncendido && luzActual < UMBRAL_OSCURO) {
        ponerLed(true);
        Serial.println(">>> Oscuro: LED ENCENDIDO");
      }
      // Si el LED esta encendido y la luz supero el umbral MAS el
      // margen de histeresis: ya es de dia.
      else if (ledEncendido && luzActual > UMBRAL_OSCURO + HISTERESIS) {
        ponerLed(false);
        Serial.println(">>> Hay luz: LED APAGADO");
      }
    }
  }

  // ---- PASO 3: informar por el Monitor Serial ----
  if (ahora - ultimoInforme >= INTERVALO_INFORME) {
    ultimoInforme = ahora;
    informar();
  }
}