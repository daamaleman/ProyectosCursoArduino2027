/*
  ============================================================================
  EJERCICIO 26: TERMOSTATO IoT CON UMBRAL CONFIGURABLE DESDE EL NAVEGADOR
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor DHT11 (modulo de 3 pines: S, V, G)
                - Modulo rele Keyestudio de 5 V (marcas S, V, G)
                - LED con resistencia de 1 kohmio
  BIBLIOTECAS:  "DHT sensor library" (Adafruit; pide tambien "Adafruit Unified
                Sensor": acepte). La biblioteca WiFiS3 ya viene con la placa.
  ARCHIVOS:     Este programa necesita el archivo "arduino_secrets.h" en la
                misma carpeta (nombre y clave de la red WiFi).

  CONEXIONES:   DHT11   S -> D2   V -> 5V   G -> GND
                Rele    S -> D7   V -> 5V   G -> GND
                        (solo carga de BAJA TENSION en COM y NO)
                LED     D8 -> resistencia 1 kohm -> anodo (pata larga)
                        catodo (pata corta) -> GND

  QUE HACE:     - Mide la temperatura cada 2 s.
                - La carga (rele + LED) se ENCIENDE cuando la temperatura
                  SUPERA el umbral y se APAGA cuando baja 1 grado por debajo
                  del umbral (histeresis). Ejemplo con umbral 30 C: enciende
                  al llegar a 31 C y apaga a 29 C o menos.
                - Crea un servidor web: la pagina muestra la temperatura, el
                  umbral, el estado de la carga y cuantas veces se activo
                  desde el arranque.
                - La pagina tiene un cuadro para escribir un umbral nuevo.
                  Al enviarlo, el navegador pide:  /umbral?valor=32
                  La placa comprueba que sea un numero entero entre 20 y 40.
                  Si no lo es, no cambia nada y la pagina explica el error.
                - Si el DHT11 falla, la carga se APAGA (estado seguro).
                - El termostato sigue funcionando aunque se caiga la red WiFi.

  RUTAS:        /                  -> pagina principal
                /umbral?valor=NN   -> cambia el umbral (20 a 40)
                cualquier otra     -> 404

  SEGURIDAD:    Todo funciona en la red local, sin Internet. Cualquier equipo
                de esa red puede abrir la pagina y cambiar el umbral: NO pide
                usuario ni clave. El umbral NO se guarda: al reiniciar la
                placa vuelve a 30 C. Use solo cargas de baja tension.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

#include <WiFiS3.h>
#include <DHT.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_DHT = 2;               // DHT11: terminal S.
const int PIN_RELE = 7;              // Modulo rele: terminal S.
const int PIN_LED_EXTERNO = 8;       // LED externo con resistencia de 1 kohm.

// Nivel que ENCIENDE el rele. El modulo Keyestudio se activa con HIGH;
// si su modulo se activa con LOW, cambie estos dos valores entre si.
const int NIVEL_RELE_ENCENDIDO = HIGH;
const int NIVEL_RELE_APAGADO   = LOW;

// ---- Umbral ----
const int UMBRAL_MINIMO_C  = 20;     // Valor minimo que se acepta.
const int UMBRAL_MAXIMO_C  = 40;     // Valor maximo que se acepta.
const int UMBRAL_INICIAL_C = 30;     // Valor al arrancar.
const float HISTERESIS_C   = 1.0;    // La carga apaga a (umbral - 1).

// El DHT11 no debe leerse mas rapido que una vez por segundo.
const unsigned long INTERVALO_MEDICION_MS = 2000UL;

// ---- Red ----
const int INTENTOS_CONEXION = 5;
const unsigned long PAUSA_INTENTO_MS = 5000UL;
const unsigned long INTERVALO_REINTENTO_RED_MS = 10000UL;
const unsigned long ESPERA_CLIENTE_MS = 2000UL;
const unsigned int LARGO_MAXIMO_LINEA = 100;

// Resultados al leer un umbral nuevo de la direccion.
const int UMBRAL_OK      = 0;
const int UMBRAL_FORMATO = 1;        // No es un numero entero.
const int UMBRAL_RANGO   = 2;        // Esta fuera de 20 a 40.

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

DHT sensor(PIN_DHT, DHT11);
WiFiServer servidor(80);

int umbralC = UMBRAL_INICIAL_C;      // Umbral actual (se cambia desde la web).
float temperaturaC = 0.0;
bool lecturaValida = false;
bool cargaEncendida = false;
unsigned long activaciones = 0;      // Veces que se encendio la carga.

unsigned long ultimaMedicion = 0;
unsigned long ultimoReintentoRed = 0;
bool servidorIniciado = false;

// ============================================================================
// 4. FUNCIONES PROPIAS: TERMOSTATO
// ============================================================================

// Pone la carga (rele y LED juntos) en el estado pedido.
void aplicarCarga(bool encender) {
  digitalWrite(PIN_RELE, encender ? NIVEL_RELE_ENCENDIDO : NIVEL_RELE_APAGADO);
  digitalWrite(PIN_LED_EXTERNO, encender ? HIGH : LOW);
  cargaEncendida = encender;
}

// Decide si la carga debe estar encendida (con histeresis).
void actualizarCarga() {
  if (!lecturaValida) {
    if (cargaEncendida) {
      aplicarCarga(false);
      Serial.println("CARGA APAGADA: no hay lectura valida del DHT11 (estado seguro)");
    }
    return;
  }
  if (!cargaEncendida && temperaturaC > umbralC) {
    aplicarCarga(true);
    activaciones++;
    Serial.print("CARGA ENCENDIDA: ");
    Serial.print(temperaturaC, 1);
    Serial.print(" C supera el umbral de ");
    Serial.print(umbralC);
    Serial.print(" C (activaciones: ");
    Serial.print(activaciones);
    Serial.println(")");
  } else if (cargaEncendida && temperaturaC <= umbralC - HISTERESIS_C) {
    aplicarCarga(false);
    Serial.print("CARGA APAGADA: ");
    Serial.print(temperaturaC, 1);
    Serial.println(" C bajo del umbral menos la histeresis");
  }
}

// Mide la temperatura y actualiza la carga.
void medir() {
  float t = sensor.readTemperature();
  if (isnan(t)) {
    lecturaValida = false;
    Serial.println("Error: no se pudo leer el DHT11.");
  } else {
    lecturaValida = true;
    temperaturaC = t;
    Serial.print("Temperatura: ");
    Serial.print(temperaturaC, 1);
    Serial.print(" C | Umbral: ");
    Serial.print(umbralC);
    Serial.print(" C | Carga: ");
    Serial.println(cargaEncendida ? "ENCENDIDA" : "apagada");
  }
  actualizarCarga();
}

// ============================================================================
// 5. FUNCIONES PROPIAS: RED
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
  Serial.println("No se pudo conectar. El termostato sigue funcionando sin la pagina.");
  return false;
}

// Si la red se cae, pide reconectar sin detener el termostato.
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
// 6. FUNCIONES PROPIAS: PETICIONES WEB
// ============================================================================

// Extrae la ruta completa (con su parte "?...") de "GET /ruta HTTP/1.1".
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

// Lee el valor de "valor=NN" y comprueba que sea un entero de 20 a 40.
// Solo cambia "nuevo" si todo es correcto. Devuelve UMBRAL_OK, FORMATO o RANGO.
int leerUmbral(const String& consulta, int& nuevo) {
  if (!consulta.startsWith("valor=")) {
    return UMBRAL_FORMATO;
  }
  String texto = consulta.substring(6);
  if (texto.length() == 0 || texto.length() > 3) {
    return UMBRAL_FORMATO;
  }
  for (unsigned int i = 0; i < texto.length(); i++) {
    if (texto[i] < '0' || texto[i] > '9') {
      return UMBRAL_FORMATO;           // Solo se aceptan digitos.
    }
  }
  int valor = texto.toInt();
  if (valor < UMBRAL_MINIMO_C || valor > UMBRAL_MAXIMO_C) {
    return UMBRAL_RANGO;
  }
  nuevo = valor;
  return UMBRAL_OK;
}

// Envia la pagina. "mensaje" va en un recuadro (vacio = no se muestra).
void enviarPagina(WiFiClient& cliente, int codigo, const String& mensaje) {
  if (codigo == 400) {
    cliente.println("HTTP/1.1 400 Bad Request");
  } else {
    cliente.println("HTTP/1.1 200 OK");
  }
  cliente.println("Content-Type: text/html; charset=UTF-8");
  cliente.println("Connection: close");
  cliente.println();
  cliente.println("<!DOCTYPE html><html lang=\"es\"><head>");
  cliente.println("<meta charset=\"UTF-8\">");
  cliente.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  if (codigo == 200) {
    cliente.println("<meta http-equiv=\"refresh\" content=\"10;url=/\">");
  }
  cliente.println("<title>Termostato - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;margin:0;padding:24px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".temp{font-size:56px;font-weight:bold;color:#007A86;margin:8px 0}");
  cliente.println(".estado{display:inline-block;padding:12px 24px;border-radius:12px;");
  cliente.println("font-size:20px;font-weight:bold;color:#fff}");
  cliente.println(".on{background:#009CAA}.off{background:#7F8184}");
  cliente.println("form{margin:20px 0}");
  cliente.println("input{font-size:22px;width:90px;padding:8px;text-align:center}");
  cliente.println("button{font-size:20px;padding:10px 20px;border:0;border-radius:10px;");
  cliente.println("background:#007A86;color:#fff;font-weight:bold}");
  cliente.println(".aviso{margin:16px auto;max-width:360px;padding:12px;border-radius:10px;");
  cliente.println("background:#FDECEC;color:#8B1A1A;font-weight:bold}");
  cliente.println("p{font-size:14px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Termostato IoT</h1>");
  if (lecturaValida) {
    cliente.print("<div class=\"temp\">");
    cliente.print(temperaturaC, 1);
    cliente.println(" &deg;C</div>");
  } else {
    cliente.println("<div class=\"temp\">-- &deg;C</div>");
    cliente.println("<p>Sin lectura del sensor: la carga se mantiene apagada.</p>");
  }
  if (cargaEncendida) {
    cliente.println("<div class=\"estado on\">Carga ENCENDIDA</div>");
  } else {
    cliente.println("<div class=\"estado off\">Carga apagada</div>");
  }
  cliente.print("<p>Umbral actual: <b>");
  cliente.print(umbralC);
  cliente.println(" &deg;C</b> (enciende sobre el umbral, apaga 1 &deg;C abajo)</p>");
  cliente.print("<p>Veces que se activo la carga desde el arranque: <b>");
  cliente.print(activaciones);
  cliente.println("</b></p>");
  if (mensaje.length() > 0) {
    cliente.print("<div class=\"aviso\">");
    cliente.print(mensaje);
    cliente.println("</div>");
  }
  cliente.println("<form action=\"/umbral\" method=\"get\">");
  cliente.print("<input type=\"number\" name=\"valor\" min=\"");
  cliente.print(UMBRAL_MINIMO_C);
  cliente.print("\" max=\"");
  cliente.print(UMBRAL_MAXIMO_C);
  cliente.print("\" value=\"");
  cliente.print(umbralC);
  cliente.println("\"> &deg;C ");
  cliente.println("<button type=\"submit\">Cambiar umbral</button></form>");
  cliente.print("<p>Valores permitidos: de ");
  cliente.print(UMBRAL_MINIMO_C);
  cliente.print(" a ");
  cliente.print(UMBRAL_MAXIMO_C);
  cliente.println(" &deg;C, enteros.</p>");
  cliente.println("<p>Red local sin Internet. Cualquier equipo de esta red puede cambiar el umbral.</p>");
  cliente.println("</body></html>");
}

// Responde "vuelva a la pagina principal" (codigo 303).
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

  // Separa la ruta de la consulta: "/umbral?valor=32" -> "/umbral" y "valor=32".
  String camino = ruta;
  String consulta = "";
  int signo = ruta.indexOf('?');
  if (signo >= 0) {
    camino = ruta.substring(0, signo);
    consulta = ruta.substring(signo + 1);
  }

  if (camino == "/") {
    enviarPagina(cliente, 200, "");
  } else if (camino == "/umbral") {
    int nuevo = umbralC;
    int resultado = leerUmbral(consulta, nuevo);
    if (resultado == UMBRAL_OK) {
      umbralC = nuevo;
      Serial.print("Umbral cambiado a ");
      Serial.print(umbralC);
      Serial.println(" C");
      actualizarCarga();             // Se revisa de inmediato con el umbral nuevo.
      enviarRedireccion(cliente);
    } else if (resultado == UMBRAL_RANGO) {
      Serial.println("Umbral rechazado: fuera de rango.");
      enviarPagina(cliente, 400, "Umbral NO cambiado: debe estar entre 20 y 40 &deg;C.");
    } else {
      Serial.println("Umbral rechazado: no es un numero entero.");
      enviarPagina(cliente, 400, "Umbral NO cambiado: escriba un numero entero, por ejemplo 32.");
    }
  } else {
    enviarNoEncontrada(cliente);
  }

  delay(1);
  cliente.stop();
}

// ============================================================================
// 7. SETUP
// ============================================================================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_LED_EXTERNO, OUTPUT);
  aplicarCarga(false);               // La carga arranca apagada.
  sensor.begin();

  delay(1500);
  Serial.println("TERMOSTATO IoT CON UMBRAL DESDE EL NAVEGADOR - ARDUINO UNO R4 WIFI");
  Serial.println("------------------------------------------------------------------");

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("No se detecta el modulo WiFi. Revise la placa seleccionada.");
    while (true) {
      // Sin modulo WiFi no hay nada mas que hacer.
    }
  }

  if (conectarWifi()) {
    servidor.begin();
    servidorIniciado = true;
    imprimirDatosRed();
  }
  ultimoReintentoRed = millis();
  ultimaMedicion = millis() - INTERVALO_MEDICION_MS;   // Primera medicion ya.
}

// ============================================================================
// 8. LOOP
// ============================================================================

void loop() {
  // El termostato se atiende primero: no depende de la red.
  if (millis() - ultimaMedicion >= INTERVALO_MEDICION_MS) {
    ultimaMedicion = millis();
    medir();
  }

  vigilarRed();

  if (servidorIniciado) {
    WiFiClient cliente = servidor.available();
    if (cliente) {
      atenderCliente(cliente);
    }
  }
}