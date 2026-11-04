/*
  ============================================================================
  EJERCICIO 8: BARRERA DE ACCESO CON INFRARROJO Y SERVO
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor infrarrojo Keyestudio KS0051 (pines G, V, S, EN)
                - Servomotor SG90 de posicion (brazo de la barrera)
                - Fuente regulada de 5 V para el servo
                - Modulo semaforo Keyestudio Traffic Light (GND, G, Y, R)
  LIBRERIA:     Servo (viene incluida con el Arduino IDE)

  CONEXIONES:   Sensor    G -> GND   V -> 5V   S -> D2   (EN sin cable:
                                                     puente amarillo puesto)
                Servo     senal (naranja/amarillo) -> D9
                          rojo   -> positivo de la fuente de 5 V
                          marron -> negativo de la fuente
                Fuente    negativo -> TAMBIEN a GND del Arduino (tierra comun)
                Semaforo  GND -> GND   G -> D3 (verde)   R -> D5 (rojo)

  QUE HACE:     1. Al encender, aprende cual es el estado del sensor cuando
                   NO hay objeto delante (zona libre).
                2. Barrera CERRADA: servo a 0 grados y semaforo en ROJO.
                3. Al detectar un objeto, la barrera se ABRE: servo a 90
                   grados y semaforo en VERDE. Se cuenta un vehiculo.
                4. Mientras haya objeto, sigue abierta. Cuando pasan 3 s
                   seguidos sin detectar nada, se CIERRA.
                5. El Monitor Serial muestra los eventos y el contador.

  IMPORTANTE:   Durante los 3 primeros segundos NO ponga nada frente al
                sensor. Es una maqueta educativa: no se usa como proteccion
                de personas ni de vehiculos reales.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// Se incluye la libreria que genera los pulsos de control del servo.
#include <Servo.h>

// ============================================================================
// 1. PINES Y CONSTANTES
// ============================================================================

// Pin donde se conecta la salida (S) del sensor infrarrojo.
const int PIN_SENSOR = 2;

// Pin donde se conecta la senal de control del servo.
const int PIN_SERVO = 9;

// Pines del modulo semaforo: verde = barrera abierta, rojo = cerrada.
const int PIN_VERDE = 3;
const int PIN_ROJO  = 5;

// Posicion del servo con la barrera cerrada (brazo horizontal), en grados.
const int POSICION_CERRADA = 0;

// Posicion del servo con la barrera abierta (brazo vertical), en grados.
const int POSICION_ABIERTA = 90;

// Tiempo sin detectar nada antes de cerrar: 3000 ms = 3 s.
const unsigned long TIEMPO_CIERRE_MS = 3000UL;

// Cada cuantos ms se muestra el estado en el Monitor Serial: 500 ms.
const unsigned long INTERVALO_MENSAJE_MS = 500UL;

// Cuantas lecturas se toman al arrancar para aprender el estado libre.
const int MUESTRAS_INICIALES = 20;

// ============================================================================
// 2. VARIABLES DE ESTADO
// ============================================================================

// Objeto que representa el servo.
Servo barrera;

// Valor (HIGH o LOW) que entrega el sensor cuando NO hay objeto.
// Se aprende al arrancar; asi funciona aunque el modulo trabaje al reves.
int estadoSinObjeto = LOW;

// Estado de la barrera: true = abierta, false = cerrada.
bool barreraAbierta = false;

// Instante (millis) de la ultima vez que se detecto un objeto.
unsigned long ultimaDeteccion = 0;

// Instante (millis) del ultimo mensaje enviado al Monitor Serial.
unsigned long ultimoMensaje = 0;

// Cantidad de vehiculos contados (una apertura = un vehiculo).
int vehiculos = 0;

// ============================================================================
// 3. FUNCIONES PROPIAS
// ============================================================================

// Enciende el LED del semaforo que corresponde al estado de la barrera.
//   abierta : true = verde encendido, rojo apagado; false = al reves
void mostrarSemaforo(bool abierta) {
  // Verde solo si la barrera esta abierta.
  digitalWrite(PIN_VERDE, abierta ? HIGH : LOW);
  // Rojo solo si la barrera esta cerrada.
  digitalWrite(PIN_ROJO, abierta ? LOW : HIGH);
}

// Abre la barrera: mueve el servo, cambia el semaforo y cuenta el vehiculo.
void abrirBarrera() {
  // Orden al servo: ir a la posicion de apertura (90 grados).
  barrera.write(POSICION_ABIERTA);
  // Guarda el nuevo estado.
  barreraAbierta = true;
  // Semaforo en verde.
  mostrarSemaforo(true);
  // Cuenta un vehiculo mas.
  vehiculos++;
  // Informa por el Monitor Serial.
  Serial.print("ABRIENDO: servo a 90 grados. Vehiculos: ");
  Serial.println(vehiculos);
}

// Cierra la barrera: mueve el servo y cambia el semaforo.
void cerrarBarrera() {
  // Orden al servo: volver a la posicion cerrada (0 grados).
  barrera.write(POSICION_CERRADA);
  // Guarda el nuevo estado.
  barreraAbierta = false;
  // Semaforo en rojo.
  mostrarSemaforo(false);
  // Informa por el Monitor Serial.
  Serial.println("CERRANDO: servo a 0 grados.");
}

// ============================================================================
// 4. setup(): SE EJECUTA UNA SOLA VEZ AL ENCENDER
// ============================================================================
void setup() {
  // Abre la comunicacion con el Monitor Serial a 9600 baudios.
  Serial.begin(9600);
  // Pausa breve para que el Monitor Serial alcance a conectarse.
  delay(1500);

  // El sensor es una entrada; el semaforo son dos salidas.
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);

  // Conecta el servo al pin de control y lo lleva a la posicion cerrada.
  barrera.attach(PIN_SERVO);
  barrera.write(POSICION_CERRADA);
  // Semaforo en rojo: la barrera empieza cerrada.
  mostrarSemaforo(false);

  // Mensajes de bienvenida.
  Serial.println("BARRERA DE ACCESO - ARDUINO UNO R4 WIFI");
  Serial.println("---------------------------------------");

  // Cuenta regresiva de 3 s: es el tiempo para dejar libre el sensor.
  for (int segundos = 3; segundos > 0; segundos--) {
    Serial.print("Mantenga libre el sensor. Inicio en ");
    Serial.print(segundos);
    Serial.println(" segundos.");
    delay(1000);
  }

  // Toma 20 lecturas con la zona libre y cuenta cuantas fueron HIGH.
  int cantidadDeUnos = 0;
  for (int muestra = 0; muestra < MUESTRAS_INICIALES; muestra++) {
    cantidadDeUnos += digitalRead(PIN_SENSOR);
    delay(10);
  }

  // El valor que aparecio en la mayoria de las lecturas es el estado "libre".
  estadoSinObjeto = (cantidadDeUnos >= MUESTRAS_INICIALES / 2) ? HIGH : LOW;

  // Muestra el estado aprendido.
  Serial.print("Estado del sensor SIN objeto: ");
  Serial.println(estadoSinObjeto);
  Serial.println("Sistema listo. Acerque un objeto.");
}

// ============================================================================
// 5. loop(): SE REPITE CONTINUAMENTE
// ============================================================================
void loop() {
  // Lee el reloj una sola vez por vuelta.
  unsigned long ahora = millis();

  // Lee el sensor: HIGH o LOW.
  int lectura = digitalRead(PIN_SENSOR);

  // Hay objeto si la lectura es DISTINTA del estado aprendido como "libre".
  bool hayObjeto = (lectura != estadoSinObjeto);

  if (hayObjeto) {
    // Cada deteccion renueva el tiempo de espera para cerrar.
    ultimaDeteccion = ahora;
    // Si la barrera estaba cerrada, se abre (y se cuenta un vehiculo).
    if (!barreraAbierta) {
      abrirBarrera();
    }
  } else if (barreraAbierta && (ahora - ultimaDeteccion >= TIEMPO_CIERRE_MS)) {
    // Barrera abierta y ya pasaron 3 s seguidos sin objeto: se cierra.
    cerrarBarrera();
  }

  // Cada 500 ms muestra el estado (sin delay(), para seguir leyendo el sensor).
  if (ahora - ultimoMensaje >= INTERVALO_MENSAJE_MS) {
    // Anota el instante del mensaje.
    ultimoMensaje = ahora;
    // Muestra la lectura cruda del sensor.
    Serial.print("Sensor S = ");
    Serial.print(lectura);
    // Describe la situacion actual.
    if (hayObjeto) {
      Serial.print(" | OBJETO | Barrera a 90 grados");
    } else if (barreraAbierta) {
      Serial.print(" | SIN OBJETO | Esperando para cerrar");
    } else {
      Serial.print(" | SIN OBJETO | Barrera a 0 grados");
    }
    // Muestra el contador de vehiculos.
    Serial.print(" | Vehiculos: ");
    Serial.println(vehiculos);
  }
}