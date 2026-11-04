/*
  ============================================================================
  EJERCICIO 9: SENSOR DE PARQUEO
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor ultrasonico HC-SR04 (VCC, Trig, Echo, Gnd)
                - Modulo buzzer PASIVO Keyestudio (marcas S, V, G)
                - Modulo semaforo Keyestudio Traffic Light (GND, G, Y, R)

  CONEXIONES:   HC-SR04  VCC -> 5V   Trig -> D9   Echo -> D10   Gnd -> GND
                Buzzer   S -> D6     V -> 5V      G -> GND
                Semaforo GND -> GND  G -> D3 (verde)
                                     Y -> D4 (amarillo)
                                     R -> D5 (rojo)

  QUE HACE:     Mide la distancia a un obstaculo (como el sensor trasero de
                un carro al estacionar) y la divide en tres zonas:
                    VERDE    : mas de 50 cm   (lejos)
                    AMARILLA : de 20 a 50 cm  (cerca)
                    ROJA     : menos de 20 cm (muy cerca)
                El buzzer pita cada vez mas rapido al acercarse. A 5 cm o
                menos el pitido es continuo. A mas de 100 cm no suena.

  EXTENSION:    La distancia que se usa es el PROMEDIO de las ultimas 5
                mediciones, para que las lecturas no salten.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. PINES Y CONSTANTES
// ============================================================================

// Pin de salida que dispara el sensor (Trig).
const int PIN_TRIG = 9;

// Pin de entrada que recibe el eco del sensor (Echo).
const int PIN_ECHO = 10;

// Pin de senal (S) del buzzer pasivo.
const int PIN_BUZZER = 6;

// Pines del modulo semaforo (un pin por color).
const int PIN_VERDE    = 3;
const int PIN_AMARILLO = 4;
const int PIN_ROJO     = 5;

// Limite entre zona verde y amarilla, en cm: mas lejos que esto es VERDE.
const float LIMITE_VERDE_CM = 50.0;

// Limite entre zona amarilla y roja, en cm: mas cerca que esto es ROJA.
const float LIMITE_ROJO_CM = 20.0;

// A mas distancia que esta (cm) el buzzer permanece en silencio.
const float DISTANCIA_ALERTA_CM = 100.0;

// A esta distancia o menos (cm) el pitido es continuo.
const float DISTANCIA_CONTINUO_CM = 5.0;

// Intervalo entre pitidos cuando el objeto esta a 100 cm: 1000 ms (lento).
const int INTERVALO_MAXIMO_MS = 1000;

// Intervalo entre pitidos cuando el objeto esta a 5 cm: 100 ms (rapido).
const int INTERVALO_MINIMO_MS = 100;

// Frecuencia del pitido: 1000 Hz.
const int FRECUENCIA_HZ = 1000;

// Duracion de cada pitido: 60 ms.
const unsigned int DURACION_PITIDO_MS = 60;

// Cada cuantos ms se hace una medicion: 60 ms (el sensor necesita una pausa
// entre disparos para no confundir un eco con el siguiente).
const unsigned long INTERVALO_MEDICION_MS = 60UL;

// Cada cuantos ms se escribe una linea en el Monitor Serial: 250 ms.
const unsigned long INTERVALO_MENSAJE_MS = 250UL;

// Cuantas mediciones se promedian (extension del ejercicio).
const int MUESTRAS = 5;

// Tiempo maximo de espera del eco: 30000 us = 30 ms.
const unsigned long TIEMPO_LIMITE_US = 30000UL;

// Velocidad del sonido en cm por microsegundo (aproximada a 20 C).
const float VELOCIDAD_CM_US = 0.0343;

// Rango valido del sensor, en cm. Fuera de el, la lectura se descarta.
const float DISTANCIA_MINIMA_CM = 2.0;
const float DISTANCIA_MAXIMA_CM = 400.0;

// Identificadores de zona (numeros enteros, no "enum", para evitar un
// problema conocido del Arduino IDE con las funciones propias).
const int ZONA_NINGUNA  = 0;
const int ZONA_VERDE    = 1;
const int ZONA_AMARILLA = 2;
const int ZONA_ROJA     = 3;

// ============================================================================
// 2. VARIABLES DE ESTADO
// ============================================================================

// Las ultimas mediciones validas (para calcular el promedio).
float lecturas[MUESTRAS];

// Posicion del arreglo donde se guardara la proxima medicion.
int indice = 0;

// Cuantas mediciones validas hay guardadas (de 0 a MUESTRAS).
int cantidad = 0;

// Cuantas mediciones invalidas seguidas (sin eco o fuera de rango).
int fallos = 0;

// Distancia promedio actual en cm. Vale -1 si no hay lectura valida.
float distancia = -1.0;

// Instantes (millis) de la ultima medicion, del ultimo mensaje y del ultimo pitido.
unsigned long ultimaMedicion = 0;
unsigned long ultimoMensaje = 0;
unsigned long ultimoPitido = 0;

// Indica si el buzzer esta sonando de forma continua.
bool sonandoContinuo = false;

// ============================================================================
// 3. FUNCIONES PROPIAS
// ============================================================================

// Dispara el sensor y devuelve la distancia en cm, o -1 si la lectura no es valida.
float medirDistanciaCm() {
  // Pulso de disparo: LOW 2 us, HIGH 10 us, LOW.
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // Mide cuanto dura el pulso HIGH del eco (en microsegundos).
  unsigned long duracionUs = pulseIn(PIN_ECHO, HIGH, TIEMPO_LIMITE_US);

  // Si no llego un pulso completo, no hay lectura valida.
  if (duracionUs == 0) {
    return -1.0;
  }

  // Convierte el tiempo de ida y vuelta a centimetros (se divide entre 2).
  float cm = duracionUs * VELOCIDAD_CM_US / 2.0;

  // Descarta lecturas fuera del rango del sensor.
  if (cm < DISTANCIA_MINIMA_CM || cm > DISTANCIA_MAXIMA_CM) {
    return -1.0;
  }
  return cm;
}

// Guarda una medicion valida en el arreglo circular.
void guardarLectura(float cm) {
  // Escribe en la posicion actual.
  lecturas[indice] = cm;
  // Avanza a la siguiente posicion; al llegar al final vuelve a la primera.
  indice = (indice + 1) % MUESTRAS;
  // Aumenta la cantidad de datos guardados hasta el maximo.
  if (cantidad < MUESTRAS) {
    cantidad++;
  }
}

// Devuelve el promedio de las mediciones guardadas, o -1 si no hay ninguna.
float calcularPromedio() {
  // Sin datos no hay promedio.
  if (cantidad == 0) {
    return -1.0;
  }
  // Suma los datos guardados y divide entre cuantos son.
  float suma = 0.0;
  for (int i = 0; i < cantidad; i++) {
    suma += lecturas[i];
  }
  return suma / cantidad;
}

// Decide la zona segun la distancia, con condicionales encadenadas.
int calcularZona(float cm) {
  // Sin lectura valida: ninguna zona.
  if (cm < 0) {
    return ZONA_NINGUNA;
  }
  // Mas lejos que el limite verde: zona verde.
  if (cm > LIMITE_VERDE_CM) {
    return ZONA_VERDE;
  }
  // Entre el limite rojo y el verde: zona amarilla.
  if (cm >= LIMITE_ROJO_CM) {
    return ZONA_AMARILLA;
  }
  // Mas cerca que el limite rojo: zona roja.
  return ZONA_ROJA;
}

// Devuelve el nombre de la zona para el Monitor Serial.
const char* nombreZona(int zona) {
  if (zona == ZONA_VERDE)    return "VERDE";
  if (zona == ZONA_AMARILLA) return "AMARILLA";
  if (zona == ZONA_ROJA)     return "ROJA";
  return "SIN LECTURA";
}

// Enciende el LED de la zona y apaga los otros dos.
void mostrarZona(int zona) {
  digitalWrite(PIN_VERDE,    zona == ZONA_VERDE    ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, zona == ZONA_AMARILLA ? HIGH : LOW);
  digitalWrite(PIN_ROJO,     zona == ZONA_ROJA     ? HIGH : LOW);
}

// Calcula cada cuantos ms debe sonar un pitido segun la distancia (5 a 100 cm).
int calcularIntervalo(float cm) {
  // Limita la distancia al rango de trabajo.
  float limitada = constrain(cm, DISTANCIA_CONTINUO_CM, DISTANCIA_ALERTA_CM);
  // Convierte la distancia (en decimas de cm, para no perder decimales) a intervalo:
  // cerca = intervalo corto (pitidos rapidos), lejos = intervalo largo.
  return map((long)(limitada * 10), (long)(DISTANCIA_CONTINUO_CM * 10),
             (long)(DISTANCIA_ALERTA_CM * 10),
             INTERVALO_MINIMO_MS, INTERVALO_MAXIMO_MS);
}

// ============================================================================
// 4. setup(): SE EJECUTA UNA SOLA VEZ AL ENCENDER
// ============================================================================
void setup() {
  // Abre la comunicacion con el Monitor Serial a 9600 baudios.
  Serial.begin(9600);
  // Pausa breve para que el Monitor Serial alcance a conectarse.
  delay(1500);

  // Trig es salida; Echo es entrada.
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  // El buzzer y los tres LED son salidas.
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);

  // Estado inicial: Trig en reposo, buzzer en silencio y semaforo apagado.
  digitalWrite(PIN_TRIG, LOW);
  noTone(PIN_BUZZER);
  mostrarZona(ZONA_NINGUNA);

  // Mensajes de bienvenida.
  Serial.println("SENSOR DE PARQUEO - ARDUINO UNO R4 WIFI");
  Serial.println("---------------------------------------");
  Serial.println("Acerque un objeto plano al sensor.");
}

// ============================================================================
// 5. loop(): SE REPITE CONTINUAMENTE
// ============================================================================
void loop() {
  // Lee el reloj una sola vez por vuelta.
  unsigned long ahora = millis();

  // ---------------------------------------------------------------------
  // FASE 1: MEDICION (cada 60 ms)
  // ---------------------------------------------------------------------
  if (ahora - ultimaMedicion >= INTERVALO_MEDICION_MS) {
    // Anota el instante de esta medicion.
    ultimaMedicion = ahora;

    // Mide la distancia (devuelve -1 si la lectura no es valida).
    float cm = medirDistanciaCm();

    if (cm >= 0) {
      // Lectura valida: se guarda y se reinicia el contador de fallos.
      guardarLectura(cm);
      fallos = 0;
    } else {
      // Lectura invalida: se cuenta el fallo.
      fallos++;
      // Con 5 fallos seguidos se descartan los datos viejos: ya no hay objeto.
      if (fallos >= MUESTRAS) {
        cantidad = 0;
        indice = 0;
        fallos = MUESTRAS;
      }
    }

    // Distancia que se usa: promedio de las ultimas mediciones validas.
    distancia = calcularPromedio();
    // Actualiza el semaforo segun la zona.
    mostrarZona(calcularZona(distancia));
  }

  // ---------------------------------------------------------------------
  // FASE 2: BUZZER
  // ---------------------------------------------------------------------
  if (distancia >= 0 && distancia <= DISTANCIA_CONTINUO_CM) {
    // Muy cerca: tono continuo.
    tone(PIN_BUZZER, FRECUENCIA_HZ);
    sonandoContinuo = true;
  } else {
    // Si venia sonando continuo y ya no corresponde, se silencia.
    if (sonandoContinuo) {
      noTone(PIN_BUZZER);
      sonandoContinuo = false;
    }
    // Entre 5 y 100 cm: pitidos separados por un intervalo que depende de la distancia.
    if (distancia > DISTANCIA_CONTINUO_CM && distancia <= DISTANCIA_ALERTA_CM) {
      if (ahora - ultimoPitido >= (unsigned long)calcularIntervalo(distancia)) {
        // Anota el instante del pitido.
        ultimoPitido = ahora;
        // Pitido corto: 1000 Hz durante 60 ms (tone con duracion no bloquea).
        tone(PIN_BUZZER, FRECUENCIA_HZ, DURACION_PITIDO_MS);
      }
    }
  }

  // ---------------------------------------------------------------------
  // FASE 3: MENSAJE POR EL MONITOR SERIAL (cada 250 ms)
  // ---------------------------------------------------------------------
  if (ahora - ultimoMensaje >= INTERVALO_MENSAJE_MS) {
    // Anota el instante del mensaje.
    ultimoMensaje = ahora;
    if (distancia < 0) {
      // No hay lectura valida.
      Serial.println("Distancia: sin eco valido | Zona: SIN LECTURA");
    } else {
      // Distancia promedio con un decimal.
      Serial.print("Distancia: ");
      Serial.print(distancia, 1);
      Serial.print(" cm | Zona: ");
      Serial.print(nombreZona(calcularZona(distancia)));
      // Informa cada cuanto suena el buzzer.
      Serial.print(" | Buzzer: ");
      if (distancia <= DISTANCIA_CONTINUO_CM) {
        Serial.println("continuo");
      } else if (distancia <= DISTANCIA_ALERTA_CM) {
        Serial.print("cada ");
        Serial.print(calcularIntervalo(distancia));
        Serial.println(" ms");
      } else {
        Serial.println("silencio");
      }
    }
  }
}