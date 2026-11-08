/*
  ============================================================================
  EJERCICIO 18: INVERNADERO O JARDIN INTELIGENTE
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor de humedad de suelo (analogico, marcas G, V, S)
                - Sensor DHT11 de temperatura y humedad (modulo S, V, G)
                - Modulo fotorresistencia (LDR) Keyestudio (S, V, G)
                - Modulo rele Keyestudio de 5 V (S, V, G): la BOMBA de riego
                  (en la maqueta se simula con el modulo LED de 3 W)
                - Sensor de nivel de agua (analogico, S, V, G) del deposito
                - Pantalla LCD 16x2 con adaptador I2C (GND, VCC, SDA, SCL)
  BIBLIOTECAS:  "DHT sensor library" de Adafruit (y "Adafruit Unified Sensor"),
                "hd44780" de Bill Perry. WiFiS3 viene con la placa.
  ARCHIVOS:     Necesita "arduino_secrets.h" en la misma carpeta (nombre y
                clave de la red WiFi) para la pagina web.

  CONEXIONES:   Suelo      S -> A1   V -> 5V   G -> GND
                LDR        S -> A0   V -> 5V   G -> GND
                Nivel agua S -> A2   V -> 5V   G -> GND
                DHT11      S -> D2   V -> 5V   G -> GND
                Rele       S -> D7   V -> 5V   G -> GND
                Contactos del rele (lado de la carga, NO van al Arduino):
                  fuente de la carga (+) -> COM del rele
                  NO del rele            -> + de la bomba o del modulo LED
                LCD I2C    SDA -> A4   SCL -> A5   VCC -> 5V   GND -> GND

  QUE HACE:     - Modo AUTOMATICO: el riego se enciende cuando el suelo baja
                  de UMBRAL_SECO_PORC y se apaga cuando sube de
                  UMBRAL_HUMEDO_PORC (histeresis: dos umbrales distintos).
                - Proteccion: la bomba no corre mas de RIEGO_MAXIMO_MS seguidos
                  y luego descansa DESCANSO_MS para que el agua se absorba.
                - Bloqueo: si el nivel de agua del deposito esta bajo, la
                  bomba no se enciende (ni en automatico ni en manual).
                - Modo MANUAL: desde la pagina web se enciende y apaga el
                  riego a voluntad (con los mismos limites de seguridad).
                - La LCD muestra suelo, luz, temperatura y humedad del aire.
                - La pagina web muestra todo y permite cambiar el modo.
                - Sin WiFi el invernadero funciona igual, pero sin pagina.

  IMPORTANTE:   Es una maqueta educativa. La pagina no pide usuario ni clave.
                Los valores de calibracion (SUELO_*, NIVEL_*, LUZ_*) son de
                ejemplo: calibrelos con su sensor. No conecte cargas de red
                electrica (110/220 V) en las pruebas de aula.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

#include <Wire.h>
#include <DHT.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <WiFiS3.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_DHT   = 2;    // DHT11 (senal S).
const int PIN_RELE  = 7;    // Rele de la bomba (senal S).
const int PIN_LDR   = A0;   // Fotorresistencia.
const int PIN_SUELO = A1;   // Humedad de suelo.
const int PIN_NIVEL = A2;   // Nivel de agua del deposito.

// Nivel que ENCIENDE el rele. El modulo Keyestudio se activa con HIGH;
// si su modulo se activa con LOW, cambie estos dos valores entre si.
const int NIVEL_RELE_ENCENDIDO = HIGH;
const int NIVEL_RELE_APAGADO   = LOW;

// ---- Suelo: lectura bruta (0 a 1023) que se toma como 0 % y como 100 % ----
// Con el sensor al aire (seco) y dentro de un vaso con agua (mojado) anote
// los valores del Monitor Serial y escribalos aqui. Si su sensor da MAS
// valor con el suelo SECO, deje SUELO_SECO_BRUTO mayor que SUELO_MOJADO_BRUTO.
const int SUELO_SECO_BRUTO   = 50;
const int SUELO_MOJADO_BRUTO = 600;

// Histeresis del riego (en % de humedad del suelo).
const int UMBRAL_SECO_PORC   = 35;   // Debajo de este valor se enciende.
const int UMBRAL_HUMEDO_PORC = 55;   // Por encima de este valor se apaga.

// Proteccion de la bomba.
const unsigned long RIEGO_MAXIMO_MS = 20000UL;  // Maximo seguido: 20 s.
const unsigned long DESCANSO_MS     = 60000UL;  // Descanso obligatorio: 60 s.

// ---- Nivel de agua del deposito ----
// Si no tiene el sensor de nivel, ponga USAR_SENSOR_NIVEL en false.
const bool USAR_SENSOR_NIVEL = true;
const int NIVEL_VACIO_BRUTO = 50;     // Lectura con el sensor seco.
const int NIVEL_LLENO_BRUTO = 500;    // Lectura con el sensor sumergido.
const int NIVEL_BAJO_PORC   = 20;     // Debajo de esto: bloqueo.
const int NIVEL_OK_PORC     = 30;     // Por encima de esto: se desbloquea.

// ---- Luz: lectura bruta que se toma como 0 % y como 100 % ----
const int LUZ_OSCURO_BRUTO = 100;
const int LUZ_CLARO_BRUTO  = 900;
// Si es true, la lectura SUBE cuando hay MENOS luz (sensor invertido).
const bool SENSOR_LUZ_INVERTIDO = false;

// Cuantas lecturas se promedian en cada medicion analogica.
const int MUESTRAS = 8;

// Cada cuantos ms se mide (el DHT11 pide al menos 1 s).
const unsigned long INTERVALO_MEDIDA_MS = 2000UL;
// Lecturas fallidas seguidas del DHT11 para dar el error por confirmado.
const int FALLOS_MAXIMOS = 3;

// Modos de trabajo.
const int MODO_AUTOMATICO = 0;
const int MODO_MANUAL     = 1;

// Cada cuantos ms se escribe un resumen en el Monitor Serial.
const unsigned long INTERVALO_RESUMEN_MS = 10000UL;

// Pagina web.
const int SEGUNDOS_RECARGA = 5;
const int INTENTOS_CONEXION = 3;
const unsigned long PAUSA_INTENTO_MS = 5000UL;
const unsigned long ESPERA_CLIENTE_MS = 500UL;
const unsigned long INTERVALO_REVISION_RED_MS = 10000UL;
const unsigned int LARGO_MAXIMO_LINEA = 100;

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

DHT sensorDht(PIN_DHT, DHT11);
hd44780_I2Cexp lcd;
bool lcdDisponible = false;
String filaActual[2] = {"", ""};
WiFiServer servidor(80);

// Mediciones.
int sueloPorc = 0;
int luzPorc = 0;
int nivelPorc = 100;
float temperatura = 0.0;
float humedadAire = 0.0;
bool dhtValido = false;
int fallosDht = 0;
unsigned long ultimaMedida = 0;

// Control.
int modo = MODO_AUTOMATICO;
bool bombaEncendida = false;
bool bloqueoAgua = false;
unsigned long inicioRiego = 0;
unsigned long finRiego = 0;
bool huboRiego = false;          // true cuando ya hubo un riego (hay descanso).
unsigned long riegosTotales = 0;

// Mensajes.
String ultimoEvento = "Ninguno";
unsigned long tiempoUltimoEvento = 0;

unsigned long ultimoResumen = 0;
unsigned long ultimaRevisionRed = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: MEDICION
// ============================================================================

// Anota el ultimo evento (para la pagina y el Monitor Serial).
void registrarEvento(String texto) {
  ultimoEvento = texto;
  tiempoUltimoEvento = millis();
  Serial.print("Evento: ");
  Serial.println(texto);
}

// Promedia varias lecturas analogicas de un pin.
int leerPromedio(int pin) {
  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    suma += analogRead(pin);
  }
  return (int)(suma / MUESTRAS);
}

// Convierte una lectura bruta en porcentaje de 0 a 100.
// map() funciona aunque "seco" sea mayor que "mojado".
int aPorcentaje(int bruto, int brutoCero, int brutoCien) {
  int porcentaje = map(bruto, brutoCero, brutoCien, 0, 100);
  return constrain(porcentaje, 0, 100);
}

// Mide suelo, luz, nivel de agua, temperatura y humedad del aire.
void medirTodo() {
  int brutoSuelo = leerPromedio(PIN_SUELO);
  sueloPorc = aPorcentaje(brutoSuelo, SUELO_SECO_BRUTO, SUELO_MOJADO_BRUTO);

  int brutoLuz = leerPromedio(PIN_LDR);
  if (SENSOR_LUZ_INVERTIDO) {
    brutoLuz = 1023 - brutoLuz;
  }
  luzPorc = aPorcentaje(brutoLuz, LUZ_OSCURO_BRUTO, LUZ_CLARO_BRUTO);

  if (USAR_SENSOR_NIVEL) {
    nivelPorc = aPorcentaje(leerPromedio(PIN_NIVEL), NIVEL_VACIO_BRUTO, NIVEL_LLENO_BRUTO);
  } else {
    nivelPorc = 100;
  }

  // DHT11: la lectura no es valida si alguno de los dos es "no numero".
  float t = sensorDht.readTemperature();
  float h = sensorDht.readHumidity();
  if (isnan(t) || isnan(h)) {
    fallosDht++;
    if (fallosDht >= FALLOS_MAXIMOS && dhtValido) {
      dhtValido = false;
      registrarEvento("Error al leer el DHT11");
    }
  } else {
    fallosDht = 0;
    if (!dhtValido) {
      registrarEvento("DHT11 funcionando");
    }
    dhtValido = true;
    temperatura = t;
    humedadAire = h;
  }

  Serial.print("Medicion | suelo bruto: ");
  Serial.print(brutoSuelo);
  Serial.print(" | luz bruta: ");
  Serial.println(brutoLuz);
}

// ============================================================================
// 5. FUNCIONES PROPIAS: BOMBA Y CONTROL DEL RIEGO
// ============================================================================

// Pone el rele en el nivel que corresponde.
void ponerBomba(bool encender) {
  digitalWrite(PIN_RELE, encender ? NIVEL_RELE_ENCENDIDO : NIVEL_RELE_APAGADO);
  bombaEncendida = encender;
}

// Indica si se puede encender la bomba ahora (sin escribir mensajes).
bool puedeRegar() {
  if (bloqueoAgua) {
    return false;
  }
  if (huboRiego && (millis() - finRiego) < DESCANSO_MS) {
    return false;
  }
  return true;
}

// Enciende la bomba (se debe llamar solo si puedeRegar() es true).
void encenderRiego(String motivo) {
  ponerBomba(true);
  inicioRiego = millis();
  riegosTotales++;
  registrarEvento("Riego ON: " + motivo);
}

// Apaga la bomba y arranca el descanso.
void apagarRiego(String motivo) {
  ponerBomba(false);
  finRiego = millis();
  huboRiego = true;
  registrarEvento("Riego OFF: " + motivo);
}

// Actualiza el bloqueo por nivel de agua (con histeresis).
void revisarNivelAgua() {
  if (!USAR_SENSOR_NIVEL) {
    bloqueoAgua = false;
    return;
  }
  if (!bloqueoAgua && nivelPorc < NIVEL_BAJO_PORC) {
    bloqueoAgua = true;
    registrarEvento("Nivel de agua bajo: riego bloqueado");
  } else if (bloqueoAgua && nivelPorc > NIVEL_OK_PORC) {
    bloqueoAgua = false;
    registrarEvento("Nivel de agua recuperado");
  }
}

// Decide si hay que encender o apagar la bomba.
void controlarRiego() {
  unsigned long ahora = millis();

  // Reglas de seguridad: valen en los dos modos.
  if (bombaEncendida && bloqueoAgua) {
    apagarRiego("poca agua");
    return;
  }
  if (bombaEncendida && (ahora - inicioRiego) >= RIEGO_MAXIMO_MS) {
    apagarRiego("tiempo maximo");
    return;
  }

  // Solo el modo automatico decide por el suelo.
  if (modo != MODO_AUTOMATICO) {
    return;
  }
  if (bombaEncendida) {
    if (sueloPorc >= UMBRAL_HUMEDO_PORC) {
      apagarRiego("suelo humedo");
    }
  } else if (sueloPorc < UMBRAL_SECO_PORC && puedeRegar()) {
    encenderRiego("suelo seco");
  }
}

// Cambia el modo de trabajo.
void cambiarModo(int nuevoModo) {
  if (nuevoModo == modo) {
    return;
  }
  modo = nuevoModo;
  if (modo == MODO_MANUAL) {
    registrarEvento("Modo manual");
  } else {
    registrarEvento("Modo automatico");
  }
}

// Texto del modo.
const char* textoModo() {
  return (modo == MODO_AUTOMATICO) ? "AUTOMATICO" : "MANUAL";
}

// ============================================================================
// 6. FUNCIONES PROPIAS: LCD
// ============================================================================

// Escribe una fila de la LCD solo si cambio (rellena a 16 caracteres).
void escribirFila(int fila, String texto) {
  if (!lcdDisponible) {
    return;
  }
  while (texto.length() < 16) {
    texto += ' ';
  }
  texto = texto.substring(0, 16);
  if (texto == filaActual[fila]) {
    return;
  }
  filaActual[fila] = texto;
  lcd.setCursor(0, fila);
  lcd.print(texto);
}

// Actualiza las dos filas de la LCD.
void actualizarPantalla() {
  // Fila 1: suelo y luz (maximo 16 caracteres: Suelo 100% L100%).
  escribirFila(0, "Suelo " + String(sueloPorc) + "% L" + String(luzPorc) + "%");

  // Fila 2: temperatura y humedad del aire (8 caracteres) + estado del riego
  // (8 caracteres).
  String izquierda = "";
  if (dhtValido) {
    izquierda = String((int)(temperatura + 0.5)) + "C " + String((int)(humedadAire + 0.5)) + "%";
  } else {
    izquierda = "--C --%";
  }
  while (izquierda.length() < 8) {
    izquierda += ' ';
  }
  String derecha = "";
  if (bloqueoAgua) {
    derecha = "SIN AGUA";
  } else if (modo == MODO_MANUAL) {
    derecha = bombaEncendida ? "Man. ON" : "Man. --";
  } else {
    derecha = bombaEncendida ? "Riego ON" : "Riego --";
  }
  escribirFila(1, izquierda + derecha);
}

// Escribe un resumen en el Monitor Serial.
void imprimirResumen() {
  Serial.print("Modo: ");
  Serial.print(textoModo());
  Serial.print(" | Suelo: ");
  Serial.print(sueloPorc);
  Serial.print(" % | Luz: ");
  Serial.print(luzPorc);
  Serial.print(" % | Agua: ");
  Serial.print(nivelPorc);
  Serial.print(" % | ");
  if (dhtValido) {
    Serial.print(temperatura, 1);
    Serial.print(" C, ");
    Serial.print(humedadAire, 0);
    Serial.print(" %");
  } else {
    Serial.print("DHT11 sin lectura");
  }
  Serial.print(" | Bomba: ");
  Serial.print(bombaEncendida ? "ON" : "OFF");
  Serial.print(" | Riegos: ");
  Serial.println(riegosTotales);
}

// ============================================================================
// 7. FUNCIONES PROPIAS: RED Y PAGINA WEB
// ============================================================================

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
  Serial.println("Sin red: el invernadero funciona igual, pero sin pagina web.");
  return false;
}

// Extrae la ruta de la primera linea de una peticion HTTP.
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

// Envia la pagina con las mediciones, el modo y los botones.
void enviarPagina(WiFiClient& cliente) {
  cliente.println("HTTP/1.1 200 OK");
  cliente.println("Content-Type: text/html; charset=UTF-8");
  cliente.println("Connection: close");
  cliente.println();

  cliente.println("<!DOCTYPE html><html lang=\"es\"><head>");
  cliente.println("<meta charset=\"UTF-8\">");
  cliente.println("<meta name=\"viewport\" ");
  cliente.println("content=\"width=device-width, initial-scale=1\">");
  cliente.print("<meta http-equiv=\"refresh\" content=\"");
  cliente.print(SEGUNDOS_RECARGA);
  cliente.println("\">");
  cliente.println("<title>Invernadero inteligente - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;");
  cliente.println("margin:0;padding:20px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".tarjeta{max-width:320px;margin:12px auto;padding:14px;");
  cliente.println("border-radius:12px;background:#E3F4F6}");
  cliente.println(".tarjeta h2{margin:0;font-size:15px;color:#00606A}");
  cliente.println(".valor{font-size:36px;font-weight:bold;color:#007A86}");
  cliente.println(".chico{font-size:18px}");
  cliente.println(".alerta{background:#FBE3E3}.alerta .valor{color:#C62828}");
  cliente.println(".boton{display:inline-block;margin:8px 4px 0;padding:10px 18px;");
  cliente.println("border-radius:8px;background:#007A86;color:#fff;");
  cliente.println("text-decoration:none;font-weight:bold}");
  cliente.println("p{font-size:13px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Invernadero inteligente</h1>");
  cliente.println("<p>Arduino UNO R4 WiFi - Curso Arduino UAM</p>");

  // Suelo.
  cliente.print("<div class=\"tarjeta\"><h2>Humedad del suelo</h2><div class=\"valor\">");
  cliente.print(sueloPorc);
  cliente.println(" %</div></div>");

  // Aire.
  cliente.print("<div class=\"tarjeta\"><h2>Temperatura y humedad del aire</h2>");
  cliente.print("<div class=\"valor\">");
  if (dhtValido) {
    cliente.print(temperatura, 1);
    cliente.print(" &deg;C</div><div class=\"chico\">");
    cliente.print(humedadAire, 0);
    cliente.print(" % de humedad</div></div>");
  } else {
    cliente.println("--</div><div class=\"chico\">Sin lectura del DHT11</div></div>");
  }

  // Luz.
  cliente.print("<div class=\"tarjeta\"><h2>Luz</h2><div class=\"valor\">");
  cliente.print(luzPorc);
  cliente.println(" %</div></div>");

  // Agua del deposito.
  cliente.print("<div class=\"tarjeta");
  if (bloqueoAgua) {
    cliente.print(" alerta");
  }
  cliente.print("\"><h2>Agua del deposito</h2><div class=\"valor chico\">");
  if (!USAR_SENSOR_NIVEL) {
    cliente.print("Sin sensor");
  } else if (bloqueoAgua) {
    cliente.print("BAJA (");
    cliente.print(nivelPorc);
    cliente.print(" %): riego bloqueado");
  } else {
    cliente.print("OK (");
    cliente.print(nivelPorc);
    cliente.print(" %)");
  }
  cliente.println("</div></div>");

  // Riego y modo.
  cliente.print("<div class=\"tarjeta\"><h2>Riego</h2><div class=\"valor chico\">");
  cliente.print(bombaEncendida ? "ENCENDIDO" : "APAGADO");
  cliente.print("</div><div>Modo: ");
  cliente.print(textoModo());
  cliente.println("</div>");
  if (modo == MODO_AUTOMATICO) {
    cliente.println("<a class=\"boton\" href=\"/manual\">Pasar a manual</a>");
  } else {
    cliente.println("<a class=\"boton\" href=\"/auto\">Pasar a automatico</a>");
    cliente.print("<a class=\"boton\" href=\"/riego\">");
    cliente.print(bombaEncendida ? "Apagar riego" : "Encender riego");
    cliente.println("</a>");
  }
  cliente.println("</div>");

  // Ultimo evento.
  cliente.print("<div class=\"tarjeta\"><h2>Ultimo evento</h2>");
  cliente.print("<div class=\"chico\">");
  cliente.print(ultimoEvento);
  cliente.print("</div><div>hace ");
  cliente.print((unsigned long)((millis() - tiempoUltimoEvento) / 1000UL));
  cliente.println(" s</div></div>");

  cliente.print("<p>Se actualiza sola cada ");
  cliente.print(SEGUNDOS_RECARGA);
  cliente.println(" s. Sin usuario ni clave: use solo la red del curso.</p>");
  cliente.println("</body></html>");
}

// Responde con una redireccion (303) a la pagina principal.
void enviarRedireccion(WiFiClient& cliente) {
  cliente.println("HTTP/1.1 303 See Other");
  cliente.println("Location: /");
  cliente.println("Connection: close");
  cliente.println();
}

void enviarNoEncontrada(WiFiClient& cliente) {
  cliente.println("HTTP/1.1 404 Not Found");
  cliente.println("Content-Type: text/plain");
  cliente.println("Connection: close");
  cliente.println();
  cliente.println("Ruta no encontrada");
}

// Atiende a un cliente: lee la peticion y responde.
void atenderCliente(WiFiClient& cliente) {
  String primeraLinea = "";
  String lineaActual = "";
  unsigned long inicio = millis();

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
  if (ruta == "/") {
    enviarPagina(cliente);
  } else if (ruta == "/manual") {
    cambiarModo(MODO_MANUAL);
    enviarRedireccion(cliente);
  } else if (ruta == "/auto") {
    cambiarModo(MODO_AUTOMATICO);
    enviarRedireccion(cliente);
  } else if (ruta == "/riego") {
    // Solo en modo manual: alterna la bomba.
    if (modo == MODO_MANUAL) {
      if (bombaEncendida) {
        apagarRiego("orden manual");
      } else if (bloqueoAgua) {
        registrarEvento("Riego rechazado: poca agua");
      } else if (!puedeRegar()) {
        registrarEvento("Riego rechazado: en descanso");
      } else {
        encenderRiego("orden manual");
      }
    }
    enviarRedireccion(cliente);
  } else {
    enviarNoEncontrada(cliente);
  }

  delay(1);
  cliente.stop();
}

// ============================================================================
// 8. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  // La bomba se deja APAGADA antes de configurar el pin como salida,
  // para que el rele no se active un instante al encender.
  digitalWrite(PIN_RELE, NIVEL_RELE_APAGADO);
  pinMode(PIN_RELE, OUTPUT);

  sensorDht.begin();

  // Pantalla LCD.
  if (lcd.begin(16, 2) == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  Serial.println("INVERNADERO INTELIGENTE - ARDUINO UNO R4 WIFI");
  Serial.println("---------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  escribirFila(0, "Invernadero UAM");
  escribirFila(1, "Midiendo...");

  // El DHT11 necesita unos instantes tras encender.
  delay(2000);
  medirTodo();
  revisarNivelAgua();

  escribirFila(1, "Conectando WiFi");
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("No se detecta el modulo WiFi: el invernadero funciona sin pagina web.");
  } else {
    String version = WiFi.firmwareVersion();
    Serial.print("Firmware del modulo WiFi: ");
    Serial.println(version);
    if (version < WIFI_FIRMWARE_LATEST_VERSION) {
      Serial.println("Aviso: hay un firmware mas nuevo (Herramientas > Firmware Updater).");
    }
    if (conectarWifi()) {
      servidor.begin();
      imprimirDatosRed();
    }
  }

  registrarEvento("Invernadero listo");
  ultimaMedida = millis();
  ultimaRevisionRed = millis();
}

// ============================================================================
// 9. LOOP
// ============================================================================

void loop() {
  unsigned long ahora = millis();

  // Mediciones periodicas.
  if (ahora - ultimaMedida >= INTERVALO_MEDIDA_MS) {
    ultimaMedida = ahora;
    medirTodo();
    revisarNivelAgua();
  }

  // Control y pantalla (en cada vuelta: los limites de tiempo y el
  // bloqueo por agua se aplican sin esperar a la proxima medicion).
  controlarRiego();
  actualizarPantalla();

  // Resumen cada 10 s.
  if (ahora - ultimoResumen >= INTERVALO_RESUMEN_MS) {
    ultimoResumen = ahora;
    imprimirResumen();
  }

  // Reconexion WiFi: solo con la bomba apagada, porque reconectar
  // bloquea la placa varios segundos y el limite de riego no se vigilaria.
  if (ahora - ultimaRevisionRed >= INTERVALO_REVISION_RED_MS) {
    ultimaRevisionRed = ahora;
    if (!bombaEncendida && WiFi.status() != WL_CONNECTED && WiFi.status() != WL_NO_MODULE) {
      Serial.println("Sin conexion a la red. Reintentando...");
      if (conectarWifi()) {
        servidor.begin();
        imprimirDatosRed();
      }
    }
  }

  // Cliente web.
  WiFiClient cliente = servidor.available();
  if (cliente) {
    atenderCliente(cliente);
  }
}