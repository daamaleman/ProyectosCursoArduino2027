/*
  ============================================================================
  EJERCICIO 28: ESTACION METEOROLOGICA IoT CON HISTORIAL EN EL NAVEGADOR
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor DHT11 (modulo de 3 pines: S, V, G)
                - Modulo fotorresistencia (LDR) Keyestudio (S, V, G)
                - Pantalla LCD 1602 con modulo I2C (GND, VCC, SDA, SCL)
  BIBLIOTECAS:  "DHT sensor library" (Adafruit; pide tambien "Adafruit Unified
                Sensor": acepte) y "hd44780" (Bill Perry) para la LCD I2C.
                La biblioteca WiFiS3 ya viene con la placa.
  ARCHIVOS:     Este programa necesita el archivo "arduino_secrets.h" en la
                misma carpeta (nombre y clave de la red WiFi).

  CONEXIONES:   DHT11   S -> D2    V -> 5V   G -> GND
                LDR     S -> A0    V -> 5V   G -> GND
                LCD I2C GND -> GND   VCC -> 5V   SDA -> A4   SCL -> A5

  QUE HACE:     - Cada 10 s mide temperatura, humedad y luz y GUARDA la
                  lectura en un arreglo que conserva solo las ULTIMAS 10
                  (cuando se llena, la lectura nueva reemplaza a la mas
                  antigua: "buffer circular").
                - La LCD muestra siempre la lectura MAS RECIENTE.
                - Crea un servidor web: la pagina muestra la ultima lectura,
                  una TABLA con las lecturas guardadas (la mas nueva arriba),
                  el MAXIMO y el MINIMO del periodo guardado y una GRAFICA DE
                  BARRAS de la temperatura (hecha solo con HTML y CSS).
                - Si el DHT11 falla en una lectura, esa fila se guarda como
                  "error" (se ve el hueco) y no entra en los maximos ni en
                  los minimos.
                - La estacion sigue midiendo y mostrando en la LCD aunque se
                  caiga la red WiFi.

  RUTAS:        /            -> pagina principal (se recarga sola cada 10 s)
                cualquier otra -> 404

  LIMITACION:   El historial vive en la memoria RAM: se PIERDE al reiniciar
                la placa o al quitarle la energia. Guardarlo de forma
                permanente exigiria memoria no volatil, una tarjeta SD o un
                servicio externo.

  SEGURIDAD:    Todo funciona en la red local, sin Internet ni nube. La pagina
                solo MUESTRA datos, pero cualquier equipo de la red puede
                verla.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

#include <WiFiS3.h>
#include <DHT.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_DHT = 2;               // DHT11: terminal S.
const int PIN_LDR = A0;              // Fotorresistencia: terminal S.

// ---- Historial ----
const int TAMANO_HISTORIAL = 10;                    // Lecturas que se guardan.
const unsigned long INTERVALO_MUESTRA_MS = 10000UL; // Una lectura cada 10 s.
const unsigned long ESPERA_PRIMERA_MS = 2000UL;     // La primera, a los 2 s.

// ---- Luz: lectura cruda (0 a 1023) que se toma como 0 % y como 100 % ----
// Son valores de ejemplo: calibrelos con SU sensor (ver ejercicio 4).
const int LUZ_OSCURO_BRUTO = 100;
const int LUZ_CLARO_BRUTO  = 900;
// Si es true, la lectura SUBE cuando hay MENOS luz (sensor invertido).
const bool SENSOR_LUZ_INVERTIDO = false;
// Cuantas lecturas se promedian para medir la luz.
const int MUESTRAS_LUZ = 8;

// ---- Grafica de barras ----
const int ALTO_GRAFICA_PX = 120;     // Altura de la barra mas alta.
const float TEMP_GRAFICA_MAX_C = 50.0; // Temperatura que llena la grafica.

// ---- Red ----
const int INTENTOS_CONEXION = 5;
const unsigned long PAUSA_INTENTO_MS = 5000UL;
const unsigned long INTERVALO_REINTENTO_RED_MS = 10000UL;
const unsigned long ESPERA_CLIENTE_MS = 2000UL;
const unsigned int LARGO_MAXIMO_LINEA = 100;

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

// Una lectura guardada en el historial.
struct Muestra {
  bool valida;                 // false si el DHT11 fallo en esta lectura.
  float temperatura;           // Grados Celsius.
  float humedad;               // Porcentaje.
  int luz;                     // Porcentaje (0 a 100).
  unsigned long segundos;      // Instante de la lectura (segundos desde el arranque).
};

// Maximos y minimos del periodo guardado.
struct Extremos {
  bool hayDatos;               // false si no hay ninguna lectura valida.
  float tMin, tMax;
  float hMin, hMax;
  int lMin, lMax;
};

DHT sensor(PIN_DHT, DHT11);
WiFiServer servidor(80);
hd44780_I2Cexp lcd;

Muestra historial[TAMANO_HISTORIAL]; // Buffer circular de lecturas.
int siguiente = 0;                   // Posicion donde se guardara la proxima.
int cantidad = 0;                    // Lecturas guardadas (0 a 10).

bool lcdDisponible = false;
String filaActual[2] = {"", ""};     // Lo que ya esta escrito en cada fila.

unsigned long ultimaMuestra = 0;
unsigned long ultimoReintentoRed = 0;
bool servidorIniciado = false;

// ============================================================================
// 4. FUNCIONES PROPIAS: HISTORIAL
// ============================================================================

// Guarda una lectura. Si el arreglo esta lleno, reemplaza la mas antigua.
void guardarMuestra(const Muestra& m) {
  historial[siguiente] = m;
  siguiente = (siguiente + 1) % TAMANO_HISTORIAL;
  if (cantidad < TAMANO_HISTORIAL) {
    cantidad++;
  }
}

// Devuelve la posicion en el arreglo de la lectura numero "atras" contando
// desde la mas reciente (0 = la mas reciente, 1 = la anterior, ...).
int indiceReciente(int atras) {
  return (siguiente - 1 - atras + TAMANO_HISTORIAL) % TAMANO_HISTORIAL;
}

// Calcula maximos y minimos con las lecturas validas del historial.
Extremos calcularExtremos() {
  Extremos e;
  e.hayDatos = false;
  e.tMin = e.tMax = e.hMin = e.hMax = 0;
  e.lMin = e.lMax = 0;
  for (int atras = 0; atras < cantidad; atras++) {
    const Muestra& m = historial[indiceReciente(atras)];
    if (!m.valida) {
      continue;                        // Una lectura con error no cuenta.
    }
    if (!e.hayDatos) {                 // La primera valida fija el punto de partida.
      e.hayDatos = true;
      e.tMin = e.tMax = m.temperatura;
      e.hMin = e.hMax = m.humedad;
      e.lMin = e.lMax = m.luz;
    } else {
      e.tMin = min(e.tMin, m.temperatura);
      e.tMax = max(e.tMax, m.temperatura);
      e.hMin = min(e.hMin, m.humedad);
      e.hMax = max(e.hMax, m.humedad);
      e.lMin = min(e.lMin, m.luz);
      e.lMax = max(e.lMax, m.luz);
    }
  }
  return e;
}

// ============================================================================
// 5. FUNCIONES PROPIAS: MEDICION
// ============================================================================

// Mide la luz promediando varias lecturas y la devuelve de 0 a 100 %.
int leerLuzPorcentaje() {
  long suma = 0;
  for (int i = 0; i < MUESTRAS_LUZ; i++) {
    suma += analogRead(PIN_LDR);
  }
  int bruto = suma / MUESTRAS_LUZ;
  if (SENSOR_LUZ_INVERTIDO) {
    bruto = 1023 - bruto;
  }
  int porcentaje = map(bruto, LUZ_OSCURO_BRUTO, LUZ_CLARO_BRUTO, 0, 100);
  return constrain(porcentaje, 0, 100);
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

// Muestra en la LCD la lectura mas reciente del historial.
void actualizarPantalla() {
  if (cantidad == 0) {
    escribirFila(0, "Estacion UAM");
    escribirFila(1, "Esperando datos");
    return;
  }
  const Muestra& m = historial[indiceReciente(0)];
  if (m.valida) {
    escribirFila(0, "Temp:" + String((int)round(m.temperatura)) + "C Hum:"
                    + String((int)round(m.humedad)) + "%");
  } else {
    escribirFila(0, "Error del DHT11");
  }
  // "Luz:45% (7/10)": hasta 15 caracteres (16 si la luz es 100 %).
  escribirFila(1, "Luz:" + String(m.luz) + "% (" + String(cantidad)
                  + "/" + String(TAMANO_HISTORIAL) + ")");
}

// ============================================================================
// 7. FUNCIONES PROPIAS: TOMAR UNA LECTURA
// ============================================================================

// Mide los tres sensores, guarda la lectura y actualiza la LCD.
void tomarMuestra() {
  Muestra m;
  float t = sensor.readTemperature();
  float h = sensor.readHumidity();
  m.valida = !(isnan(t) || isnan(h));
  m.temperatura = m.valida ? t : 0.0;
  m.humedad = m.valida ? h : 0.0;
  m.luz = leerLuzPorcentaje();
  m.segundos = millis() / 1000UL;

  guardarMuestra(m);

  Serial.print("Lectura guardada (");
  Serial.print(cantidad);
  Serial.print("/");
  Serial.print(TAMANO_HISTORIAL);
  Serial.print("): ");
  if (m.valida) {
    Serial.print(m.temperatura, 1);
    Serial.print(" C | ");
    Serial.print(m.humedad, 0);
    Serial.print(" % | luz ");
  } else {
    Serial.print("ERROR DHT11 | luz ");
  }
  Serial.print(m.luz);
  Serial.println(" %");

  actualizarPantalla();
}

// ============================================================================
// 8. FUNCIONES PROPIAS: RED
// ============================================================================

void imprimirDatosRed() {
  Serial.print("Red: ");
  Serial.println(WiFi.SSID());
  Serial.print("Abra en el navegador: http://");
  Serial.println(WiFi.localIP());
}

// Conecta al arrancar (con varios intentos). Devuelve true si lo logra.
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
  Serial.println("No se pudo conectar. La estacion sigue midiendo sin la pagina.");
  return false;
}

// Si la red se cae, pide reconectar sin detener la estacion.
void vigilarRed() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!servidorIniciado) {
      servidor.begin();
      servidorIniciado = true;
      imprimirDatosRed();
    }
    return;
  }
  if (millis() - ultimoReintentoRed >= INTERVALO_REINTENTO_RED_MS) {
    ultimoReintentoRed = millis();
    Serial.println("Sin red: pidiendo reconectar...");
    servidorIniciado = false;
    WiFi.begin(ssid, pass);
  }
}

// ============================================================================
// 9. FUNCIONES PROPIAS: PAGINA WEB
// ============================================================================

// Convierte segundos en texto: 25 -> "25 s", 125 -> "2 min 5 s".
String textoHace(unsigned long segundos) {
  if (segundos < 60UL) {
    return String(segundos) + " s";
  }
  return String(segundos / 60UL) + " min " + String(segundos % 60UL) + " s";
}

// Envia la tabla con las lecturas guardadas (la mas reciente arriba).
void enviarTabla(WiFiClient& cliente, unsigned long ahoraSeg) {
  cliente.println("<h2>Historial (&uacute;ltimas lecturas)</h2>");
  cliente.println("<table><tr><th>#</th><th>Hace</th><th>Temp.</th>");
  cliente.println("<th>Hum.</th><th>Luz</th></tr>");
  for (int atras = 0; atras < cantidad; atras++) {
    const Muestra& m = historial[indiceReciente(atras)];
    cliente.print("<tr><td>");
    cliente.print(atras + 1);
    cliente.print("</td><td>");
    cliente.print(textoHace(ahoraSeg - m.segundos));
    if (m.valida) {
      cliente.print("</td><td>");
      cliente.print(m.temperatura, 1);
      cliente.print(" &deg;C</td><td>");
      cliente.print(m.humedad, 0);
      cliente.print(" %</td><td>");
    } else {
      cliente.print("</td><td colspan=\"2\" class=\"error\">error del DHT11</td><td>");
    }
    cliente.print(m.luz);
    cliente.println(" %</td></tr>");
  }
  cliente.println("</table>");
}

// Envia el maximo y el minimo del periodo guardado.
void enviarExtremos(WiFiClient& cliente) {
  Extremos e = calcularExtremos();
  cliente.print("<h2>M&aacute;ximo y m&iacute;nimo (");
  cliente.print(cantidad);
  cliente.println(" lecturas)</h2>");
  if (!e.hayDatos) {
    cliente.println("<p>No hay lecturas v&aacute;lidas todav&iacute;a.</p>");
    return;
  }
  cliente.println("<table><tr><th></th><th>M&iacute;nimo</th><th>M&aacute;ximo</th></tr>");
  cliente.print("<tr><td>Temperatura</td><td>");
  cliente.print(e.tMin, 1);
  cliente.print(" &deg;C</td><td>");
  cliente.print(e.tMax, 1);
  cliente.println(" &deg;C</td></tr>");
  cliente.print("<tr><td>Humedad</td><td>");
  cliente.print(e.hMin, 0);
  cliente.print(" %</td><td>");
  cliente.print(e.hMax, 0);
  cliente.println(" %</td></tr>");
  cliente.print("<tr><td>Luz</td><td>");
  cliente.print(e.lMin);
  cliente.print(" %</td><td>");
  cliente.print(e.lMax);
  cliente.println(" %</td></tr></table>");
}

// Envia la grafica de barras de temperatura (izquierda = lectura mas antigua).
void enviarGrafica(WiFiClient& cliente) {
  cliente.println("<h2>Gr&aacute;fica de temperatura</h2>");
  cliente.println("<div class=\"grafica\">");
  for (int atras = cantidad - 1; atras >= 0; atras--) {
    const Muestra& m = historial[indiceReciente(atras)];
    cliente.print("<div class=\"col\"><span>");
    if (m.valida) {
      int altura = (int)(m.temperatura * ALTO_GRAFICA_PX / TEMP_GRAFICA_MAX_C);
      altura = constrain(altura, 2, ALTO_GRAFICA_PX);
      cliente.print((int)round(m.temperatura));
      cliente.print("</span><div class=\"barra\" style=\"height:");
      cliente.print(altura);
      cliente.println("px\"></div></div>");
    } else {
      cliente.println("x</span><div class=\"barra vacia\" style=\"height:4px\"></div></div>");
    }
  }
  cliente.println("</div>");
  cliente.print("<p>Izquierda = lectura m&aacute;s antigua. Escala de 0 a ");
  cliente.print((int)TEMP_GRAFICA_MAX_C);
  cliente.println(" &deg;C.</p>");
}

// Envia la pagina completa.
void enviarPagina(WiFiClient& cliente) {
  unsigned long ahoraSeg = millis() / 1000UL;

  cliente.println("HTTP/1.1 200 OK");
  cliente.println("Content-Type: text/html; charset=UTF-8");
  cliente.println("Connection: close");
  cliente.println();
  cliente.println("<!DOCTYPE html><html lang=\"es\"><head>");
  cliente.println("<meta charset=\"UTF-8\">");
  cliente.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  cliente.println("<meta http-equiv=\"refresh\" content=\"10\">");
  cliente.println("<title>Estaci&oacute;n meteorol&oacute;gica - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;margin:0;padding:20px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}h2{color:#007A86;font-size:18px;margin:22px 0 8px}");
  cliente.println(".ultima{font-size:34px;font-weight:bold;color:#007A86}");
  cliente.println("table{margin:0 auto;border-collapse:collapse;font-size:15px}");
  cliente.println("th{background:#009CAA;color:#fff}th,td{padding:6px 12px;border:1px solid #D5D8DA}");
  cliente.println("tr:nth-child(even) td{background:#E3F4F6}.error{color:#8B1A1A;font-weight:bold}");
  cliente.println(".grafica{display:flex;align-items:flex-end;justify-content:center;gap:4px;");
  cliente.println("height:150px;border-bottom:2px solid #7F8184}");
  cliente.println(".col{display:flex;flex-direction:column;justify-content:flex-end;");
  cliente.println("align-items:center;width:34px}.col span{font-size:11px}");
  cliente.println(".barra{width:26px;background:#009CAA}.vacia{background:#C9CBCD}");
  cliente.println("p{font-size:14px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Estaci&oacute;n meteorol&oacute;gica IoT</h1>");

  if (cantidad == 0) {
    cliente.println("<p>A&uacute;n no hay lecturas. La primera llega en unos segundos.</p>");
  } else {
    const Muestra& ultima = historial[indiceReciente(0)];
    cliente.println("<p>&Uacute;ltima lectura</p>");
    if (ultima.valida) {
      cliente.print("<div class=\"ultima\">");
      cliente.print(ultima.temperatura, 1);
      cliente.print(" &deg;C &middot; ");
      cliente.print(ultima.humedad, 0);
      cliente.print(" % &middot; luz ");
      cliente.print(ultima.luz);
      cliente.println(" %</div>");
    } else {
      cliente.print("<div class=\"ultima\">DHT11 sin lectura &middot; luz ");
      cliente.print(ultima.luz);
      cliente.println(" %</div>");
    }
    enviarTabla(cliente, ahoraSeg);
    enviarExtremos(cliente);
    enviarGrafica(cliente);
  }

  cliente.println("<p>Se mide cada 10 s y se guardan las &uacute;ltimas 10 lecturas.</p>");
  cliente.println("<p>El historial est&aacute; en la memoria de la placa: se pierde al reiniciarla.</p>");
  cliente.println("<p>Red local sin Internet. Cualquier equipo de esta red puede ver la p&aacute;gina.</p>");
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

// Extrae la ruta de la primera linea "GET /ruta HTTP/1.1" (sin la parte "?...").
String extraerRuta(const String& linea) {
  if (!linea.startsWith("GET ")) {
    return "";
  }
  int inicio = 4;
  int fin = linea.indexOf(' ', inicio);
  if (fin < 0) {
    fin = linea.length();
  }
  String ruta = linea.substring(inicio, fin);
  int signo = ruta.indexOf('?');
  if (signo >= 0) {
    ruta = ruta.substring(0, signo);
  }
  return ruta;
}

// Atiende a un cliente: lee la peticion, decide y responde.
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
  } else {
    enviarNoEncontrada(cliente);
  }

  delay(1);
  cliente.stop();
}

// ============================================================================
// 10. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);
  analogReadResolution(10);          // Lecturas del LDR de 0 a 1023.
  sensor.begin();

  // Pantalla LCD.
  if (lcd.begin(16, 2) == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  delay(1500);
  Serial.println("ESTACION METEOROLOGICA IoT - ARDUINO UNO R4 WIFI");
  Serial.println("------------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  actualizarPantalla();              // "Estacion UAM / Esperando datos".

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("No se detecta el modulo WiFi. Revise la placa seleccionada.");
    while (true) {
      // Sin modulo WiFi no hay nada mas que hacer.
    }
  }

  escribirFila(1, "Conectando WiFi");
  if (conectarWifi()) {
    servidor.begin();
    servidorIniciado = true;
    imprimirDatosRed();
  }
  ultimoReintentoRed = millis();
  ultimaMuestra = millis();          // La primera lectura llega a los 2 s.
}

// ============================================================================
// 11. LOOP
// ============================================================================

void loop() {
  // La estacion mide primero: no depende de la red.
  unsigned long espera = (cantidad == 0) ? ESPERA_PRIMERA_MS : INTERVALO_MUESTRA_MS;
  if (millis() - ultimaMuestra >= espera) {
    ultimaMuestra = millis();
    tomarMuestra();
  }

  vigilarRed();

  if (servidorIniciado) {
    WiFiClient cliente = servidor.available();
    if (cliente) {
      atenderCliente(cliente);
    }
  }
}
