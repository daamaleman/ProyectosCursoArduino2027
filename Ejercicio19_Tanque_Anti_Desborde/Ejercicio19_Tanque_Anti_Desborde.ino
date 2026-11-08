/*
  ============================================================================
  EJERCICIO 19: CONTROL DE NIVEL DE TANQUE ANTI-DESBORDE
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor de nivel de agua (analogico, marcas S, V, G)
                - Modulo rele Keyestudio de 5 V (S, V, G): la BOMBA de llenado
                  (en la maqueta se simula con el modulo LED de 3 W)
                - Modulo buzzer PASIVO Keyestudio (S, V, G)
                - Modulo semaforo Keyestudio (GND, G, Y, R)
                - Pantalla LCD 16x2 con adaptador I2C (GND, VCC, SDA, SCL)
  BIBLIOTECAS:  "hd44780" de Bill Perry. WiFiS3 viene con la placa.
  ARCHIVOS:     Necesita "arduino_secrets.h" en la misma carpeta (nombre y
                clave de la red WiFi) para la pagina web.

  CONEXIONES:   Nivel agua S -> A0   V -> 5V   G -> GND
                Rele       S -> D7   V -> 5V   G -> GND
                Contactos del rele (lado de la carga, NO van al Arduino):
                  fuente de la carga (+) -> COM del rele
                  NO del rele            -> + de la bomba o del modulo LED
                Buzzer     S -> D5   V -> 5V   G -> GND
                Semaforo   GND -> GND   G (verde) -> D2
                           Y (amarillo) -> D3   R (rojo) -> D4
                LCD I2C    SDA -> A4   SCL -> A5   VCC -> 5V   GND -> GND

  QUE HACE:     Tres niveles segun el porcentaje del tanque:
                  BAJO   (menos de 30 %)  -> semaforo AMARILLO, pitido lento
                  NORMAL (30 % a 79 %)    -> semaforo VERDE, silencio
                  ALTO   (80 % o mas)     -> semaforo ROJO, pitidos rapidos
                - Modo AUTOMATICO: la bomba se enciende en nivel BAJO y se
                  apaga en nivel ALTO (anti-desborde). Entre los dos queda
                  como estaba (histeresis).
                - Nivel ALTO apaga la bomba en CUALQUIER modo.
                - ALARMA DE FALLA: si la bomba trabaja mas de 20 s sin que el
                  nivel suba, se apaga, suena una sirena y el semaforo
                  parpadea en rojo hasta que se reconozca desde la pagina.
                - Modo MANUAL: desde la pagina se enciende y apaga la bomba.
                - La LCD muestra el nivel en porcentaje y el estado.
                - Sin WiFi el control funciona igual, pero sin pagina.

  IMPORTANTE:   Es una maqueta educativa. El sensor se sumerge SOLO hasta las
                pistas, nunca la electronica, y se seca despues de usarlo
                porque se corroe. La pagina no pide usuario ni clave. Un
                tanque real necesita ademas un flotador de seguridad
                independiente. No use cargas de red electrica en el aula.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <WiFiS3.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_NIVEL    = A0;   // Sensor de nivel de agua (senal S).
const int PIN_RELE     = 7;    // Rele de la bomba (senal S).
const int PIN_BUZZER   = 5;    // Buzzer pasivo (se maneja con tone()).
const int PIN_VERDE    = 2;    // Semaforo: verde.
const int PIN_AMARILLO = 3;    // Semaforo: amarillo.
const int PIN_ROJO     = 4;    // Semaforo: rojo.

// Nivel que ENCIENDE el rele. El modulo Keyestudio se activa con HIGH;
// si su modulo se activa con LOW, cambie estos dos valores entre si.
const int NIVEL_RELE_ENCENDIDO = HIGH;
const int NIVEL_RELE_APAGADO   = LOW;

// ---- Calibracion del sensor: lectura bruta (0 a 1023) ----
// Con el sensor seco anote la lectura (0 %) y con el sensor sumergido hasta
// donde quiere medir (hasta las pistas) anote la otra (100 %).
const int NIVEL_VACIO_BRUTO = 50;
const int NIVEL_LLENO_BRUTO = 500;

// Cuantas lecturas se promedian en cada medicion.
const int MUESTRAS = 8;
// Cada cuantos ms se mide el nivel.
const unsigned long INTERVALO_MEDIDA_MS = 500UL;

// ---- Los tres niveles (en % del tanque) ----
const int UMBRAL_BAJO_PORC = 30;   // Debajo de esto: BAJO.
const int UMBRAL_ALTO_PORC = 80;   // Desde esto: ALTO.
// Margen para no cambiar de nivel por el ruido del sensor.
const int HISTERESIS_PORC = 3;

const int NIVEL_BAJO   = 0;
const int NIVEL_NORMAL = 1;
const int NIVEL_ALTO   = 2;

// ---- Alarma de falla de la bomba ----
// Si la bomba trabaja este tiempo sin que el nivel suba, hay una falla.
const unsigned long TIEMPO_FALLA_MS = 20000UL;
// Cuanto debe subir el nivel (en %) para contarlo como progreso.
const int SUBIDA_MINIMA_PORC = 3;
// Cuanto tiempo suena la sirena de falla (el estado de falla sigue despues).
const unsigned long ALARMA_SONORA_MS = 30000UL;

// ---- Buzzer ----
const int TONO_BAJO  = 1000;   // Pitido lento (nivel bajo).
const int TONO_ALTO  = 2200;   // Pitidos rapidos (nivel alto).
const int TONO_SIRENA_ALTO = 1500;
const int TONO_SIRENA_BAJO = 1000;
const unsigned long PERIODO_BAJO_MS = 2000UL;
const unsigned long DURACION_BAJO_MS = 150UL;
const unsigned long PERIODO_ALTO_MS = 400UL;
const unsigned long DURACION_ALTO_MS = 120UL;
const unsigned long CAMBIO_SIRENA_MS = 300UL;

// Patrones del buzzer.
const int PATRON_SILENCIO = 0;
const int PATRON_BAJO     = 1;
const int PATRON_ALTO     = 2;
const int PATRON_FALLA    = 3;

// Modos de trabajo.
const int MODO_AUTOMATICO = 0;
const int MODO_MANUAL     = 1;

// Cada cuantos ms se escribe un resumen en el Monitor Serial.
const unsigned long INTERVALO_RESUMEN_MS = 5000UL;

// Pagina web.
const int SEGUNDOS_RECARGA = 3;
const int INTENTOS_CONEXION = 3;
const unsigned long PAUSA_INTENTO_MS = 5000UL;
const unsigned long ESPERA_CLIENTE_MS = 500UL;
const unsigned long INTERVALO_REVISION_RED_MS = 10000UL;
const unsigned int LARGO_MAXIMO_LINEA = 100;

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

hd44780_I2Cexp lcd;
bool lcdDisponible = false;
String filaActual[2] = {"", ""};
WiFiServer servidor(80);

// Medicion.
int nivelBruto = 0;
int nivelPorc = 0;
int estadoNivel = NIVEL_NORMAL;
unsigned long ultimaMedida = 0;

// Control.
int modo = MODO_AUTOMATICO;
bool bombaEncendida = false;
bool falla = false;
unsigned long inicioFalla = 0;
unsigned long llenadosTotales = 0;

// Vigilancia de la bomba (para detectar la falla).
int nivelReferencia = 0;
unsigned long tiempoReferencia = 0;

// Buzzer.
int patronActual = PATRON_SILENCIO;
unsigned long ultimoPitido = 0;
int ultimaFrecuencia = 0;

// Mensajes.
String ultimoEvento = "Ninguno";
unsigned long tiempoUltimoEvento = 0;

unsigned long ultimoResumen = 0;
unsigned long ultimaRevisionRed = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: MEDICION Y NIVELES
// ============================================================================

// Anota el ultimo evento (para la pagina y el Monitor Serial).
void registrarEvento(String texto) {
  ultimoEvento = texto;
  tiempoUltimoEvento = millis();
  Serial.print("Evento: ");
  Serial.println(texto);
}

// Promedia varias lecturas analogicas del sensor de nivel.
int leerNivelBruto() {
  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    suma += analogRead(PIN_NIVEL);
  }
  return (int)(suma / MUESTRAS);
}

// Convierte la lectura bruta en porcentaje de 0 a 100.
int aPorcentaje(int bruto) {
  int porcentaje = map(bruto, NIVEL_VACIO_BRUTO, NIVEL_LLENO_BRUTO, 0, 100);
  return constrain(porcentaje, 0, 100);
}

// Clasifica el porcentaje en BAJO, NORMAL o ALTO sin histeresis.
int clasificarDirecto(int porcentaje) {
  if (porcentaje < UMBRAL_BAJO_PORC) {
    return NIVEL_BAJO;
  }
  if (porcentaje >= UMBRAL_ALTO_PORC) {
    return NIVEL_ALTO;
  }
  return NIVEL_NORMAL;
}

// Actualiza el nivel con histeresis para que el ruido no lo haga cambiar.
void actualizarEstadoNivel() {
  int anterior = estadoNivel;
  if (estadoNivel == NIVEL_BAJO) {
    if (nivelPorc >= UMBRAL_BAJO_PORC + HISTERESIS_PORC) {
      estadoNivel = clasificarDirecto(nivelPorc);
    }
  } else if (estadoNivel == NIVEL_ALTO) {
    if (nivelPorc < UMBRAL_ALTO_PORC - HISTERESIS_PORC) {
      estadoNivel = clasificarDirecto(nivelPorc);
    }
  } else {
    estadoNivel = clasificarDirecto(nivelPorc);
  }
  if (estadoNivel != anterior) {
    if (estadoNivel == NIVEL_BAJO) {
      registrarEvento("Nivel BAJO");
    } else if (estadoNivel == NIVEL_ALTO) {
      registrarEvento("Nivel ALTO");
    } else {
      registrarEvento("Nivel NORMAL");
    }
  }
}

// Texto del nivel.
const char* textoNivel() {
  if (estadoNivel == NIVEL_BAJO) {
    return "BAJO";
  }
  if (estadoNivel == NIVEL_ALTO) {
    return "ALTO";
  }
  return "NORMAL";
}

// Mide el nivel y actualiza la clasificacion.
void medirNivel() {
  nivelBruto = leerNivelBruto();
  nivelPorc = aPorcentaje(nivelBruto);
  actualizarEstadoNivel();
}

// ============================================================================
// 5. FUNCIONES PROPIAS: BOMBA, FALLA Y CONTROL
// ============================================================================

// Pone el rele en el nivel que corresponde.
void ponerBomba(bool encender) {
  digitalWrite(PIN_RELE, encender ? NIVEL_RELE_ENCENDIDO : NIVEL_RELE_APAGADO);
  bombaEncendida = encender;
}

// Indica si se puede encender la bomba (sin escribir mensajes).
bool puedeEncender() {
  return !falla && estadoNivel != NIVEL_ALTO;
}

// Enciende la bomba y empieza a vigilar que el nivel suba.
void encenderBomba(String motivo) {
  ponerBomba(true);
  nivelReferencia = nivelPorc;
  tiempoReferencia = millis();
  llenadosTotales++;
  registrarEvento("Bomba ON: " + motivo);
}

// Apaga la bomba.
void apagarBomba(String motivo) {
  ponerBomba(false);
  registrarEvento("Bomba OFF: " + motivo);
}

// Declara la falla de la bomba: la apaga y activa la alarma.
void declararFalla() {
  falla = true;
  inicioFalla = millis();
  apagarBomba("falla");
  registrarEvento("FALLA: la bomba trabajo 20 s sin subir el nivel");
}

// Revisa si la bomba lleva demasiado tiempo sin subir el nivel.
void vigilarBomba() {
  if (!bombaEncendida) {
    return;
  }
  unsigned long ahora = millis();
  if (nivelPorc >= nivelReferencia + SUBIDA_MINIMA_PORC) {
    nivelReferencia = nivelPorc;
    tiempoReferencia = ahora;
  } else if (ahora - tiempoReferencia >= TIEMPO_FALLA_MS) {
    declararFalla();
  }
}

// Reconoce la falla (desde la pagina web).
void reconocerFalla() {
  if (!falla) {
    return;
  }
  falla = false;
  registrarEvento("Falla reconocida");
}

// Decide si hay que encender o apagar la bomba.
void controlarBomba() {
  // Reglas de seguridad: valen en los dos modos.
  if (bombaEncendida && estadoNivel == NIVEL_ALTO) {
    apagarBomba("nivel alto");
    return;
  }
  vigilarBomba();
  if (modo != MODO_AUTOMATICO) {
    return;
  }
  // Automatico: se enciende en BAJO; se apaga en ALTO (regla de arriba).
  if (!bombaEncendida && estadoNivel == NIVEL_BAJO && puedeEncender()) {
    encenderBomba("nivel bajo");
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
// 6. FUNCIONES PROPIAS: SEMAFORO, BUZZER Y LCD
// ============================================================================

// Semaforo: verde = normal, amarillo = bajo, rojo = alto.
// En falla el rojo parpadea.
void actualizarSemaforo() {
  bool verde = false;
  bool amarillo = false;
  bool rojo = false;
  if (falla) {
    rojo = (((millis() / 500UL) % 2UL) == 0UL);
  } else if (estadoNivel == NIVEL_ALTO) {
    rojo = true;
  } else if (estadoNivel == NIVEL_BAJO) {
    amarillo = true;
  } else {
    verde = true;
  }
  digitalWrite(PIN_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, amarillo ? HIGH : LOW);
  digitalWrite(PIN_ROJO, rojo ? HIGH : LOW);
}

// Elige el patron del buzzer segun el estado.
int elegirPatron() {
  if (falla && (millis() - inicioFalla) < ALARMA_SONORA_MS) {
    return PATRON_FALLA;
  }
  if (falla) {
    return PATRON_SILENCIO;
  }
  if (estadoNivel == NIVEL_ALTO) {
    return PATRON_ALTO;
  }
  if (estadoNivel == NIVEL_BAJO) {
    return PATRON_BAJO;
  }
  return PATRON_SILENCIO;
}

// Hace sonar el patron elegido sin bloquear el programa.
void actualizarBuzzer() {
  unsigned long ahora = millis();
  int patron = elegirPatron();
  if (patron != patronActual) {
    patronActual = patron;
    noTone(PIN_BUZZER);
    ultimaFrecuencia = 0;
    ultimoPitido = ahora - 10000UL;   // El primer pitido suena enseguida.
  }
  if (patronActual == PATRON_BAJO) {
    if (ahora - ultimoPitido >= PERIODO_BAJO_MS) {
      ultimoPitido = ahora;
      tone(PIN_BUZZER, TONO_BAJO, DURACION_BAJO_MS);
    }
  } else if (patronActual == PATRON_ALTO) {
    if (ahora - ultimoPitido >= PERIODO_ALTO_MS) {
      ultimoPitido = ahora;
      tone(PIN_BUZZER, TONO_ALTO, DURACION_ALTO_MS);
    }
  } else if (patronActual == PATRON_FALLA) {
    int frecuencia = (((ahora / CAMBIO_SIRENA_MS) % 2UL) == 0UL) ? TONO_SIRENA_ALTO : TONO_SIRENA_BAJO;
    if (frecuencia != ultimaFrecuencia) {
      ultimaFrecuencia = frecuencia;
      tone(PIN_BUZZER, frecuencia);
    }
  }
}

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
  // Fila 1: nivel en porcentaje y su clasificacion.
  escribirFila(0, "Nivel " + String(nivelPorc) + "% " + String(textoNivel()));

  // Fila 2: falla, o estado de la bomba y modo.
  if (falla) {
    escribirFila(1, "FALLA: revisar");
  } else {
    String bomba = bombaEncendida ? "Bomba ON  " : "Bomba --  ";
    String texto = (modo == MODO_AUTOMATICO) ? "Auto" : "Manual";
    escribirFila(1, bomba + texto);
  }
}

// Escribe un resumen en el Monitor Serial.
void imprimirResumen() {
  Serial.print("Modo: ");
  Serial.print(textoModo());
  Serial.print(" | Nivel: ");
  Serial.print(nivelPorc);
  Serial.print(" % (");
  Serial.print(textoNivel());
  Serial.print(") | Bruto: ");
  Serial.print(nivelBruto);
  Serial.print(" | Bomba: ");
  Serial.print(bombaEncendida ? "ON" : "OFF");
  Serial.print(" | Falla: ");
  Serial.print(falla ? "SI" : "no");
  Serial.print(" | Llenados: ");
  Serial.println(llenadosTotales);
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
  Serial.println("Sin red: el control del tanque funciona igual, pero sin pagina web.");
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

// Envia la pagina con el nivel, el estado y los botones.
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
  cliente.println("<title>Tanque anti-desborde - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;");
  cliente.println("margin:0;padding:20px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".tarjeta{max-width:320px;margin:12px auto;padding:14px;");
  cliente.println("border-radius:12px;background:#E3F4F6}");
  cliente.println(".tarjeta h2{margin:0;font-size:15px;color:#00606A}");
  cliente.println(".valor{font-size:40px;font-weight:bold;color:#007A86}");
  cliente.println(".chico{font-size:18px}");
  cliente.println(".alerta{background:#FBE3E3}.alerta .valor{color:#C62828}");
  cliente.println(".aviso{background:#FFF3D6}.aviso .valor{color:#B07A00}");
  cliente.println(".barra{background:#cfd8da;border-radius:8px;height:18px;margin-top:8px}");
  cliente.println(".relleno{background:#007A86;height:18px;border-radius:8px}");
  cliente.println(".boton{display:inline-block;margin:8px 4px 0;padding:10px 18px;");
  cliente.println("border-radius:8px;background:#007A86;color:#fff;");
  cliente.println("text-decoration:none;font-weight:bold}");
  cliente.println("p{font-size:13px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Tanque anti-desborde</h1>");
  cliente.println("<p>Arduino UNO R4 WiFi - Curso Arduino UAM</p>");

  // Nivel.
  cliente.print("<div class=\"tarjeta");
  if (estadoNivel == NIVEL_ALTO) {
    cliente.print(" alerta");
  } else if (estadoNivel == NIVEL_BAJO) {
    cliente.print(" aviso");
  }
  cliente.print("\"><h2>Nivel del tanque</h2><div class=\"valor\">");
  cliente.print(nivelPorc);
  cliente.print(" %</div><div class=\"chico\">");
  cliente.print(textoNivel());
  cliente.print("</div><div class=\"barra\"><div class=\"relleno\" style=\"width:");
  cliente.print(nivelPorc);
  cliente.println("%\"></div></div></div>");

  // Falla.
  if (falla) {
    cliente.println("<div class=\"tarjeta alerta\"><h2>FALLA DE LA BOMBA</h2>");
    cliente.println("<div class=\"chico\">La bomba trabajo 20 s sin subir el nivel.</div>");
    cliente.println("<a class=\"boton\" href=\"/reconocer\">Reconocer falla</a></div>");
  }

  // Bomba y modo.
  cliente.print("<div class=\"tarjeta\"><h2>Bomba</h2><div class=\"valor chico\">");
  cliente.print(bombaEncendida ? "ENCENDIDA" : "APAGADA");
  cliente.print("</div><div>Modo: ");
  cliente.print(textoModo());
  cliente.println("</div>");
  if (modo == MODO_AUTOMATICO) {
    cliente.println("<a class=\"boton\" href=\"/manual\">Pasar a manual</a>");
  } else {
    cliente.println("<a class=\"boton\" href=\"/auto\">Pasar a automatico</a>");
    cliente.print("<a class=\"boton\" href=\"/bomba\">");
    cliente.print(bombaEncendida ? "Apagar bomba" : "Encender bomba");
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
  } else if (ruta == "/bomba") {
    // Solo en modo manual: alterna la bomba.
    if (modo == MODO_MANUAL) {
      if (bombaEncendida) {
        apagarBomba("orden manual");
      } else if (falla) {
        registrarEvento("Bomba rechazada: hay una falla");
      } else if (estadoNivel == NIVEL_ALTO) {
        registrarEvento("Bomba rechazada: nivel alto");
      } else {
        encenderBomba("orden manual");
      }
    }
    enviarRedireccion(cliente);
  } else if (ruta == "/reconocer") {
    reconocerFalla();
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
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Pantalla LCD.
  if (lcd.begin(16, 2) == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  Serial.println("TANQUE ANTI-DESBORDE - ARDUINO UNO R4 WIFI");
  Serial.println("------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  escribirFila(0, "Tanque UAM");
  escribirFila(1, "Midiendo...");

  // Primera medicion: se clasifica directo, sin histeresis.
  nivelBruto = leerNivelBruto();
  nivelPorc = aPorcentaje(nivelBruto);
  estadoNivel = clasificarDirecto(nivelPorc);
  Serial.print("Nivel inicial: ");
  Serial.print(nivelPorc);
  Serial.print(" % (");
  Serial.print(textoNivel());
  Serial.println(")");

  escribirFila(1, "Conectando WiFi");
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("No se detecta el modulo WiFi: el control funciona sin pagina web.");
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

  registrarEvento("Control de tanque listo");
  ultimaMedida = millis();
  ultimaRevisionRed = millis();
}

// ============================================================================
// 9. LOOP
// ============================================================================

void loop() {
  unsigned long ahora = millis();

  // Medicion periodica del nivel.
  if (ahora - ultimaMedida >= INTERVALO_MEDIDA_MS) {
    ultimaMedida = ahora;
    medirNivel();
  }

  // Control, salidas y pantalla (en cada vuelta).
  controlarBomba();
  actualizarSemaforo();
  actualizarBuzzer();
  actualizarPantalla();

  // Resumen cada 5 s.
  if (ahora - ultimoResumen >= INTERVALO_RESUMEN_MS) {
    ultimoResumen = ahora;
    imprimirResumen();
  }

  // Reconexion WiFi: solo con la bomba apagada y sin falla, porque
  // reconectar bloquea la placa varios segundos.
  if (ahora - ultimaRevisionRed >= INTERVALO_REVISION_RED_MS) {
    ultimaRevisionRed = ahora;
    if (!bombaEncendida && !falla && WiFi.status() != WL_CONNECTED && WiFi.status() != WL_NO_MODULE) {
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
