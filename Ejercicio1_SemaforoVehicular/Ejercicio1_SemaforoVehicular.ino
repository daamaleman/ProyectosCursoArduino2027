// Ejercicio 1: Semaforo vehicular con modo manual por Monitor Serial
// Arduino UNO R4 WiFi + modulo Keyestudio Traffic Light
// Conexiones: R -> D5, Y -> D4, G -> D3, GND -> GND
// Comandos: r = rojo, a = amarillo, v = verde (modo manual)
//           x = volver al modo automatico, h = ayuda

// Pines del modulo semaforo
const int PIN_ROJO = 5;
const int PIN_AMARILLO = 4;
const int PIN_VERDE = 3;

// Tiempos de cada color en milisegundos (1000 ms = 1 s)
const unsigned long TIEMPO_VERDE = 5000;
const unsigned long TIEMPO_AMARILLO = 2000;
const unsigned long TIEMPO_ROJO = 5000;

// Cada cuantos milisegundos se repiten las instrucciones
const unsigned long INTERVALO_MENU = 5000;

// Identificadores de los colores
const int VERDE = 0;
const int AMARILLO = 1;
const int ROJO = 2;

int colorActual = VERDE;            // color encendido ahora
bool modoManual = false;            // false = automatico, true = manual
unsigned long inicioColor = 0;      // momento en que se encendio el color
unsigned long ultimoMenu = 0;       // momento del ultimo envio del menu

// Muestra las instrucciones en el Monitor Serial
void mostrarMenu() {
  Serial.println("SEMAFORO VEHICULAR - ARDUINO UNO R4 WIFI");
  Serial.println("----------------------------------------");
  Serial.println("Digite r para ROJO (modo manual)");
  Serial.println("Digite a para AMARILLO (modo manual)");
  Serial.println("Digite v para VERDE (modo manual)");
  Serial.println("Digite x para volver al modo AUTOMATICO");
  Serial.println("Digite h para repetir estas instrucciones");
}

// Devuelve cuanto dura cada color en el modo automatico
unsigned long duracionColor(int color) {
  if (color == VERDE) return TIEMPO_VERDE;
  if (color == AMARILLO) return TIEMPO_AMARILLO;
  return TIEMPO_ROJO;
}

// Apaga las tres luces y enciende solo la del color pedido
void ponerColor(int color) {
  digitalWrite(PIN_ROJO, LOW);
  digitalWrite(PIN_AMARILLO, LOW);
  digitalWrite(PIN_VERDE, LOW);

  if (color == ROJO) {
    digitalWrite(PIN_ROJO, HIGH);
  } else if (color == AMARILLO) {
    digitalWrite(PIN_AMARILLO, HIGH);
  } else {
    digitalWrite(PIN_VERDE, HIGH);
  }

  colorActual = color;
  inicioColor = millis();           // reinicia el cronometro del color

  // Informa el modo y el color por el Monitor Serial
  if (modoManual) {
    Serial.print("[MANUAL] ");
  } else {
    Serial.print("[AUTOMATICO] ");
  }
  if (color == ROJO) {
    Serial.println("ROJO");
  } else if (color == AMARILLO) {
    Serial.println("AMARILLO");
  } else {
    Serial.println("VERDE");
  }
}

// Interpreta un caracter recibido por el Monitor Serial
void atenderComando(char dato) {
  if (dato == 'r' || dato == 'R') {
    modoManual = true;
    ponerColor(ROJO);
  } else if (dato == 'a' || dato == 'A') {
    modoManual = true;
    ponerColor(AMARILLO);
  } else if (dato == 'v' || dato == 'V') {
    modoManual = true;
    ponerColor(VERDE);
  } else if (dato == 'x' || dato == 'X') {
    modoManual = false;
    Serial.println("Volviendo al modo AUTOMATICO");
    ponerColor(VERDE);              // el ciclo automatico empieza en verde
  } else if (dato == 'h' || dato == 'H') {
    mostrarMenu();
    ultimoMenu = millis();
  } else if (dato != '\r' && dato != '\n' && dato != ' ') {
    // Se ignoran los finales de linea y los espacios
    Serial.println("Comando no valido. Use r, a, v, x o h.");
  }
}

void setup() {
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);

  Serial.begin(9600);
  delay(1500);                      // pausa inicial para el enlace USB

  mostrarMenu();
  ultimoMenu = millis();
  ponerColor(VERDE);                // el semaforo arranca en verde
}

void loop() {
  unsigned long ahora = millis();

  // Repite las instrucciones aunque el monitor se abra mas tarde
  if (ahora - ultimoMenu >= INTERVALO_MENU) {
    mostrarMenu();
    ultimoMenu = ahora;
  }

  // Atiende todos los caracteres recibidos sin esperar
  while (Serial.available() > 0) {
    char dato = Serial.read();
    atenderComando(dato);
  }

  // En modo automatico, cambia de color cuando termina su tiempo
  // (se lee millis() de nuevo porque un comando pudo reiniciar el cronometro)
  ahora = millis();
  if (!modoManual && (ahora - inicioColor >= duracionColor(colorActual))) {
    if (colorActual == VERDE) {
      ponerColor(AMARILLO);
    } else if (colorActual == AMARILLO) {
      ponerColor(ROJO);
    } else {
      ponerColor(VERDE);
    }
  }
}