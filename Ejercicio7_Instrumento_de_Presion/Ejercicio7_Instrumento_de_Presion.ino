/*
  ============================================================================
  EJERCICIO 7: INSTRUMENTO DE PRESION
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor de presion FSR Keyestudio (marcas S, V, G)
                - Modulo buzzer PASIVO Keyestudio (marcas S, V, G)
                - Modulo semaforo Keyestudio Traffic Light (GND, G, Y, R)

  CONEXIONES:   FSR      S -> A0     V -> 5V     G -> GND
                Buzzer   S -> D9     V -> 5V     G -> GND
                Semaforo GND -> GND  G -> D3  (verde)
                                     Y -> D4  (amarillo)
                                     R -> D5  (rojo)

  QUE HACE:     - Sin presion (lectura menor o igual al umbral): el buzzer
                  NO suena y el semaforo queda apagado.
                - Al presionar el FSR, el buzzer suena y su tono SUBE con
                  la fuerza: de 200 Hz (presion minima) a 2000 Hz (maxima).
                - Tres niveles de presion con el semaforo:
                    leve   -> LED verde
                    media  -> LED amarillo
                    fuerte -> LED rojo
                - El Monitor Serial muestra lectura, porcentaje, frecuencia
                  y nivel.

  IMPORTANTE:   Se necesita un buzzer PASIVO: el tono lo decide el Arduino
                con tone(). Un buzzer ACTIVO produce siempre el mismo tono.
                Los valores del sensor son una escala relativa, NO newtons.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. PINES Y CONSTANTES
// ============================================================================

// Pin analogico A0: recibe la senal (S) del sensor de presion.
const int PIN_FSR = A0;

// Pin digital D9: envia la senal (S) al buzzer pasivo.
const int PIN_BUZZER = 9;

// Pines del modulo semaforo (un pin por color).
const int PIN_VERDE    = 3;
const int PIN_AMARILLO = 4;
const int PIN_ROJO     = 5;

// Umbral minimo: lecturas menores o iguales se consideran "sin presion".
// Evita que el ruido del sensor haga sonar el buzzer en reposo.
const int UMBRAL_PRESION = 20;

// Lectura a partir de la cual se alcanza la presion maxima de la escala.
// No es el maximo fisico del sensor, es el tope de esta practica.
const int PRESION_MAXIMA = 800;

// Frecuencia (tono) del buzzer con la presion minima: 200 Hz (grave).
const int FRECUENCIA_MINIMA = 200;

// Frecuencia (tono) del buzzer con la presion maxima: 2000 Hz (agudo).
const int FRECUENCIA_MAXIMA = 2000;

// Limites de los niveles, como porcentaje de la escala (0 a 100 %).
// Hasta 33 % es leve, hasta 66 % es media y por encima es fuerte.
const int LIMITE_LEVE  = 33;
const int LIMITE_MEDIA = 66;

// Identificadores de nivel (numeros enteros, no "enum", para evitar un
// problema conocido del Arduino IDE con las funciones propias).
const int NIVEL_NINGUNO = 0;
const int NIVEL_LEVE    = 1;
const int NIVEL_MEDIA   = 2;
const int NIVEL_FUERTE  = 3;

// Cada cuantos ms se lee el sensor: 50 ms (20 lecturas por segundo).
const unsigned long INTERVALO_LECTURA_MS = 50UL;

// Cada cuantos ms se escribe una linea en el Monitor Serial: 250 ms.
const unsigned long INTERVALO_MENSAJE_MS = 250UL;

// Cuantas lecturas se promedian en cada medicion (reduce el ruido).
const int MUESTRAS = 4;

// ============================================================================
// 2. VARIABLES DE ESTADO
// ============================================================================

// Instante (millis) de la ultima lectura del sensor.
unsigned long ultimaLectura = 0;

// Instante (millis) del ultimo mensaje enviado al Monitor Serial.
unsigned long ultimoMensaje = 0;

// ============================================================================
// 3. FUNCIONES PROPIAS
// ============================================================================

// Lee el sensor varias veces y devuelve el promedio (valor de 0 a 1023).
int leerPresion() {
  // Acumulador de las lecturas. "long" evita desbordar la suma.
  long suma = 0;
  // Repite la lectura MUESTRAS veces y las suma.
  for (int i = 0; i < MUESTRAS; i++) {
    suma += analogRead(PIN_FSR);
  }
  // Devuelve el promedio.
  return (int)(suma / MUESTRAS);
}

// Decide el nivel de presion segun el porcentaje (0 a 100).
int calcularNivel(int porcentaje) {
  // Hasta el limite leve: presion leve.
  if (porcentaje <= LIMITE_LEVE) {
    return NIVEL_LEVE;
  }
  // Hasta el limite medio: presion media.
  if (porcentaje <= LIMITE_MEDIA) {
    return NIVEL_MEDIA;
  }
  // Por encima: presion fuerte.
  return NIVEL_FUERTE;
}

// Devuelve el nombre del nivel para mostrarlo en el Monitor Serial.
const char* nombreNivel(int nivel) {
  if (nivel == NIVEL_LEVE)   return "leve";
  if (nivel == NIVEL_MEDIA)  return "media";
  if (nivel == NIVEL_FUERTE) return "fuerte";
  return "ninguna";
}

// Enciende el LED del nivel indicado y apaga los otros dos.
void mostrarNivel(int nivel) {
  // Cada LED se enciende solo si el nivel coincide con su color.
  digitalWrite(PIN_VERDE,    nivel == NIVEL_LEVE   ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, nivel == NIVEL_MEDIA  ? HIGH : LOW);
  digitalWrite(PIN_ROJO,     nivel == NIVEL_FUERTE ? HIGH : LOW);
}

// ============================================================================
// 4. setup(): SE EJECUTA UNA SOLA VEZ AL ENCENDER
// ============================================================================
void setup() {
  // Abre la comunicacion con el Monitor Serial a 9600 baudios.
  Serial.begin(9600);
  // Pausa breve para que el Monitor Serial alcance a conectarse.
  delay(1500);

  // Lectura analogica de 10 bits: valores enteros de 0 a 1023.
  analogReadResolution(10);

  // Los pines del buzzer y de los tres LED son salidas.
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);

  // Estado inicial: buzzer en silencio y semaforo apagado.
  noTone(PIN_BUZZER);
  mostrarNivel(NIVEL_NINGUNO);

  // Mensajes de bienvenida.
  Serial.println("INSTRUMENTO DE PRESION - ARDUINO UNO R4 WIFI");
  Serial.println("--------------------------------------------");
  Serial.println("Presione el sensor: el tono sube con la fuerza.");
}

// ============================================================================
// 5. loop(): SE REPITE CONTINUAMENTE
// ============================================================================
void loop() {
  // Lee el reloj una sola vez por vuelta.
  unsigned long ahora = millis();

  // Si no pasaron 50 ms desde la ultima lectura, termina la vuelta.
  if (ahora - ultimaLectura < INTERVALO_LECTURA_MS) {
    return;
  }
  // Anota el instante de esta lectura.
  ultimaLectura = ahora;

  // Lee la presion (promedio de varias lecturas).
  int presion = leerPresion();

  // Valores de salida; empiezan en "sin presion".
  int porcentaje = 0;
  int frecuencia = 0;
  int nivel = NIVEL_NINGUNO;

  // Solo si la lectura supera el umbral hay presion suficiente.
  if (presion > UMBRAL_PRESION) {
    // Limita la lectura al rango de trabajo (de 20 a 800).
    int valor = constrain(presion, UMBRAL_PRESION, PRESION_MAXIMA);
    // Convierte la lectura a porcentaje de 0 a 100.
    porcentaje = map(valor, UMBRAL_PRESION, PRESION_MAXIMA, 0, 100);
    // Convierte la lectura a frecuencia: mas fuerza, tono mas agudo.
    frecuencia = map(valor, UMBRAL_PRESION, PRESION_MAXIMA,
                     FRECUENCIA_MINIMA, FRECUENCIA_MAXIMA);
    // Decide el nivel (leve, media o fuerte) segun el porcentaje.
    nivel = calcularNivel(porcentaje);
    // Emite el tono continuo con esa frecuencia.
    tone(PIN_BUZZER, frecuencia);
  } else {
    // Sin presion suficiente: silencia el buzzer.
    noTone(PIN_BUZZER);
  }

  // Actualiza el semaforo con el nivel calculado.
  mostrarNivel(nivel);

  // Cada 250 ms escribe una linea en el Monitor Serial.
  if (ahora - ultimoMensaje >= INTERVALO_MENSAJE_MS) {
    // Anota el instante del mensaje.
    ultimoMensaje = ahora;
    // Lectura cruda del sensor.
    Serial.print("Lectura: ");
    Serial.print(presion);
    // Porcentaje relativo.
    Serial.print(" | Presion: ");
    Serial.print(porcentaje);
    Serial.print(" %");
    // Frecuencia del buzzer (0 = silencio).
    Serial.print(" | Tono: ");
    Serial.print(frecuencia);
    Serial.print(" Hz");
    // Nombre del nivel.
    Serial.print(" | Nivel: ");
    Serial.println(nombreNivel(nivel));
  }
}