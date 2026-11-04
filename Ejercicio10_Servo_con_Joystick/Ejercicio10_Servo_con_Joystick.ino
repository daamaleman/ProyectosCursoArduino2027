/*
  ============================================================================
  EJERCICIO 10: CONTROL DE SERVO CON JOYSTICK
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Modulo joystick Keyestudio (marcas Y, X, B, V, G)
                - Servomotor SG90 con fuente externa de 5 V
                - LED de confirmacion (con resistencia de 330 ohm)
                - LED de brillo (con resistencia de 330 ohm)  [extension]

  CONEXIONES:   Joystick  G -> GND   V -> 5V   (alimentacion)
                          Y -> A1    (eje horizontal, "VRY" en otras guias)
                          X -> A0    (eje vertical,   "VRX" en otras guias)
                          B -> D2    (boton, "SW" en otras guias)
                Servo     senal -> D9
                          rojo  -> positivo de la fuente externa de 5 V
                          marron-> negativo de la fuente externa de 5 V
                Fuente externa: negativo unido a GND del Arduino (tierra comun)
                LED de confirmacion: D8 -> 330 ohm -> LED -> GND
                LED de brillo:       D5 -> 330 ohm -> LED -> GND

  QUE HACE:     - Inclinar el joystick a la derecha o a la izquierda mueve el
                  servo. Mientras mas se inclina, mas rapido se mueve.
                - Al soltar el joystick, el servo conserva su posicion.
                - Al presionar el boton, el servo vuelve a 90 grados y se
                  enciende el LED de confirmacion durante 1 segundo.
                - EXTENSION: inclinar el joystick hacia arriba o hacia abajo
                  sube o baja el brillo del segundo LED con PWM.

  CONCEPTOS:    Dos entradas analogicas y una digital, zona muerta, map().

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

#include <Servo.h>  // Biblioteca oficial del servomotor.

// ============================================================================
// 1. PINES Y CONSTANTES
// ============================================================================

// Pin analogico del eje horizontal (marca Y del modulo).
const int PIN_HORIZONTAL = A1;

// Pin analogico del eje vertical (marca X del modulo).
const int PIN_VERTICAL = A0;

// Pin digital del boton (marca B del modulo).
const int PIN_BOTON = 2;

// Pin de senal del servo.
const int PIN_SERVO = 9;

// Pin del LED de confirmacion (se enciende al presionar el boton).
const int PIN_LED_CONFIRMACION = 8;

// Pin del LED de brillo. Debe ser un pin con PWM (D3, D5, D6, D9, D10, D11).
const int PIN_LED_BRILLO = 5;

// Zona muerta: mientras la lectura este entre estos dos valores se considera
// que el joystick esta en reposo (el centro real ronda 510 +/- unas decenas).
const int ZONA_MUERTA_BAJA = 400;
const int ZONA_MUERTA_ALTA = 600;

// Valor maximo de la lectura analogica con resolucion de 10 bits.
const int LECTURA_MAXIMA = 1023;

// Limites del servo en grados (se evitan los extremos para no forzarlo).
const int ANGULO_MINIMO = 10;
const int ANGULO_MAXIMO = 170;

// Posicion central del servo en grados.
const int ANGULO_CENTRO = 90;

// Si el brazo gira al reves de lo esperado, cambiar 1 por -1.
const int SENTIDO_HORIZONTAL = 1;

// Si el brillo sube al reves de lo esperado, cambiar 1 por -1.
const int SENTIDO_VERTICAL = 1;

// Cambio maximo del angulo por actualizacion, en grados (joystick al tope).
const int PASO_MAXIMO_SERVO = 4;

// Cambio maximo del brillo por actualizacion (escala de 0 a 255).
const int PASO_MAXIMO_BRILLO = 8;

// Cada cuantos ms se actualizan el servo y el brillo: 20 ms.
const unsigned long INTERVALO_ACTUALIZACION_MS = 20UL;

// Cuanto tiempo queda encendido el LED de confirmacion: 1000 ms.
const unsigned long TIEMPO_CONFIRMACION_MS = 1000UL;

// Cada cuantos ms se escribe una linea en el Monitor Serial: 250 ms.
const unsigned long INTERVALO_MENSAJE_MS = 250UL;

// ============================================================================
// 2. VARIABLES DE ESTADO
// ============================================================================

// Objeto que controla el servo.
Servo miServo;

// Angulo actual ordenado al servo, en grados.
int angulo = ANGULO_CENTRO;

// Ultimo angulo enviado al servo (para enviar solo cuando cambia).
int anguloEnviado = -1;

// Brillo actual del segundo LED, de 0 a 255.
int brillo = 0;

// Instante del ultimo pulso del boton (para el LED de confirmacion).
unsigned long ultimoPulso = 0;

// Indica si el LED de confirmacion esta encendido.
bool confirmacionEncendida = false;

// Indica si el boton estaba presionado en la vuelta anterior.
bool botonAntes = false;

// Instantes de la ultima actualizacion y del ultimo mensaje.
unsigned long ultimaActualizacion = 0;
unsigned long ultimoMensaje = 0;

// ============================================================================
// 3. FUNCIONES PROPIAS
// ============================================================================

// Convierte una lectura del joystick en un cambio por actualizacion.
//   Dentro de la zona muerta: 0 (no se mueve).
//   Por encima: de +1 (apenas inclinado) a +pasoMaximo (al tope).
//   Por debajo: de -1 (apenas inclinado) a -pasoMaximo (al tope).
int calcularPaso(int lectura, int pasoMaximo) {
  // Inclinado hacia valores altos (la lectura se acerca a 1023).
  if (lectura > ZONA_MUERTA_ALTA) {
    return map(lectura, ZONA_MUERTA_ALTA + 1, LECTURA_MAXIMA, 1, pasoMaximo);
  }
  // Inclinado hacia valores bajos (la lectura se acerca a 0).
  if (lectura < ZONA_MUERTA_BAJA) {
    return -map(lectura, ZONA_MUERTA_BAJA - 1, 0, 1, pasoMaximo);
  }
  // En reposo: sin cambio.
  return 0;
}

// ============================================================================
// 4. setup(): SE EJECUTA UNA SOLA VEZ AL ENCENDER
// ============================================================================
void setup() {
  // Abre la comunicacion con el Monitor Serial a 9600 baudios.
  Serial.begin(9600);
  // Pausa breve para que el Monitor Serial alcance a conectarse.
  delay(1500);

  // Lecturas analogicas de 10 bits: valores de 0 a 1023.
  analogReadResolution(10);

  // El boton usa la resistencia interna: presionado = LOW.
  pinMode(PIN_BOTON, INPUT_PULLUP);
  // Los dos LED son salidas.
  pinMode(PIN_LED_CONFIRMACION, OUTPUT);
  pinMode(PIN_LED_BRILLO, OUTPUT);

  // Estado inicial: LED apagados.
  digitalWrite(PIN_LED_CONFIRMACION, LOW);
  analogWrite(PIN_LED_BRILLO, 0);

  // Conecta el servo y lo lleva al centro.
  miServo.attach(PIN_SERVO);
  miServo.write(angulo);
  anguloEnviado = angulo;
  // Espera a que el servo llegue a la posicion inicial.
  delay(500);

  // Mensajes de bienvenida.
  Serial.println("SERVO CON JOYSTICK - ARDUINO UNO R4 WIFI");
  Serial.println("----------------------------------------");
  Serial.println("Horizontal: mueve el servo | Vertical: brillo | Boton: centra");
}

// ============================================================================
// 5. loop(): SE REPITE CONTINUAMENTE
// ============================================================================
void loop() {
  // Lee el reloj una sola vez por vuelta.
  unsigned long ahora = millis();

  // Lee las dos entradas analogicas y la digital en cada vuelta.
  int horizontal = analogRead(PIN_HORIZONTAL);
  int vertical = analogRead(PIN_VERTICAL);
  bool botonPresionado = (digitalRead(PIN_BOTON) == LOW);

  // ---------------------------------------------------------------------
  // BOTON: tiene prioridad sobre el joystick
  // ---------------------------------------------------------------------
  if (botonPresionado) {
    // Mientras este presionado, el servo queda en el centro.
    angulo = ANGULO_CENTRO;
    // Renueva el instante del pulso: el LED sigue encendido.
    ultimoPulso = ahora;
    confirmacionEncendida = true;
    digitalWrite(PIN_LED_CONFIRMACION, HIGH);
    // Informa solo en el instante en que se presiona.
    if (!botonAntes) {
      Serial.println(">>> Boton presionado: servo al centro (90 grados)");
    }
  } else if (confirmacionEncendida && (ahora - ultimoPulso >= TIEMPO_CONFIRMACION_MS)) {
    // Pasado 1 s desde el ultimo pulso, se apaga el LED de confirmacion.
    confirmacionEncendida = false;
    digitalWrite(PIN_LED_CONFIRMACION, LOW);
  }
  botonAntes = botonPresionado;

  // ---------------------------------------------------------------------
  // JOYSTICK: se actualiza cada 20 ms
  // ---------------------------------------------------------------------
  if (ahora - ultimaActualizacion >= INTERVALO_ACTUALIZACION_MS) {
    // Anota el instante de esta actualizacion.
    ultimaActualizacion = ahora;

    // Con el boton suelto, el eje horizontal cambia el angulo.
    if (!botonPresionado) {
      angulo = angulo + SENTIDO_HORIZONTAL * calcularPaso(horizontal, PASO_MAXIMO_SERVO);
    }
    // Limita el angulo al recorrido permitido.
    angulo = constrain(angulo, ANGULO_MINIMO, ANGULO_MAXIMO);

    // Envia el angulo al servo solo si cambio.
    if (angulo != anguloEnviado) {
      miServo.write(angulo);
      anguloEnviado = angulo;
    }

    // EXTENSION: el eje vertical cambia el brillo del segundo LED.
    brillo = brillo + SENTIDO_VERTICAL * calcularPaso(vertical, PASO_MAXIMO_BRILLO);
    brillo = constrain(brillo, 0, 255);
    analogWrite(PIN_LED_BRILLO, brillo);
  }

  // ---------------------------------------------------------------------
  // MENSAJE POR EL MONITOR SERIAL (cada 250 ms)
  // ---------------------------------------------------------------------
  if (ahora - ultimoMensaje >= INTERVALO_MENSAJE_MS) {
    // Anota el instante del mensaje.
    ultimoMensaje = ahora;
    Serial.print("Horizontal: ");
    Serial.print(horizontal);
    Serial.print(" | Servo: ");
    Serial.print(angulo);
    Serial.print(" grados | Vertical: ");
    Serial.print(vertical);
    Serial.print(" | Brillo: ");
    Serial.print(brillo * 100 / 255);
    Serial.print(" % | Boton: ");
    Serial.println(botonPresionado ? "PRESIONADO" : "libre");
  }
}