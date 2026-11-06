/*
  ============================================================================
  EJERCICIO 15: ILUMINACION AUTOMATICA CON CONTROL WIFI
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Modulo fotorresistencia (LDR) Keyestudio (S, V, G)
                - Modulo sensor PIR Keyestudio (S, V, G)
                - Modulo LED de 3 W Keyestudio (S, V, G) con fuente externa
                  de 5 V (1 A o mas)
  ARCHIVOS:     Necesita "arduino_secrets.h" en la misma carpeta (nombre y
                clave de la red WiFi).

  CONEXIONES:   LDR      S -> A0    V -> 5V   G -> GND
                PIR      S -> D2    V -> 5V   G -> GND
                LED 3 W  S -> D6 (PWM)
                         V -> +5 V de la FUENTE EXTERNA
                         G -> negativo de la fuente
                Negativo de la fuente -> GND del Arduino (tierra comun,
                con UN solo cable).

  QUE HACE:     MODO AUTOMATICO: la luz se enciende solo si hay POCA LUZ
                en el ambiente y hay PRESENCIA (movimiento detectado por el
                PIR). Cuanto mas oscuro esta, mas brillo da (PWM).
                Cuando termina el movimiento, espera 10 s y se apaga.
                MODO MANUAL: la pagina web enciende o apaga la luz sin
                mirar el sensor de luz ni el PIR.
                La pagina web (en la misma red WiFi) permite:
                  - cambiar entre modo automatico y manual,
                  - encender y apagar en modo manual,
                  - subir o bajar el umbral de oscuridad,
                  - ver la luz, la presencia, el brillo y cuantas veces
                    se encendio la lampara.

  SEGURIDAD:    La pagina NO pide usuario ni clave. Use la practica solo en
                la red del curso. La lampara es de 5 V; no conecte la red
                electrica.

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

// Pin de la salida (S) del sensor PIR.
const int PIN_PIR = 2;

// Pin analogico de la fotorresistencia (S del modulo).
const int PIN_LDR = A0;

// Pin PWM que controla el brillo del modulo LED de 3 W (terminal S).
// (Se evita el nombre PIN_LED porque la placa ya lo usa.)
const int PIN_LAMPARA = 6;

// Luz: lectura del sensor (0 a 1023) que se toma como 0 % y como 100 %.
// Son valores de ejemplo: calibrelos con SU sensor (ver ejercicio 4).
const int LUZ_OSCURO_BRUTO = 100;
const int LUZ_CLARO_BRUTO  = 900;

// Si es true, la lectura SUBE cuando hay MENOS luz (sensor invertido).
const bool SENSOR_LUZ_INVERTIDO = false;

// Cuantas lecturas se promedian para medir la luz.
const int MUESTRAS_LUZ = 8;

// Umbral de oscuridad (porcentaje de luz). Con menos luz que el umbral
// se considera "oscuro". Se puede cambiar desde la pagina.
const int UMBRAL_INICIAL = 30;
const int UMBRAL_MINIMO  = 10;
const int UMBRAL_MAXIMO  = 90;
const int PASO_UMBRAL    = 5;

// Una vez encendida, la lampara no se apaga hasta que la luz sube este
// margen por encima del umbral (evita parpadeos en el limite).
const int HISTERESIS_LUZ = 10;

// Brillo minimo (porcentaje) cuando la lampara esta encendida en
// automatico: por debajo de este valor casi no se ve.
const int BRILLO_MINIMO = 20;

// En modo manual, la lampara enciende con este brillo (porcentaje).
const int BRILLO_MANUAL = 100;

// Cuanto sube o baja el brillo en cada actualizacion (rampa suave).
const int PASO_RAMPA = 10;

// Tiempo que el PIR necesita para estabilizarse al encender (ms).
const unsigned long ESTABILIZACION_PIR_MS = 30000UL;

// Tiempo que se mantiene la "presencia" despues del ultimo movimiento (ms).
const unsigned long ESPERA_PRESENCIA_MS = 10000UL;

// Cada cuantos ms se actualizan sensores y lampara.
const unsigned long INTERVALO_ACTUALIZACION_MS = 200UL;

// Cada cuantos ms se escribe un resumen en el Monitor Serial.
const unsigned long INTERVALO_RESUMEN_MS = 5000UL;

// Cada cuantos segundos el navegador recarga la pagina solo.
const int SEGUNDOS_RECARGA = 3;

// Modos de funcionamiento.
const int MODO_AUTOMATICO = 0;
const int MODO_MANUAL     = 1;

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

// Servidor web en el puerto 80.
WiFiServer servidor(80);

// Modo actual y estado de la lampara en modo manual.
int modo = MODO_AUTOMATICO;
bool manualEncendida = false;

// Umbral de oscuridad actual (porcentaje de luz).
int umbral = UMBRAL_INICIAL;

// Ultimas mediciones.
int luzPorcentaje = 0;
bool pirActivo = false;

// Instante (millis) del ultimo movimiento detectado y bandera de presencia.
unsigned long ultimoMovimiento = 0;
bool hayPresencia = false;

// Brillo actual de la lampara (0 a 100 %) y numero de encendidos.
int brilloActual = 0;
unsigned long vecesEncendida = 0;

// Instantes (millis) de las ultimas tareas periodicas.
unsigned long ultimaActualizacion = 0;
unsigned long ultimoResumen = 0;
unsigned long ultimaRevisionRed = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: SENSORES Y LAMPARA
// ============================================================================

// true mientras el PIR se estabiliza despues de encender la placa.
bool pirEstabilizando() {
  return millis() < ESTABILIZACION_PIR_MS;
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

// Aplica un brillo de 0 a 100 % al LED de 3 W con PWM.
void aplicarBrillo(int porcentaje) {
  int valor = map(porcentaje, 0, 100, 0, 255);
  analogWrite(PIN_LAMPARA, valor);
}

// Brillo que pide el modo automatico (0 = apagada).
int brilloObjetivoAutomatico() {
  // Sin presencia no se enciende.
  if (!hayPresencia) {
    return 0;
  }
  // Si ya esta encendida, se tolera un poco mas de luz antes de apagar.
  int limite = umbral;
  if (brilloActual > 0) {
    limite = umbral + HISTERESIS_LUZ;
  }
  if (luzPorcentaje >= limite) {
    return 0;
  }
  // Cuanto mas oscuro, mas brillo: de BRILLO_MINIMO (en el umbral) a 100 %.
  int brillo = map(luzPorcentaje, umbral, 0, BRILLO_MINIMO, 100);
  return constrain(brillo, BRILLO_MINIMO, 100);
}

// Brillo que pide el modo manual.
int brilloObjetivoManual() {
  if (manualEncendida) {
    return BRILLO_MANUAL;
  }
  return 0;
}

// Texto del modo para la pagina y el Monitor Serial.
const char* textoModo() {
  if (modo == MODO_AUTOMATICO) {
    return "AUTOMATICO";
  }
  return "MANUAL";
}

// Texto de la presencia para la pagina y el Monitor Serial.
const char* textoPresencia() {
  if (pirEstabilizando()) {
    return "ESTABILIZANDO";
  }
  if (hayPresencia) {
    return "SI";
  }
  return "NO";
}

// Lee los sensores, decide el brillo y lo aplica con una rampa suave.
void actualizarLampara() {
  luzPorcentaje = leerLuzPorcentaje();

  // Presencia: el PIR en HIGH la activa y se mantiene un tiempo despues.
  // Durante la estabilizacion se ignoran sus lecturas.
  bool presenciaAnterior = hayPresencia;
  if (pirEstabilizando()) {
    pirActivo = false;
    hayPresencia = false;
  } else {
    pirActivo = (digitalRead(PIN_PIR) == HIGH);
    if (pirActivo) {
      ultimoMovimiento = millis();
      hayPresencia = true;
    } else if (hayPresencia && (millis() - ultimoMovimiento) >= ESPERA_PRESENCIA_MS) {
      hayPresencia = false;
    }
  }
  if (hayPresencia != presenciaAnterior) {
    Serial.print("Presencia: ");
    Serial.println(hayPresencia ? "SI" : "NO");
  }

  // Brillo que pide el modo actual.
  int objetivo;
  if (modo == MODO_AUTOMATICO) {
    objetivo = brilloObjetivoAutomatico();
  } else {
    objetivo = brilloObjetivoManual();
  }

  // Rampa: el brillo se acerca al objetivo de a PASO_RAMPA por vez.
  int anterior = brilloActual;
  if (brilloActual < objetivo) {
    brilloActual = min(brilloActual + PASO_RAMPA, objetivo);
  } else if (brilloActual > objetivo) {
    brilloActual = max(brilloActual - PASO_RAMPA, objetivo);
  }
  aplicarBrillo(brilloActual);

  // Cada paso de apagada a encendida cuenta como un encendido.
  if (anterior == 0 && brilloActual > 0) {
    vecesEncendida++;
    Serial.print("Lampara ENCENDIDA (encendido numero ");
    Serial.print(vecesEncendida);
    Serial.println(")");
  }
  if (anterior > 0 && brilloActual == 0) {
    Serial.println("Lampara APAGADA");
  }
}

// Escribe un resumen del estado en el Monitor Serial.
void imprimirResumen() {
  Serial.print("Modo: ");
  Serial.print(textoModo());
  Serial.print(" | Luz: ");
  Serial.print(luzPorcentaje);
  Serial.print(" % | Umbral: ");
  Serial.print(umbral);
  Serial.print(" % | Presencia: ");
  Serial.print(textoPresencia());
  Serial.print(" | Brillo: ");
  Serial.print(brilloActual);
  Serial.println(" %");
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
// Ejemplo: "GET /auto HTTP/1.1" -> "/auto". Devuelve "" si no es GET.
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

// Envia la pagina de control.
void enviarPagina(WiFiClient& cliente) {
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
  cliente.println("<title>Iluminacion automatica - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;");
  cliente.println("margin:0;padding:20px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".tarjeta{max-width:320px;margin:12px auto;padding:14px;");
  cliente.println("border-radius:12px;background:#E3F4F6}");
  cliente.println(".tarjeta h2{margin:0;font-size:15px;color:#00606A}");
  cliente.println(".valor{font-size:30px;font-weight:bold;color:#007A86}");
  cliente.println(".on .valor{color:#B07A00}");
  cliente.println(".boton{display:inline-block;margin:6px;padding:12px 18px;");
  cliente.println("border-radius:10px;background:#007A86;color:#fff;");
  cliente.println("text-decoration:none;font-size:16px}");
  cliente.println(".gris{background:#7F8184}");
  cliente.println("p{font-size:13px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Iluminacion automatica</h1>");
  cliente.println("<p>Arduino UNO R4 WiFi - Curso Arduino UAM</p>");

  // Tarjeta del modo.
  cliente.print("<div class=\"tarjeta\"><h2>Modo</h2><div class=\"valor\">");
  cliente.print(textoModo());
  cliente.println("</div></div>");

  // Tarjeta de la lampara (cambia de color si esta encendida).
  cliente.print("<div class=\"tarjeta");
  if (brilloActual > 0) {
    cliente.print(" on");
  }
  cliente.print("\"><h2>Lampara</h2><div class=\"valor\">");
  if (brilloActual > 0) {
    cliente.print("ENCENDIDA ");
    cliente.print(brilloActual);
    cliente.print(" %");
  } else {
    cliente.print("APAGADA");
  }
  cliente.println("</div></div>");

  // Tarjeta de la luz y el umbral.
  cliente.print("<div class=\"tarjeta\"><h2>Luz ambiente</h2><div class=\"valor\">");
  cliente.print(luzPorcentaje);
  cliente.print(" %</div><div>Umbral de oscuridad: ");
  cliente.print(umbral);
  cliente.println(" %</div></div>");

  // Tarjeta de presencia.
  cliente.print("<div class=\"tarjeta\"><h2>Presencia</h2><div class=\"valor\">");
  cliente.print(textoPresencia());
  cliente.println("</div></div>");

  // Contador de encendidos.
  cliente.print("<div class=\"tarjeta\"><h2>Encendidos desde el arranque</h2>");
  cliente.print("<div class=\"valor\">");
  cliente.print(vecesEncendida);
  cliente.println("</div></div>");

  // Botones de modo y de encendido manual.
  cliente.println("<div>");
  cliente.println("<a class=\"boton\" href=\"/auto\">Automatico</a>");
  cliente.println("<a class=\"boton\" href=\"/encender\">Manual: encender</a>");
  cliente.println("<a class=\"boton gris\" href=\"/apagar\">Manual: apagar</a>");
  cliente.println("</div><div>");
  // Botones del umbral.
  cliente.println("<a class=\"boton gris\" href=\"/umbral/menos\">Umbral -</a>");
  cliente.println("<a class=\"boton gris\" href=\"/umbral/mas\">Umbral +</a>");
  cliente.println("</div>");

  cliente.print("<p>La pagina se actualiza sola cada ");
  cliente.print(SEGUNDOS_RECARGA);
  cliente.println(" s. Red local sin Internet.</p>");
  cliente.println("</body></html>");
}

// Responde con una redireccion a la pagina principal (codigo 303).
// Asi, al actualizar el navegador no se repite la orden.
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

  // Cada ruta hace una cosa distinta; cualquier otra da 404.
  if (ruta == "/") {
    enviarPagina(cliente);
  } else if (ruta == "/auto") {
    modo = MODO_AUTOMATICO;
    Serial.println("Modo: AUTOMATICO");
    enviarRedireccion(cliente);
  } else if (ruta == "/encender") {
    modo = MODO_MANUAL;
    manualEncendida = true;
    Serial.println("Modo: MANUAL, lampara pedida ENCENDIDA");
    enviarRedireccion(cliente);
  } else if (ruta == "/apagar") {
    modo = MODO_MANUAL;
    manualEncendida = false;
    Serial.println("Modo: MANUAL, lampara pedida APAGADA");
    enviarRedireccion(cliente);
  } else if (ruta == "/umbral/mas") {
    umbral = min(umbral + PASO_UMBRAL, UMBRAL_MAXIMO);
    Serial.print("Umbral: ");
    Serial.println(umbral);
    enviarRedireccion(cliente);
  } else if (ruta == "/umbral/menos") {
    umbral = max(umbral - PASO_UMBRAL, UMBRAL_MINIMO);
    Serial.print("Umbral: ");
    Serial.println(umbral);
    enviarRedireccion(cliente);
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

  // La lampara empieza apagada antes de cualquier otra cosa.
  pinMode(PIN_LAMPARA, OUTPUT);
  aplicarBrillo(0);
  pinMode(PIN_PIR, INPUT);

  // Pausa inicial: el Monitor Serial necesita un momento.
  delay(1500);
  Serial.println("ILUMINACION AUTOMATICA CON CONTROL WIFI - ARDUINO UNO R4 WIFI");
  Serial.println("--------------------------------------------------------------");
  Serial.println("Estabilizando el sensor PIR (30 s). No se mueva frente a el.");

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
// 7. LOOP
// ============================================================================

void loop() {
  // Cada 200 ms se leen los sensores y se actualiza la lampara.
  if (millis() - ultimaActualizacion >= INTERVALO_ACTUALIZACION_MS) {
    ultimaActualizacion = millis();
    actualizarLampara();
  }

  // Cada 5 s se escribe un resumen en el Monitor Serial.
  if (millis() - ultimoResumen >= INTERVALO_RESUMEN_MS) {
    ultimoResumen = millis();
    imprimirResumen();
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