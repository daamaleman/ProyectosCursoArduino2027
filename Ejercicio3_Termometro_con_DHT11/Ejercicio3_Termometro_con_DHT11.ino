/*
  ====================================================================
  EJERCICIO 3: TERMOMETRO DE CONFORT CON DHT11 Y LED RGB
  ====================================================================
  Placa:       Arduino UNO R4 WiFi
  Componentes: Sensor DHT11 (modulo de 3 pines: + / OUT / -)
               Modulo LED RGB Keyestudio (pines G, R, B, V)
  Librerias:   DHT sensor library (Adafruit)
               Adafruit Unified Sensor (Adafruit, dependencia)

  CONEXIONES
    DHT11:       +   -> 5V      OUT -> D2      -   -> GND
    Modulo RGB:  R   -> D9      G   -> D10     B   -> D11
                 V   -> 5V  (ver nota sobre COMUN_ANODO mas abajo)

  QUE HACE EL PROGRAMA
    Mide la temperatura (grados Celsius) y la humedad relativa (%)
    cada 2 segundos y enciende el LED RGB segun la temperatura:
        AZUL   -> hace FRIO     (menos de 20 C)
        VERDE  -> CONFORT       (de 20 C a 26 C)
        ROJO   -> hace CALOR    (mas de 26 C)
    Los valores y la zona se muestran en el Monitor Serial.
  ====================================================================
*/

// Se incluye la libreria del sensor DHT. Proporciona la clase DHT y
// las funciones readTemperature() y readHumidity().
#include <DHT.h>


// ---------------------------------------------------------------
// 1. PINES Y CONSTANTES
// ---------------------------------------------------------------

// Pin digital D2: recibe los datos del terminal OUT del DHT11.
const int PIN_DHT = 2;

// Pines digitales que controlan cada color del modulo RGB.
// (Se evita el nombre PIN_LED porque la placa ya lo usa.)
const int PIN_ROJO  = 9;
const int PIN_VERDE = 10;
const int PIN_AZUL  = 11;

// Polaridad del modulo RGB.
//   true  = comun ANODO: el pin V va a 5V y cada color se ENCIENDE
//           con LOW (0 V) y se APAGA con HIGH (5 V).
//   false = comun CATODO: el pin comun va a GND y cada color se
//           ENCIENDE con HIGH y se APAGA con LOW.
// Si en la prueba de arranque los colores salen al reves (todo
// encendido menos el color pedido), cambie este valor.
const bool COMUN_ANODO = true;

// Niveles que se escriben en el pin segun la polaridad elegida.
// El operador "? :" elige el primer valor si la condicion es
// verdadera y el segundo si es falsa.
const int NIVEL_ENCENDIDO = COMUN_ANODO ? LOW : HIGH;
const int NIVEL_APAGADO   = COMUN_ANODO ? HIGH : LOW;

// Umbrales de temperatura en grados Celsius. "float" es un numero
// con decimales.
// Menos de TEMP_FRIA              -> zona FRIA
// Entre TEMP_FRIA y TEMP_CALOR    -> zona CONFORT
// Mas de TEMP_CALOR               -> zona CALOR
const float TEMP_FRIA  = 20.0;
const float TEMP_CALOR = 26.0;

// Tiempo entre lecturas en milisegundos (2000 ms = 2 s).
// El DHT11 no debe leerse mas rapido que una vez por segundo.
const unsigned long INTERVALO_LECTURA = 2000;

// Identificadores de zona. Se usan numeros enteros con nombre para
// que el codigo sea facil de leer.
const int ZONA_FRIA    = 0;
const int ZONA_CONFORT = 1;
const int ZONA_CALOR   = 2;


// ---------------------------------------------------------------
// 2. OBJETO SENSOR Y VARIABLES
// ---------------------------------------------------------------

// Se crea el objeto "sensor": se indica el pin de datos y el tipo
// de sensor (DHT11).
DHT sensor(PIN_DHT, DHT11);

// Instante (en ms) de la ultima lectura realizada.
unsigned long ultimaLectura = 0;


// ---------------------------------------------------------------
// 3. FUNCION ponerColor()
// ---------------------------------------------------------------
// Enciende o apaga cada color del LED RGB. Recibe tres valores
// "bool" (true = encendido, false = apagado), uno por color.
void ponerColor(bool rojo, bool verde, bool azul) {
  // Si "rojo" es true se escribe el nivel de encendido; si no, el
  // de apagado. Lo mismo para verde y azul.
  digitalWrite(PIN_ROJO,  rojo  ? NIVEL_ENCENDIDO : NIVEL_APAGADO);
  digitalWrite(PIN_VERDE, verde ? NIVEL_ENCENDIDO : NIVEL_APAGADO);
  digitalWrite(PIN_AZUL,  azul  ? NIVEL_ENCENDIDO : NIVEL_APAGADO);
}


// ---------------------------------------------------------------
// 4. FUNCION clasificarZona()
// ---------------------------------------------------------------
// Recibe la temperatura y devuelve la zona (ZONA_FRIA, ZONA_CONFORT
// o ZONA_CALOR). "int" indica que devuelve un numero entero.
int clasificarZona(float temperatura) {
  // Menos de 20 C: hace frio.
  if (temperatura < TEMP_FRIA) {
    return ZONA_FRIA;
  }

  // Mas de 26 C: hace calor.
  if (temperatura > TEMP_CALOR) {
    return ZONA_CALOR;
  }

  // Si no es ni frio ni calor, esta en confort.
  return ZONA_CONFORT;
}


// ---------------------------------------------------------------
// 5. FUNCION mostrarZona()
// ---------------------------------------------------------------
// Enciende el color del LED RGB que corresponde a la zona.
void mostrarZona(int zona) {
  if (zona == ZONA_FRIA) {
    // Solo azul encendido.
    ponerColor(false, false, true);
  } else if (zona == ZONA_CONFORT) {
    // Solo verde encendido.
    ponerColor(false, true, false);
  } else {
    // Solo rojo encendido (zona de calor).
    ponerColor(true, false, false);
  }
}


// ---------------------------------------------------------------
// 6. FUNCION nombreZona()
// ---------------------------------------------------------------
// Devuelve el texto de la zona para mostrarlo en el Monitor Serial.
// "const char*" es un texto fijo.
const char* nombreZona(int zona) {
  if (zona == ZONA_FRIA) {
    return "FRIO (azul)";
  } else if (zona == ZONA_CONFORT) {
    return "CONFORT (verde)";
  }
  return "CALOR (rojo)";
}


// ---------------------------------------------------------------
// 7. FUNCION setup()
// ---------------------------------------------------------------
// setup() se ejecuta UNA sola vez al encender o reiniciar la placa.
void setup() {
  // Los tres pines del LED RGB envian senal: son salidas.
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AZUL, OUTPUT);

  // Se apagan los tres colores al inicio.
  ponerColor(false, false, false);

  // Se abre el Monitor Serial a 9600 baudios.
  Serial.begin(9600);

  // Se espera 1,5 s para que la conexion USB quede establecida.
  delay(1500);

  // Se inicia el sensor DHT11.
  sensor.begin();

  // Mensajes iniciales con los umbrales configurados.
  Serial.println("TERMOMETRO DE CONFORT - ARDUINO UNO R4 WIFI");
  Serial.println("-------------------------------------------");
  Serial.print("Azul  : menos de ");
  Serial.print(TEMP_FRIA, 1);
  Serial.println(" C");
  Serial.print("Verde : de ");
  Serial.print(TEMP_FRIA, 1);
  Serial.print(" C a ");
  Serial.print(TEMP_CALOR, 1);
  Serial.println(" C");
  Serial.print("Rojo  : mas de ");
  Serial.print(TEMP_CALOR, 1);
  Serial.println(" C");

  // Prueba de arranque: cada color se enciende 1 segundo para
  // comprobar que el LED RGB y su polaridad son correctos.
  // Orden esperado: ROJO, VERDE, AZUL.
  Serial.println("Prueba del LED RGB: rojo, verde, azul...");
  ponerColor(true, false, false);
  delay(1000);
  ponerColor(false, true, false);
  delay(1000);
  ponerColor(false, false, true);
  delay(1000);
  ponerColor(false, false, false);
  Serial.println("Prueba terminada. Iniciando mediciones.");
}


// ---------------------------------------------------------------
// 8. FUNCION loop()
// ---------------------------------------------------------------
// loop() se repite continuamente mientras la placa tenga energia.
void loop() {
  // Se guarda el tiempo actual en milisegundos.
  unsigned long ahora = millis();

  // Solo se mide si pasaron al menos 2 s desde la ultima lectura.
  // Asi no se usa delay() y el programa no queda detenido.
  if (ahora - ultimaLectura >= INTERVALO_LECTURA) {
    // Se guarda el instante de esta lectura.
    ultimaLectura = ahora;

    // Se leen la temperatura (en C) y la humedad (en %).
    float temperatura = sensor.readTemperature();
    float humedad = sensor.readHumidity();

    // isnan() ("is not a number") es verdadero si la lectura fallo.
    // "||" significa "o".
    if (isnan(temperatura) || isnan(humedad)) {
      // Se apaga el LED y se avisa del error. Revise el cableado.
      ponerColor(false, false, false);
      Serial.println("Error: no se pudo leer el DHT11. Revise el cableado.");
      return;
    }

    // Se determina la zona segun la temperatura y se enciende el
    // color correspondiente.
    int zona = clasificarZona(temperatura);
    mostrarZona(zona);

    // Se muestran los valores con sus unidades.
    // El segundo argumento (1) indica un decimal.
    Serial.print("Temperatura: ");
    Serial.print(temperatura, 1);
    Serial.print(" C | Humedad: ");
    Serial.print(humedad, 1);
    Serial.print(" % | Zona: ");
    Serial.println(nombreZona(zona));
  }
}