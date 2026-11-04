/*
  ============================================================================
  EJERCICIO 6B: DETECTOR DE PRESENCIA CON STRIP LED
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Modulo sensor PIR Keyestudio (marcas S, V, G)
                - Tira LED RGB direccionable de 8 LED (NeoPixel / WS2812)
                - Resistencia de 330 ohm en la linea de datos de la tira
  LIBRERIA:     Adafruit NeoPixel (Administrador de bibliotecas)

  CONEXIONES:   PIR  S -> D2      (salida digital del sensor)
                PIR  V -> 5V      (alimentacion)
                PIR  G -> GND     (tierra)
                Tira LED  +5V -> 5V     GND -> GND
                Tira LED  DIN -> resistencia 330 ohm -> D6

  QUE HACE:     1. Al encender, espera 30 s a que el PIR se estabilice.
                2. Si el PIR detecta movimiento, el estado pasa a OCUPADO
                   y la tira se enciende en rojo.
                3. Cuando deja de haber movimiento, el programa espera 5 s
                   mas (tiempo de espera) y entonces pasa a LIBRE y apaga
                   la tira.
                4. En cada cambio, el Monitor Serial muestra el nuevo
                   estado y cuanto duro el estado anterior.
                5. Cada 60 s informa cuantas detecciones hubo en ese minuto.

  IMPORTANTE:   LIBRE significa "sin movimiento detectado", NO "cuarto vacio".
                Un PIR solo detecta cambios de calor en movimiento; una
                persona totalmente quieta puede pasar desapercibida.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// Se incluye la libreria que sabe hablar con la tira LED.
#include <Adafruit_NeoPixel.h>

// ============================================================================
// 1. PINES Y CONSTANTES
// ============================================================================

// Pin donde se conecta la salida (S) del sensor PIR.
// Entrega HIGH (5 V) cuando detecta movimiento y LOW (0 V) cuando no.
const int PIN_PIR = 2;

// Pin donde se conecta la entrada de datos (DIN) de la tira,
// a traves de la resistencia de 330 ohm.
const int PIN_TIRA = 6;

// Cantidad de LED de la tira.
const int NUM_LEDS = 8;

// Color de la tira en estado OCUPADO (rojo, verde, azul; cada valor de 0 a 255).
// Se usa 120 y no 255 para limitar el consumo de corriente por el puerto USB.
const int COLOR_ROJO  = 120;
const int COLOR_VERDE = 0;
const int COLOR_AZUL  = 0;

// Tiempo de estabilizacion del PIR al encender: 30000 ms = 30 s.
// Durante este tiempo el sensor se calibra con el calor del ambiente y puede
// dar lecturas falsas, por eso el programa las ignora.
const unsigned long TIEMPO_ESTABILIZACION_MS = 30000UL;

// Tiempo de espera: cuantos ms sin movimiento debe pasar antes de declarar LIBRE.
// Evita que la tira parpadee si el sensor se apaga un instante: 5000 ms = 5 s.
const unsigned long TIEMPO_ESPERA_MS = 5000UL;

// Cada cuantos ms se muestra el aviso durante la estabilizacion: 10 s.
const unsigned long AVISO_ESTABILIZACION_MS = 10000UL;

// Duracion de la ventana para contar detecciones: 60000 ms = 1 minuto.
const unsigned long VENTANA_CONTEO_MS = 60000UL;

// ============================================================================
// 2. VARIABLES DE ESTADO
// ============================================================================

// Indica si el sensor ya termino de estabilizarse (false = todavia esperando).
bool estabilizado = false;

// Estado actual del detector: true = OCUPADO, false = LIBRE.
bool ocupado = false;

// Instante (millis) en que comenzo el estado actual. Sirve para medir cuanto dura.
unsigned long inicioEstado = 0;

// Instante (millis) de la ultima lectura HIGH del PIR.
// Sirve para saber cuanto tiempo lleva sin detectar movimiento.
unsigned long ultimaDeteccion = 0;

// Instante (millis) del ultimo aviso de estabilizacion.
unsigned long ultimoAviso = 0;

// Instante (millis) en que empezo la ventana de conteo de un minuto.
unsigned long inicioVentana = 0;

// Cantidad de detecciones (cambios de LIBRE a OCUPADO) en la ventana actual.
int detecciones = 0;

// Ultimo estado enviado a la tira. Sirve para enviar datos solo cuando cambia.
bool tiraEncendida = false;

// Objeto que representa la tira: cantidad de LED, pin de datos y tipo de tira.
// NEO_GRB: orden de color verde-rojo-azul. NEO_KHZ800: velocidad de datos de 800 kHz.
Adafruit_NeoPixel tira(NUM_LEDS, PIN_TIRA, NEO_GRB + NEO_KHZ800);

// ============================================================================
// 3. FUNCIONES PROPIAS
// ============================================================================

// Imprime un cambio de estado y la duracion del estado que termino.
//   nuevo      : texto del estado al que se pasa ("OCUPADO" o "LIBRE")
//   anterior   : texto del estado que termino
//   duracionMs : cuanto duro el estado anterior, en milisegundos
void informarCambio(const char* nuevo, const char* anterior, unsigned long duracionMs) {
  // Linea en blanco para separar visualmente cada evento.
  Serial.println();
  // Muestra el nuevo estado.
  Serial.print("Estado: ");
  Serial.println(nuevo);
  // Muestra cuanto duro el estado anterior, convertido de ms a segundos.
  Serial.print("  Tiempo en ");
  Serial.print(anterior);
  Serial.print(": ");
  Serial.print(duracionMs / 1000.0, 1);
  Serial.println(" s");
}

// Enciende toda la tira con el color de OCUPADO o la apaga.
//   encendida : true = color de OCUPADO, false = apagada
void actualizarTira(bool encendida) {
  // Si esta encendida usa el color elegido; si no, usa 0, 0, 0 (apagado).
  uint32_t color = encendida ? tira.Color(COLOR_ROJO, COLOR_VERDE, COLOR_AZUL)
                             : tira.Color(0, 0, 0);
  // Asigna el mismo color a cada uno de los LED (aun solo en memoria).
  for (int i = 0; i < NUM_LEDS; i++) {
    tira.setPixelColor(i, color);
  }
  // Envia los datos a la tira: solo ahora se ven los cambios.
  tira.show();
}

// ============================================================================
// 4. setup(): SE EJECUTA UNA SOLA VEZ AL ENCENDER
// ============================================================================
void setup() {
  // Abre la comunicacion con el Monitor Serial a 9600 baudios.
  Serial.begin(9600);
  // Pausa breve para que el Monitor Serial alcance a conectarse.
  delay(1500);

  // El pin del PIR es una entrada: el Arduino lee lo que el sensor entrega.
  pinMode(PIN_PIR, INPUT);
  // Inicia la tira: prepara el pin de datos como salida.
  tira.begin();
  // Deja la tira apagada al arrancar.
  actualizarTira(false);

  // Mensajes de bienvenida.
  Serial.println("DETECTOR DE PRESENCIA - ARDUINO UNO R4 WIFI");
  Serial.println("-------------------------------------------");
  Serial.println("Estabilizando el sensor PIR (30 s). No se mueva frente a el.");

  // Anota el instante del primer aviso para contar los 10 s siguientes.
  ultimoAviso = millis();
}

// ============================================================================
// 5. loop(): SE REPITE CONTINUAMENTE
// ============================================================================
void loop() {
  // Lee el reloj una sola vez por vuelta: todas las decisiones usan este valor.
  unsigned long ahora = millis();

  // ---------------------------------------------------------------------
  // FASE 1: ESTABILIZACION (solo al encender)
  // ---------------------------------------------------------------------
  if (!estabilizado) {
    // Si ya pasaron los 30 s, termina la estabilizacion.
    if (ahora >= TIEMPO_ESTABILIZACION_MS) {
      // Marca el sensor como listo.
      estabilizado = true;
      // El estado inicial es LIBRE y empieza a contar desde ahora.
      ocupado = false;
      inicioEstado = ahora;
      // Inicia la primera ventana de conteo de un minuto.
      inicioVentana = ahora;
      // Avisa que el detector ya funciona.
      Serial.println("Sensor listo. Estado: LIBRE");
    } else if (ahora - ultimoAviso >= AVISO_ESTABILIZACION_MS) {
      // Cada 10 s muestra cuantos segundos faltan.
      ultimoAviso = ahora;
      Serial.print("  Faltan ");
      Serial.print((TIEMPO_ESTABILIZACION_MS - ahora) / 1000UL);
      Serial.println(" s...");
    }
    // Mientras se estabiliza no se lee el sensor: termina esta vuelta.
    return;
  }

  // ---------------------------------------------------------------------
  // FASE 2: LECTURA DEL SENSOR
  // ---------------------------------------------------------------------
  // true si el PIR entrega HIGH (hay movimiento en este instante).
  bool movimiento = (digitalRead(PIN_PIR) == HIGH);

  if (movimiento) {
    // Cada vez que hay movimiento se anota el instante: reinicia la espera de 5 s.
    ultimaDeteccion = ahora;

    // Si estaba LIBRE, ocurre un cambio a OCUPADO.
    if (!ocupado) {
      // Calcula cuanto duro el estado LIBRE que acaba de terminar.
      unsigned long duracionLibre = ahora - inicioEstado;
      // Cambia el estado y reinicia el cronometro del estado.
      ocupado = true;
      inicioEstado = ahora;
      // Cuenta esta deteccion para el resumen del minuto.
      detecciones++;
      // Informa el cambio por el Monitor Serial.
      informarCambio("OCUPADO", "LIBRE", duracionLibre);
    }
  } else {
    // Sin movimiento: si estaba OCUPADO y ya pasaron los 5 s de espera, pasa a LIBRE.
    if (ocupado && (ahora - ultimaDeteccion >= TIEMPO_ESPERA_MS)) {
      // Calcula cuanto duro el estado OCUPADO que acaba de terminar.
      unsigned long duracionOcupado = ahora - inicioEstado;
      // Cambia el estado y reinicia el cronometro del estado.
      ocupado = false;
      inicioEstado = ahora;
      // Informa el cambio por el Monitor Serial.
      informarCambio("LIBRE", "OCUPADO", duracionOcupado);
    }
  }

  // ---------------------------------------------------------------------
  // FASE 3: SENALIZACION CON LA TIRA LED
  // ---------------------------------------------------------------------
  // La tira copia el estado: encendida si OCUPADO, apagada si LIBRE.
  // Solo se envian datos cuando el estado cambio, para no saturar la tira.
  if (ocupado != tiraEncendida) {
    // Guarda el estado que se va a mostrar.
    tiraEncendida = ocupado;
    // Pinta la tira con el color correspondiente.
    actualizarTira(ocupado);
  }

  // ---------------------------------------------------------------------
  // FASE 4: RESUMEN DE DETECCIONES POR MINUTO
  // ---------------------------------------------------------------------
  // Si ya paso un minuto desde el inicio de la ventana, muestra el conteo.
  if (ahora - inicioVentana >= VENTANA_CONTEO_MS) {
    // Reinicia la ventana para el minuto siguiente.
    inicioVentana = ahora;
    // Muestra cuantas veces se paso de LIBRE a OCUPADO en el ultimo minuto.
    Serial.println();
    Serial.print("Resumen: ");
    Serial.print(detecciones);
    Serial.println(" deteccion(es) en el ultimo minuto.");
    // Pone el contador en cero para el minuto siguiente.
    detecciones = 0;
  }
}