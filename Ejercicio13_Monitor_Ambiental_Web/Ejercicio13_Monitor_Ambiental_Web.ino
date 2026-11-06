/*
  ============================================================================
  EJERCICIO 13: MONITOR AMBIENTAL EN EL NAVEGADOR
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor DHT11 de temperatura y humedad (modulo S, V, G)
                - Modulo fotorresistencia (LDR) Keyestudio (S, V, G)
                - Modulo semaforo Keyestudio (GND, G, Y, R)
  BIBLIOTECA:   "DHT sensor library" de Adafruit (y "Adafruit Unified Sensor").
  ARCHIVOS:     Necesita "arduino_secrets.h" en la misma carpeta (nombre y
                clave de la red WiFi).

  CONEXIONES:   DHT11    S -> D2    V -> 5V   G -> GND
                LDR      S -> A0    V -> 5V   G -> GND
                Semaforo G (verde) -> D3   Y (amarillo) -> D4
                         R (rojo)  -> D5   GND -> GND

  QUE HACE:     La placa se conecta a la red WiFi del aula y crea un SERVIDOR
                WEB. Desde un celular o computadora en la MISMA red se abre la
                direccion IP que muestra el Monitor Serial y aparece una
                pagina con la temperatura, la humedad y el nivel de luz.
                La pagina se vuelve a cargar sola cada 5 segundos.

  SEMAFORO:     Segun la TEMPERATURA (umbral de alerta = 30 C):
                  VERDE    menos de 28 C   -> normal
                  AMARILLO de 28 C a 30 C  -> atencion
                  ROJO     mas de 30 C     -> alerta
                La pagina resalta en rojo el valor que supera el umbral.

  SEGURIDAD:    Todo funciona en la red local, sin Internet. La pagina solo
                MUESTRA datos (no cambia nada en la placa), pero cualquier
                equipo de la red puede verla.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

// Biblioteca WiFi de la placa UNO R4 WiFi (viene con "Arduino UNO R4 Boards").
#include <WiFiS3.h>
// Biblioteca del sensor DHT11 (Adafruit).
#include <DHT.h>
// Nombre y clave de la red. Estan en otro archivo para no mostrarlos aqui.
#include "arduino_secrets.h"

// Copias de los datos secretos como textos de C.
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

// Pin de datos del DHT11 (terminal S del modulo).
const int PIN_DHT = 2;

// Pin analogico de la fotorresistencia (terminal S del modulo).
const int PIN_LDR = A0;

// Pines del semaforo: verde, amarillo y rojo.
const int PIN_VERDE    = 3;
const int PIN_AMARILLO = 4;
const int PIN_ROJO     = 5;

// Umbrales de temperatura (grados Celsius).
// Rojo: la temperatura SUPERA el umbral de alerta.
const float UMBRAL_ALERTA_C = 30.0;
// Amarillo: desde este valor hasta el umbral de alerta (inclusive).
// Esta 2 C por debajo porque el DHT11 tiene una exactitud de +-2 C.
const float UMBRAL_ATENCION_C = 28.0;

// Niveles de estado (se usan como numeros).
const int NIVEL_NORMAL   = 0;
const int NIVEL_ATENCION = 1;
const int NIVEL_ALERTA   = 2;

// Luz: lectura del sensor (0 a 1023) que se toma como 0 % y como 100 %.
// Son valores de ejemplo: calibrelos con SU sensor (ver ejercicio 4).
const int LUZ_OSCURO_BRUTO = 100;
const int LUZ_CLARO_BRUTO  = 900;

// Si es true, la lectura SUBE cuando hay MENOS luz (sensor invertido).
const bool SENSOR_LUZ_INVERTIDO = false;

// Cuantas lecturas se promedian para medir la luz.
const int MUESTRAS_LUZ = 8;

// Limites para clasificar la luz (en porcentaje).
const int LUZ_PENUMBRA_PORC = 25;
const int LUZ_CLARO_PORC    = 60;

// Cada cuantos ms se miden los sensores (el DHT11 pide al menos 1 s).
const unsigned long INTERVALO_MEDICION_MS = 2000UL;

// Cuantas lecturas fallidas seguidas se aceptan antes de declarar "sin datos".
const int FALLOS_MAXIMOS = 3;

// Cada cuantos segundos el navegador recarga la pagina solo.
const int SEGUNDOS_RECARGA = 5;

// Intentos y pausa para conectarse a la red.
const int INTENTOS_CONEXION = 5;
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

// Sensor DHT11.
DHT sensor(PIN_DHT, DHT11);

// Servidor web en el puerto 80.
WiFiServer servidor(80);

// Ultimos valores medidos que muestra la pagina.
float temperatura = 0.0;
float humedad = 0.0;
int luzPorcentaje = 0;

// true si la ultima medicion del DHT11 fue valida.
bool datosValidos = false;

// Lecturas fallidas seguidas del DHT11.
int fallos = 0;

// Instantes (millis) de la ultima medicion y de la ultima lectura valida.
unsigned long ultimaMedicion = 0;
unsigned long ultimaLecturaValida = 0;

// Instante (millis) de la ultima revision de la red.
unsigned long ultimaRevisionRed = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: SENSORES Y SEMAFORO
// ============================================================================

// Decide el nivel segun la temperatura.
int nivelTemperatura(float t) {
  if (t > UMBRAL_ALERTA_C) {
    return NIVEL_ALERTA;
  }
  if (t >= UMBRAL_ATENCION_C) {
    return NIVEL_ATENCION;
  }
  return NIVEL_NORMAL;
}

// Texto del nivel para la pagina y el Monitor Serial.
const char* textoNivel(int nivel) {
  if (nivel == NIVEL_ALERTA) {
    return "ALERTA";
  }
  if (nivel == NIVEL_ATENCION) {
    return "ATENCION";
  }
  return "NORMAL";
}

// Clase de estilo de la pagina (cada una tiene su color en el CSS).
const char* claseNivel(int nivel) {
  if (nivel == NIVEL_ALERTA) {
    return "alerta";
  }
  if (nivel == NIVEL_ATENCION) {
    return "atencion";
  }
  return "normal";
}

// Clasifica la luz en texto.
const char* etiquetaLuz(int porcentaje) {
  if (porcentaje < LUZ_PENUMBRA_PORC) {
    return "Oscuro";
  }
  if (porcentaje < LUZ_CLARO_PORC) {
    return "Penumbra";
  }
  return "Claro";
}

// Enciende solo la luz del semaforo que corresponde al nivel.
// Con nivel -1 se apagan las tres (sin datos).
void mostrarSemaforo(int nivel) {
  digitalWrite(PIN_VERDE,    nivel == NIVEL_NORMAL   ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, nivel == NIVEL_ATENCION ? HIGH : LOW);
  digitalWrite(PIN_ROJO,     nivel == NIVEL_ALERTA   ? HIGH : LOW);
}

// Mide la luz promediando varias lecturas y la devuelve de 0 a 100 %.
int leerLuzPorcentaje() {
  long suma = 0;
  for (int i = 0; i < MUESTRAS_LUZ; i++) {
    suma += analogRead(PIN_LDR);
  }
  int bruto = suma / MUESTRAS_LUZ;
  // Si el sensor esta invertido, se da la vuelta a la lectura.
  if (SENSOR_LUZ_INVERTIDO) {
    bruto = 1023 - bruto;
  }
  // Se pasa del rango de calibracion a 0..100 y se limita.
  int porcentaje = map(bruto, LUZ_OSCURO_BRUTO, LUZ_CLARO_BRUTO, 0, 100);
  return constrain(porcentaje, 0, 100);
}

// Mide temperatura, humedad y luz; actualiza el semaforo.
void medirSensores() {
  float t = sensor.readTemperature();
  float h = sensor.readHumidity();
  luzPorcentaje = leerLuzPorcentaje();

  // La lectura del DHT11 no es valida si alguno de los dos es "no numero".
  if (isnan(t) || isnan(h)) {
    fallos++;
    Serial.print("Error al leer el DHT11 (");
    Serial.print(fallos);
    Serial.println(" seguidos)");
    // Con varios fallos seguidos se declara "sin datos" y se apaga el semaforo.
    if (fallos >= FALLOS_MAXIMOS) {
      datosValidos = false;
      mostrarSemaforo(-1);
    }
    return;
  }

  // Lectura valida: se guarda y se actualiza el semaforo.
  fallos = 0;
  datosValidos = true;
  temperatura = t;
  humedad = h;
  ultimaLecturaValida = millis();
  int nivel = nivelTemperatura(temperatura);
  mostrarSemaforo(nivel);

  Serial.print("Temp: ");
  Serial.print(temperatura, 1);
  Serial.print(" C | Hum: ");
  Serial.print(humedad, 0);
  Serial.print(" % | Luz: ");
  Serial.print(luzPorcentaje);
  Serial.print(" % | Estado: ");
  Serial.println(textoNivel(nivel));
}

// ============================================================================
// 5. FUNCIONES PROPIAS: RED Y PAGINA WEB
// ============================================================================

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
    WiFi.begin(ssid, pass);
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
// Ejemplo: "GET / HTTP/1.1" -> "/". Devuelve "" si no es GET.
String extraerRuta(const String& linea) {
  if (!linea.startsWith("GET ")) {
    return "";
  }
  int inicio = 4;
  int fin = linea.indexOf(' ', inicio);
  if (fin < 0) {
    fin = linea.length();
  }
  return linea.substring(inicio, fin);
}

// Envia la pagina con las tres mediciones.
void enviarPagina(WiFiClient& cliente) {
  int nivel = nivelTemperatura(temperatura);

  // Encabezado de la respuesta.
  cliente.println("HTTP/1.1 200 OK");
  cliente.println("Content-Type: text/html; charset=UTF-8");
  cliente.println("Connection: close");
  cliente.println();

  // Documento HTML. La linea "refresh" hace que el navegador recargue solo.
  cliente.println("<!DOCTYPE html><html lang=\"es\"><head>");
  cliente.println("<meta charset=\"UTF-8\">");
  cliente.println("<meta name=\"viewport\" ");
  cliente.println("content=\"width=device-width, initial-scale=1\">");
  cliente.print("<meta http-equiv=\"refresh\" content=\"");
  cliente.print(SEGUNDOS_RECARGA);
  cliente.println("\">");
  cliente.println("<title>Monitor ambiental - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;");
  cliente.println("margin:0;padding:20px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".tarjeta{display:block;max-width:320px;margin:14px auto;");
  cliente.println("padding:16px;border-radius:12px;background:#E3F4F6}");
  cliente.println(".tarjeta h2{margin:0;font-size:16px;color:#00606A}");
  cliente.println(".valor{font-size:40px;font-weight:bold;color:#007A86}");
  cliente.println(".atencion .valor{color:#B07A00}");
  cliente.println(".alerta{background:#FBE3E3}");
  cliente.println(".alerta .valor{color:#C62828}");
  cliente.println(".sin{background:#EEEEEE}.sin .valor{color:#7F8184}");
  cliente.println("p{font-size:14px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Monitor ambiental</h1>");
  cliente.println("<p>Arduino UNO R4 WiFi - Curso Arduino UAM</p>");

  if (datosValidos) {
    // Tarjeta de temperatura: su clase cambia el color segun el nivel.
    cliente.print("<div class=\"tarjeta ");
    cliente.print(claseNivel(nivel));
    cliente.println("\"><h2>Temperatura</h2>");
    cliente.print("<div class=\"valor\">");
    cliente.print(temperatura, 1);
    cliente.print(" &deg;C</div><div>Estado: ");
    cliente.print(textoNivel(nivel));
    cliente.println("</div></div>");

    // Tarjeta de humedad.
    cliente.println("<div class=\"tarjeta\"><h2>Humedad</h2>");
    cliente.print("<div class=\"valor\">");
    cliente.print(humedad, 0);
    cliente.println(" %</div></div>");
  } else {
    // Sin lectura valida del DHT11.
    cliente.println("<div class=\"tarjeta sin\"><h2>Temperatura y humedad</h2>");
    cliente.println("<div class=\"valor\">--</div>");
    cliente.println("<div>Sin lectura del DHT11</div></div>");
  }

  // Tarjeta de luz (siempre disponible).
  cliente.println("<div class=\"tarjeta\"><h2>Luz</h2>");
  cliente.print("<div class=\"valor\">");
  cliente.print(luzPorcentaje);
  cliente.print(" %</div><div>");
  cliente.print(etiquetaLuz(luzPorcentaje));
  cliente.println("</div></div>");

  // Pie: cuanto hace de la ultima lectura valida.
  if (datosValidos) {
    cliente.print("<p>Ultima lectura valida hace ");
    cliente.print((millis() - ultimaLecturaValida) / 1000UL);
    cliente.println(" s.</p>");
  }
  cliente.print("<p>La pagina se actualiza sola cada ");
  cliente.print(SEGUNDOS_RECARGA);
  cliente.println(" s. Red local sin Internet.</p>");
  cliente.println("</body></html>");
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

  // Lee la peticion hasta la linea en blanco o hasta agotar el tiempo.
  while (cliente.connected() && (millis() - inicio) < ESPERA_CLIENTE_MS) {
    if (!cliente.available()) {
      continue;
    }
    char c = cliente.read();
    if (c == '\n') {
      if (primeraLinea.length() == 0) {
        primeraLinea = lineaActual;
      }
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

  // Solo existe la pagina principal; cualquier otra ruta da 404.
  if (ruta == "/") {
    enviarPagina(cliente);
  } else {
    enviarNoEncontrada(cliente);
  }

  delay(1);
  cliente.stop();
}

// ============================================================================
// 6. SETUP
// ============================================================================

void setup() {
  // Inicia el Monitor Serial lo primero para no perder mensajes.
  Serial.begin(9600);

  // Semaforo: pines como salida y todo apagado.
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  mostrarSemaforo(-1);

  // Inicia el sensor DHT11.
  sensor.begin();

  // Pausa inicial: el Monitor Serial y el DHT11 necesitan un momento.
  delay(1500);
  Serial.println("MONITOR AMBIENTAL EN EL NAVEGADOR - ARDUINO UNO R4 WIFI");
  Serial.println("-------------------------------------------------------");

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

  // Primera medicion antes de conectar, para que la pagina tenga datos.
  medirSensores();
  ultimaMedicion = millis();

  // Conecta a la red y, si lo logra, inicia el servidor.
  if (conectarWifi()) {
    servidor.begin();
    imprimirDatosRed();
  }
  ultimaRevisionRed = millis();
}

// ============================================================================
// 7. LOOP
// ============================================================================

void loop() {
  // Cada 2 s se miden los sensores (sin usar delay()).
  if (millis() - ultimaMedicion >= INTERVALO_MEDICION_MS) {
    ultimaMedicion = millis();
    medirSensores();
  }

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