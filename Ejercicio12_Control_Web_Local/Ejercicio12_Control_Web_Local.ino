/*
  ============================================================================
  EJERCICIO 12: CONTROL DE UNA SALIDA DESDE UNA PAGINA WEB LOCAL
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - LED con resistencia de 1 kohmio
                - Modulo rele Keyestudio de 5 V (marcas S, V, G)
  ARCHIVOS:     Este programa necesita el archivo "arduino_secrets.h" en la
                misma carpeta (nombre y clave de la red WiFi).

  CONEXIONES:   LED     D8 -> resistencia 1 kohm -> anodo (pata larga)
                        catodo (pata corta) -> GND
                Rele    S -> D7   V -> 5V   G -> GND
                (los contactos COM, NO y NC del rele NO se usan aqui)

  QUE HACE:     La placa se conecta a la red WiFi del aula y crea un SERVIDOR
                WEB en el puerto 80. Desde un celular o una computadora en la
                MISMA red se abre la direccion IP que muestra el Monitor
                Serial y aparece una pagina con los botones Encender y
                Apagar. La pagina tambien muestra el estado actual de la
                salida (LED y rele cambian juntos).

  RUTAS:        /            -> muestra la pagina
                /encender    -> enciende la salida y vuelve a la pagina
                /apagar      -> apaga la salida y vuelve a la pagina
                cualquier otra -> responde 404 (no encontrada)

  SEGURIDAD:    Todo funciona en la red local, sin Internet. Cualquier equipo
                conectado a esa red puede abrir la pagina y pulsar los
                botones: la pagina NO pide usuario ni clave. Use solo cargas
                de baja tension.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

// Biblioteca WiFi de la placa UNO R4 WiFi (viene con "Arduino UNO R4 Boards").
#include <WiFiS3.h>
// Nombre y clave de la red. Estan en otro archivo para no mostrarlos aqui.
#include "arduino_secrets.h"

// Copias de los datos secretos como textos de C.
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

// Pin del LED externo (con resistencia de 1 kohmio).
const int PIN_LED_EXTERNO = 8;

// Pin de senal del modulo rele (terminal S).
const int PIN_RELE = 7;

// Nivel que ENCIENDE el rele. El modulo Keyestudio se activa con HIGH;
// si su modulo se activa con LOW, cambiar estos dos valores entre si.
const int NIVEL_RELE_ENCENDIDO = HIGH;
const int NIVEL_RELE_APAGADO   = LOW;

// Cuantos intentos de conexion se hacen al arrancar antes de seguir.
const int INTENTOS_CONEXION = 5;

// Pausa entre intentos de conexion (ms).
const unsigned long PAUSA_INTENTO_MS = 5000UL;

// Tiempo maximo para recibir la peticion de un cliente (ms).
const unsigned long ESPERA_CLIENTE_MS = 2000UL;

// Cada cuantos ms se comprueba que la red siga conectada.
const unsigned long INTERVALO_REVISION_RED_MS = 10000UL;

// Largo maximo que se guarda de cada linea de la peticion (caracteres).
const unsigned int LARGO_MAXIMO_LINEA = 100;

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

// Servidor web en el puerto 80 (el puerto normal de las paginas web).
WiFiServer servidor(80);

// Estado actual de la salida: true = encendida.
bool salidaEncendida = false;

// Instante (millis) de la ultima revision de la red.
unsigned long ultimaRevisionRed = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS
// ============================================================================

// Pone la salida (LED y rele juntos) en el estado pedido y lo recuerda.
void aplicarSalida(bool encender) {
  digitalWrite(PIN_LED_EXTERNO, encender ? HIGH : LOW);
  digitalWrite(PIN_RELE, encender ? NIVEL_RELE_ENCENDIDO : NIVEL_RELE_APAGADO);
  salidaEncendida = encender;
  Serial.print("Salida: ");
  Serial.println(encender ? "ENCENDIDA" : "APAGADA");
}

// Muestra en el Monitor Serial los datos de la conexion.
void imprimirDatosRed() {
  Serial.print("Red: ");
  Serial.println(WiFi.SSID());
  Serial.print("Senal (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("Direccion IP de la placa: ");
  Serial.println(WiFi.localIP());
  Serial.print("Abra en el navegador: http://");
  Serial.println(WiFi.localIP());
}

// Intenta conectarse a la red. Devuelve true si lo logra.
bool conectarWifi() {
  for (int intento = 1; intento <= INTENTOS_CONEXION; intento++) {
    Serial.print("Conectando a la red (intento ");
    Serial.print(intento);
    Serial.print(" de ");
    Serial.print(INTENTOS_CONEXION);
    Serial.println(")...");
    // Pide la conexion con nombre y clave de la red.
    WiFi.begin(ssid, pass);
    // Da tiempo a que el modulo se conecte.
    delay(PAUSA_INTENTO_MS);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Conectado a la red.");
      return true;
    }
  }
  Serial.println("No se pudo conectar. Revise nombre, clave y que la red sea de 2,4 GHz.");
  return false;
}

// Extrae la ruta de la primera linea de una peticion HTTP.
// Ejemplo: "GET /encender HTTP/1.1" -> "/encender". Devuelve "" si no es GET.
String extraerRuta(const String& linea) {
  if (!linea.startsWith("GET ")) {
    return "";
  }
  // La ruta va entre el primer espacio y el segundo espacio.
  int inicio = 4;
  int fin = linea.indexOf(' ', inicio);
  if (fin < 0) {
    fin = linea.length();
  }
  return linea.substring(inicio, fin);
}

// Envia la pagina con el estado y los dos botones.
void enviarPagina(WiFiClient& cliente) {
  // Encabezado de la respuesta: todo salio bien y lo que sigue es HTML.
  cliente.println("HTTP/1.1 200 OK");
  cliente.println("Content-Type: text/html; charset=UTF-8");
  cliente.println("Connection: close");
  cliente.println();
  // Documento HTML. Se envia en trozos para no armar un texto enorme.
  cliente.println("<!DOCTYPE html><html lang=\"es\"><head>");
  cliente.println("<meta charset=\"UTF-8\">");
  cliente.println("<meta name=\"viewport\" ");
  cliente.println("content=\"width=device-width, initial-scale=1\">");
  cliente.println("<title>Control de salida - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;");
  cliente.println("margin:0;padding:24px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".estado{display:inline-block;padding:14px 28px;");
  cliente.println("border-radius:12px;font-size:22px;font-weight:bold;color:#fff}");
  cliente.println(".on{background:#009CAA}.off{background:#7F8184}");
  cliente.println(".boton{display:block;width:80%;max-width:320px;");
  cliente.println("margin:16px auto;padding:18px;border-radius:12px;");
  cliente.println("font-size:22px;font-weight:bold;text-decoration:none;");
  cliente.println("color:#fff;background:#007A86}");
  cliente.println(".apagar{background:#3A3D40}");
  cliente.println("p{font-size:14px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Control de salida</h1>");
  cliente.println("<p>Arduino UNO R4 WiFi - Curso Arduino UAM</p>");
  // El estado cambia de color y de texto segun la salida.
  if (salidaEncendida) {
    cliente.println("<div class=\"estado on\">Salida ENCENDIDA</div>");
  } else {
    cliente.println("<div class=\"estado off\">Salida APAGADA</div>");
  }
  // Cada boton es un enlace: al tocarlo el navegador pide esa ruta.
  cliente.println("<a class=\"boton\" href=\"/encender\">Encender</a>");
  cliente.println("<a class=\"boton apagar\" href=\"/apagar\">Apagar</a>");
  cliente.println("<p>Red local sin Internet. Cualquier equipo de esta red");
  cliente.println("puede usar estos botones.</p>");
  cliente.println("</body></html>");
}

// Responde "vuelva a la pagina principal" (codigo 303). Asi, si el usuario
// actualiza el navegador, no repite la orden de encender o apagar.
void enviarRedireccion(WiFiClient& cliente) {
  cliente.println("HTTP/1.1 303 See Other");
  cliente.println("Location: /");
  cliente.println("Connection: close");
  cliente.println();
}

// Responde que la ruta pedida no existe (codigo 404).
void enviarNoEncontrada(WiFiClient& cliente) {
  cliente.println("HTTP/1.1 404 Not Found");
  cliente.println("Content-Type: text/plain");
  cliente.println("Connection: close");
  cliente.println();
  cliente.println("Ruta no encontrada");
}

// Atiende a un cliente: lee la peticion, decide y responde.
void atenderCliente(WiFiClient& cliente) {
  String primeraLinea = "";
  String lineaActual = "";
  unsigned long inicio = millis();

  // Lee la peticion caracter por caracter hasta la linea en blanco que
  // termina el encabezado, o hasta agotar el tiempo de espera.
  while (cliente.connected() && (millis() - inicio) < ESPERA_CLIENTE_MS) {
    if (!cliente.available()) {
      continue;
    }
    char c = cliente.read();
    if (c == '\n') {
      // Fin de una linea: la primera es la que contiene la ruta.
      if (primeraLinea.length() == 0) {
        primeraLinea = lineaActual;
      }
      // Una linea vacia marca el fin del encabezado.
      if (lineaActual.length() == 0) {
        break;
      }
      lineaActual = "";
    } else if (c != '\r' && lineaActual.length() < LARGO_MAXIMO_LINEA) {
      lineaActual += c;
    }
  }

  String ruta = extraerRuta(primeraLinea);
  Serial.print("Peticion: ");
  Serial.println(primeraLinea);

  // Decide segun la ruta.
  if (ruta == "/encender") {
    aplicarSalida(true);
    enviarRedireccion(cliente);
  } else if (ruta == "/apagar") {
    aplicarSalida(false);
    enviarRedireccion(cliente);
  } else if (ruta == "/") {
    enviarPagina(cliente);
  } else {
    enviarNoEncontrada(cliente);
  }

  // Cierra la conexion con este cliente.
  delay(1);
  cliente.stop();
}

// ============================================================================
// 5. SETUP
// ============================================================================

void setup() {
  // Inicia el Monitor Serial lo primero para no perder mensajes.
  Serial.begin(9600);

  // Las salidas se configuran y se APAGAN antes de conectar a la red.
  pinMode(PIN_LED_EXTERNO, OUTPUT);
  pinMode(PIN_RELE, OUTPUT);
  aplicarSalida(false);

  // Pausa inicial para que el Monitor Serial reciba los primeros mensajes.
  delay(1500);
  Serial.println("CONTROL DE SALIDA POR PAGINA WEB LOCAL - ARDUINO UNO R4 WIFI");
  Serial.println("------------------------------------------------------------");

  // Comprueba que exista el modulo WiFi de la placa.
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("No se detecta el modulo WiFi. Revise la placa seleccionada.");
    while (true) {
      // Sin modulo WiFi no hay nada mas que hacer.
    }
  }

  // Avisa si el firmware del modulo WiFi es antiguo.
  String version = WiFi.firmwareVersion();
  Serial.print("Firmware del modulo WiFi: ");
  Serial.println(version);
  if (version < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Aviso: hay un firmware mas nuevo (Herramientas > Firmware Updater).");
  }

  // Conecta a la red y, si lo logra, inicia el servidor.
  if (conectarWifi()) {
    servidor.begin();
    imprimirDatosRed();
  }
  ultimaRevisionRed = millis();
}

// ============================================================================
// 6. LOOP
// ============================================================================

void loop() {
  // Cada cierto tiempo comprueba que la red siga conectada.
  if (millis() - ultimaRevisionRed >= INTERVALO_REVISION_RED_MS) {
    ultimaRevisionRed = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Se perdio la conexion a la red. Reintentando...");
      if (conectarWifi()) {
        servidor.begin();
        imprimirDatosRed();
      }
    }
  }

  // Si hay un cliente esperando (un navegador), se atiende.
  WiFiClient cliente = servidor.available();
  if (cliente) {
    atenderCliente(cliente);
  }
}