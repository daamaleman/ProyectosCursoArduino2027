/*
  ====================================================================
  EJERCICIO 14: LUZ RGB CONTROLADA POR BLUETOOTH (BLE)
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ====================================================================
  Placa:       Arduino UNO R4 WiFi
  Componentes: Modulo LED RGB Keyestudio (pines G, R, B, V)
  Biblioteca:  ArduinoBLE (se instala desde el Administrador de bibliotecas)
  Aplicacion:  nRF Connect for Mobile o LightBlue (en el celular)

  CONEXIONES
    Modulo RGB:  R -> D9    G -> D10    B -> D11
                 V -> 5V    (ver nota sobre COMUN_ANODO mas abajo)

  QUE HACE EL PROGRAMA
    La placa se anuncia por Bluetooth Low Energy con el nombre
    "RGB_UAM". Es el PERIFERICO: espera a que un celular (el CLIENTE)
    se conecte. El servicio tiene dos caracteristicas:

      COLOR  (escritura y lectura): el celular envia 3 bytes, uno por
             canal, en este orden: ROJO, VERDE, AZUL (cada uno de 0 a
             255). Ejemplo: FF8000 = rojo al maximo, verde a la mitad,
             azul apagado. El LED muestra ese color.
      ESTADO (lectura y notificacion): la placa informa el color que
             tiene encendido en este momento, tambien en 3 bytes.

    El Monitor Serial muestra las conexiones y los colores recibidos.
  ====================================================================
*/

// Biblioteca de Bluetooth Low Energy para la placa UNO R4 WiFi.
#include <ArduinoBLE.h>


// ---------------------------------------------------------------
// 1. PINES Y CONSTANTES
// ---------------------------------------------------------------

// Pines PWM que controlan cada color del modulo RGB.
// (Se evita el nombre PIN_LED porque la placa ya lo usa.)
const int PIN_ROJO  = 9;
const int PIN_VERDE = 10;
const int PIN_AZUL  = 11;

// Polaridad del modulo RGB.
//   true  = comun ANODO: el pin V va a 5V y cada color se enciende
//           con un valor BAJO (0 = maximo brillo, 255 = apagado).
//   false = comun CATODO: el pin comun va a GND y cada color se
//           enciende con un valor ALTO (255 = maximo brillo).
// Si el color que se ve es el contrario al enviado (por ejemplo, se
// pide rojo y se ve cian), cambie este valor.
const bool COMUN_ANODO = true;

// Nombre con el que la placa aparece en la lista de dispositivos BLE.
const char NOMBRE_BLE[] = "RGB_UAM";

// Identificadores (UUID) del servicio y de las dos caracteristicas.
// Se generaron una sola vez y se documentan en la guia. El celular
// debe usar EXACTAMENTE los mismos.
const char UUID_SERVICIO[] = "162fa345-72b8-41ef-91e2-5d6be3c731fa";
const char UUID_COLOR[]    = "07624d7a-960f-4a5f-a1fe-eef6d02d05af";
const char UUID_ESTADO[]   = "e6ad0332-0e13-4b74-857a-99707a6a2310";

// Cantidad de bytes de un color: rojo, verde y azul.
const int BYTES_COLOR = 3;

// Tiempo que se muestra cada color en la prueba de arranque (ms).
const unsigned long PRUEBA_COLOR_MS = 400UL;


// ---------------------------------------------------------------
// 2. OBJETOS BLE Y VARIABLES
// ---------------------------------------------------------------

// Servicio: agrupa las dos caracteristicas.
BLEService servicioLuz(UUID_SERVICIO);

// Caracteristica COLOR: el celular puede leerla y escribirla.
// El tercer parametro (3) es el tamano maximo en bytes.
BLECharacteristic caracteristicaColor(UUID_COLOR, BLERead | BLEWrite, BYTES_COLOR);

// Caracteristica ESTADO: el celular puede leerla y suscribirse para
// recibir un aviso (notificacion) cada vez que el color cambia.
BLECharacteristic caracteristicaEstado(UUID_ESTADO, BLERead | BLENotify, BYTES_COLOR);

// Color que esta encendido ahora (0 a 255 por canal).
byte rojoActual  = 0;
byte verdeActual = 0;
byte azulActual  = 0;


// ---------------------------------------------------------------
// 3. FUNCIONES PROPIAS
// ---------------------------------------------------------------

// Escribe un canal en su pin respetando la polaridad del modulo.
// analogWrite() produce una senal PWM: 0 = apagado y 255 = maximo.
void escribirCanal(int pin, byte valor) {
  if (COMUN_ANODO) {
    // Con comun anodo la logica esta invertida.
    analogWrite(pin, 255 - valor);
  } else {
    analogWrite(pin, valor);
  }
}

// Enciende el LED con el color indicado y lo informa en ESTADO.
void ponerColor(byte rojo, byte verde, byte azul) {
  rojoActual = rojo;
  verdeActual = verde;
  azulActual = azul;
  escribirCanal(PIN_ROJO, rojo);
  escribirCanal(PIN_VERDE, verde);
  escribirCanal(PIN_AZUL, azul);

  // Se actualiza la caracteristica ESTADO con el color real.
  byte datos[BYTES_COLOR] = {rojo, verde, azul};
  caracteristicaEstado.writeValue(datos, BYTES_COLOR);
}

// Muestra un color en el Monitor Serial.
void imprimirColor(const char* titulo, byte rojo, byte verde, byte azul) {
  Serial.print(titulo);
  Serial.print(" R=");
  Serial.print(rojo);
  Serial.print(" G=");
  Serial.print(verde);
  Serial.print(" B=");
  Serial.println(azul);
}

// Prueba de arranque: rojo, verde y azul, uno por uno.
void pruebaArranque() {
  ponerColor(255, 0, 0);
  delay(PRUEBA_COLOR_MS);
  ponerColor(0, 255, 0);
  delay(PRUEBA_COLOR_MS);
  ponerColor(0, 0, 255);
  delay(PRUEBA_COLOR_MS);
  ponerColor(0, 0, 0);
}

// Se ejecuta sola cuando un celular se conecta.
void alConectar(BLEDevice central) {
  Serial.print("Celular conectado: ");
  Serial.println(central.address());
  // El LED integrado de la placa indica que hay una conexion.
  digitalWrite(LED_BUILTIN, HIGH);
}

// Se ejecuta sola cuando el celular se desconecta.
void alDesconectar(BLEDevice central) {
  Serial.print("Celular desconectado: ");
  Serial.println(central.address());
  digitalWrite(LED_BUILTIN, LOW);
  // El LED conserva el ultimo color. La placa sigue anunciandose.
}

// Atiende una escritura recibida en la caracteristica COLOR.
void atenderEscrituraColor() {
  // Solo se acepta un color de exactamente 3 bytes.
  if (caracteristicaColor.valueLength() != BYTES_COLOR) {
    Serial.print("Color ignorado: se esperaban 3 bytes y llegaron ");
    Serial.println(caracteristicaColor.valueLength());
    return;
  }

  byte datos[BYTES_COLOR];
  caracteristicaColor.readValue(datos, BYTES_COLOR);
  imprimirColor("Color recibido:", datos[0], datos[1], datos[2]);
  ponerColor(datos[0], datos[1], datos[2]);
}


// ---------------------------------------------------------------
// 4. SETUP
// ---------------------------------------------------------------

void setup() {
  // Inicia el Monitor Serial lo primero para no perder mensajes.
  Serial.begin(9600);

  // Pines del LED RGB y LED integrado como salidas.
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AZUL, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Pausa breve para que el Monitor Serial este listo.
  delay(1500);
  Serial.println("LUZ RGB POR BLUETOOTH (BLE) - ARDUINO UNO R4 WIFI");
  Serial.println("-------------------------------------------------");

  // Inicia el Bluetooth. Si falla, no hay nada mas que hacer.
  if (!BLE.begin()) {
    Serial.println("No se pudo iniciar el Bluetooth. Revise la placa seleccionada.");
    while (true) {
      // Detenido.
    }
  }

  // Nombre visible y servicio que se anuncia.
  BLE.setLocalName(NOMBRE_BLE);
  BLE.setAdvertisedService(servicioLuz);

  // Se agregan las caracteristicas al servicio y este se registra.
  servicioLuz.addCharacteristic(caracteristicaColor);
  servicioLuz.addCharacteristic(caracteristicaEstado);
  BLE.addService(servicioLuz);

  // Funciones que se ejecutan al conectar y al desconectar.
  BLE.setEventHandler(BLEConnected, alConectar);
  BLE.setEventHandler(BLEDisconnected, alDesconectar);

  // Valores iniciales: LED apagado y ESTADO en 0, 0, 0.
  byte apagado[BYTES_COLOR] = {0, 0, 0};
  caracteristicaColor.writeValue(apagado, BYTES_COLOR);
  ponerColor(0, 0, 0);

  // Prueba de arranque de los tres colores.
  pruebaArranque();

  // Hace visible la placa para los celulares.
  BLE.advertise();
  Serial.print("Anunciandose como: ");
  Serial.println(NOMBRE_BLE);
  Serial.print("Direccion de la placa: ");
  Serial.println(BLE.address());
  Serial.print("UUID del servicio: ");
  Serial.println(UUID_SERVICIO);
}


// ---------------------------------------------------------------
// 5. LOOP
// ---------------------------------------------------------------

void loop() {
  // Procesa los eventos Bluetooth: conexiones, lecturas y escrituras.
  BLE.poll();

  // Si el celular escribio un color nuevo, se aplica.
  if (caracteristicaColor.written()) {
    atenderEscrituraColor();
  }
}