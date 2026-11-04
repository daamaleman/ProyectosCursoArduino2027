/*
  ====================================================================
  EJERCICIO 2: INTERRUPTOR TACTIL CON MEMORIA
  ====================================================================
  Placa:       Arduino UNO R4 WiFi
  Componentes: Modulo Keyestudio Touch (sensor capacitivo tactil)
               LED externo (por ejemplo rojo de 5 mm)
               Resistencia de 1 kOhm (1000 ohmios) en serie con el LED
  Libreria:    Ninguna. Solo se usan funciones incluidas en Arduino.

  CONEXIONES
    Modulo tactil:  S -> D2     V -> 5V     G -> GND
    LED externo:    D7 -> resistencia 1 kOhm -> anodo (+) del LED
                    catodo (-) del LED -> GND

  QUE HACE EL PROGRAMA
    Cada vez que se TOCA el modulo, el LED cambia de estado:
    si estaba apagado se enciende, y si estaba encendido se apaga.
    El LED "recuerda" su estado hasta el siguiente toque (memoria).
    Un toque largo cuenta como UN solo toque.
    El Monitor Serial muestra el numero de toques y el estado.
  ====================================================================
*/


// ---------------------------------------------------------------
// 1. PINES Y CONSTANTES
// ---------------------------------------------------------------
// "const" significa que el valor no cambia mientras el programa se
// ejecuta. "int" es un numero entero.

// Pin digital D2: recibe la senal del modulo tactil (terminal S).
const int PIN_TACTIL = 2;

// Pin digital D7: entrega la senal al LED (a traves de la resistencia).
// OJO: no se llama PIN_LED porque la placa UNO R4 WiFi ya define ese
// nombre (es el LED interno del pin 13) y el programa no compilaria.
const int PIN_LED_EXTERNO = 7;

// Nivel que entrega el modulo mientras se le toca. Este modulo
// entrega HIGH (5 V) al tocarlo y LOW (0 V) en reposo.
// Si el modulo trabajara al reves, bastaria con cambiar esta linea
// por: const int TOCADO = LOW;
const int TOCADO = HIGH;

// Tiempo minimo, en milisegundos, entre dos toques aceptados.
// Evita que una lectura inestable cuente dos toques por error
// (antirrebote). 1000 ms = 1 s; aqui 80 ms.
// "unsigned long" es un entero grande que nunca es negativo; es
// el tipo que devuelve la funcion millis().
const unsigned long TIEMPO_ANTIRREBOTE = 80;

// Cada cuantos milisegundos se informa el estado por el Monitor
// Serial, aunque no se toque el modulo (5000 ms = 5 s). Sirve para
// ver el estado aunque el monitor se abra despues del arranque.
const unsigned long INTERVALO_ESTADO = 5000;


// ---------------------------------------------------------------
// 2. VARIABLES DE ESTADO
// ---------------------------------------------------------------
// Estas variables SI cambian mientras el programa funciona. Guardan
// la "memoria" del sistema.

// Estado del LED. "bool" solo admite dos valores: true (verdadero)
// o false (falso). true = LED encendido, false = LED apagado.
bool ledEncendido = false;

// Indica si el modulo estaba siendo tocado en la vuelta anterior de
// loop(). Permite detectar el MOMENTO en que empieza un toque.
bool tocadoAnterior = false;

// Cuenta cuantos toques se han registrado desde el arranque.
unsigned long contadorToques = 0;

// Instante (en ms desde que se encendio la placa) del ultimo toque
// aceptado. Se usa para el antirrebote.
unsigned long ultimoToque = 0;

// Instante del ultimo informe de estado por el Monitor Serial.
unsigned long ultimoEstado = 0;


// ---------------------------------------------------------------
// 3. FUNCION informarEstado()
// ---------------------------------------------------------------
// Una funcion es un bloque de codigo con nombre que se puede
// ejecutar las veces que se quiera. "void" indica que no devuelve
// ningun valor. Esta funcion imprime el estado actual.
void informarEstado() {
  // Serial.print() envia texto SIN salto de linea.
  Serial.print("Estado: LED ");

  // Se elige el texto segun el valor de la variable ledEncendido.
  if (ledEncendido) {
    Serial.print("ENCENDIDO");
  } else {
    Serial.print("APAGADO");
  }

  // Se agrega el numero de toques. Serial.println() envia el
  // contenido y termina la linea.
  Serial.print(" | Toques: ");
  Serial.println(contadorToques);
}


// ---------------------------------------------------------------
// 4. FUNCION setup()
// ---------------------------------------------------------------
// setup() se ejecuta UNA sola vez: al encender la placa, al subir el
// programa o al pulsar el boton RESET.
void setup() {
  // pinMode() define si un pin es entrada (INPUT) o salida (OUTPUT).
  // El modulo tactil ENVIA una senal a la placa: es una entrada.
  pinMode(PIN_TACTIL, INPUT);

  // El LED RECIBE la senal de la placa: es una salida.
  pinMode(PIN_LED_EXTERNO, OUTPUT);

  // Se asegura que el LED inicie apagado. LOW = 0 V = LED apagado.
  digitalWrite(PIN_LED_EXTERNO, LOW);

  // Abre la comunicacion con la computadora por el cable USB a 9600
  // baudios. El Monitor Serial debe usar la misma velocidad.
  Serial.begin(9600);

  // delay() detiene el programa el tiempo indicado en milisegundos.
  // Da 1,5 s para que la conexion USB quede establecida.
  delay(1500);

  // Mensajes iniciales de instrucciones.
  Serial.println("INTERRUPTOR TACTIL CON MEMORIA - ARDUINO UNO R4 WIFI");
  Serial.println("----------------------------------------------------");
  Serial.println("Toque el modulo para cambiar el estado del LED.");

  // Se informa el estado inicial (LED apagado, 0 toques).
  informarEstado();

  // Se guarda el instante de este informe para contar 5 s hasta el
  // siguiente.
  ultimoEstado = millis();
}


// ---------------------------------------------------------------
// 5. FUNCION loop()
// ---------------------------------------------------------------
// loop() se repite una y otra vez, miles de veces por segundo,
// mientras la placa tenga energia.
void loop() {
  // millis() devuelve los milisegundos transcurridos desde que se
  // encendio la placa. Se guarda en "ahora".
  unsigned long ahora = millis();

  // ---- PASO 1: leer el modulo tactil ----
  // digitalRead() devuelve HIGH o LOW segun el nivel del pin.
  int lectura = digitalRead(PIN_TACTIL);

  // "==" compara si dos valores son iguales. La variable tocadoAhora
  // vale true si el modulo esta siendo tocado en este momento.
  bool tocadoAhora = (lectura == TOCADO);

  // ---- PASO 2: detectar el INICIO de un toque ----
  // Un toque nuevo ocurre cuando AHORA se toca y ANTES no se tocaba.
  // Esto se llama detectar un "flanco". Asi, si el dedo permanece
  // sobre el modulo, la condicion deja de cumplirse y el toque largo
  // cuenta solo UNA vez.
  // "&&" significa "y". "!" significa "no".
  if (tocadoAhora && !tocadoAnterior) {
    // Antirrebote: solo se acepta el toque si pasaron al menos 80 ms
    // desde el ultimo toque aceptado.
    if (ahora - ultimoToque >= TIEMPO_ANTIRREBOTE) {
      // Se invierte el estado del LED: "!" cambia true por false y
      // false por true. Esta es la "memoria" del interruptor.
      ledEncendido = !ledEncendido;

      // Se aplica el nuevo estado al pin del LED.
      // HIGH = 5 V = LED encendido; LOW = 0 V = LED apagado.
      if (ledEncendido) {
        digitalWrite(PIN_LED_EXTERNO, HIGH);
      } else {
        digitalWrite(PIN_LED_EXTERNO, LOW);
      }

      // Se suma uno al contador. "++" aumenta la variable en 1.
      contadorToques++;

      // Se guarda el instante de este toque para el antirrebote.
      ultimoToque = ahora;

      // Se informa el toque por el Monitor Serial.
      Serial.print("Toque numero ");
      Serial.print(contadorToques);
      if (ledEncendido) {
        Serial.println(" -> LED ENCENDIDO");
      } else {
        Serial.println(" -> LED APAGADO");
      }

      // El informe periodico se reinicia para no repetirse de
      // inmediato despues de un toque.
      ultimoEstado = ahora;
    }
  }

  // ---- PASO 3: recordar la lectura para la proxima vuelta ----
  // Al final de la vuelta, "ahora" pasa a ser "antes". En la proxima
  // vuelta se comparara contra este valor.
  tocadoAnterior = tocadoAhora;

  // ---- PASO 4: informar el estado cada 5 segundos ----
  // "ahora - ultimoEstado" es el tiempo transcurrido desde el ultimo
  // informe. Si alcanzo o supero 5000 ms, se informa otra vez.
  if (ahora - ultimoEstado >= INTERVALO_ESTADO) {
    informarEstado();
    ultimoEstado = ahora;
  }

  // No se usa delay() en loop(): asi el programa lee el modulo
  // miles de veces por segundo y no se pierde ningun toque.
}