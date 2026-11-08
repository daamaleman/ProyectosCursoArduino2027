/*
  ============================================================================
  EJERCICIO 17: PARQUEO INTELIGENTE CON BARRERA
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor infrarrojo Keyestudio KS0051 (G, V, S, EN)  -> afuera
                - Sensor ultrasonico HC-SR04 (VCC, Trig, Echo, GND)  -> en la barrera
                - Servomotor SG90 de posicion (brazo de la barrera)
                - Fuente regulada de 5 V para el servo
                - Modulo semaforo Keyestudio (GND, G, Y, R)
                - Pantalla LCD 16x2 con adaptador I2C (GND, VCC, SDA, SCL)
  BIBLIOTECAS:  "hd44780" de Bill Perry (LCD I2C). Servo y WiFiS3 vienen
                incluidas con el Arduino IDE / la placa.
  ARCHIVOS:     Necesita "arduino_secrets.h" en la misma carpeta (nombre y
                clave de la red WiFi) para la pagina web.

  CONEXIONES:   Infrarrojo  G -> GND   V -> 5V   S -> D2  (EN: puente puesto)
                HC-SR04     VCC -> 5V  Trig -> D7  Echo -> D8  GND -> GND
                Servo       senal -> D9
                            rojo  -> + de la fuente de 5 V
                            marron -> - de la fuente
                Fuente      negativo -> TAMBIEN a GND del Arduino
                Semaforo    GND -> GND   G (verde) -> D3
                            Y (amarillo) -> D4   R (rojo) -> D5
                LCD I2C     SDA -> A4   SCL -> A5   VCC -> 5V   GND -> GND

  QUE HACE:     Los dos sensores estan en fila: el INFRARROJO ve el vehiculo
                que llega por AFUERA y el ULTRASONICO ve el vehiculo que esta
                EN la barrera. El ORDEN en que se activan indica el sentido:
                    infrarrojo y luego ultrasonico  -> ENTRADA
                    ultrasonico y luego infrarrojo  -> SALIDA
                - Entrada: la barrera abre solo si hay cupo; al pasar, se
                  cuenta una entrada y bajan los cupos libres.
                - Salida: la barrera abre; al pasar, se cuenta una salida y
                  suben los cupos libres.
                - Semaforo: VERDE = hay cupo, ROJO = lleno,
                  AMARILLO = barrera abierta (pasa un vehiculo).
                - La barrera NO baja mientras un sensor vea un vehiculo.
                - La LCD muestra los cupos libres y el estado.
                - La pagina web muestra el cupo en tiempo real y tiene un
                  boton de apertura remota. El parqueo funciona sin WiFi.

  IMPORTANTE:   Durante los 3 primeros segundos NO ponga nada frente al
                sensor infrarrojo. Es una maqueta educativa: no sirve como
                proteccion de personas ni de vehiculos reales. La pagina no
                pide usuario ni clave: cualquiera en la red puede abrir.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <Servo.h>
#include <WiFiS3.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

// ============================================================================
// 2. PINES Y CONSTANTES
// ============================================================================

const int PIN_SENSOR_IR = 2;   // Infrarrojo KS0051 (senal S).
const int PIN_VERDE     = 3;   // Semaforo: verde.
const int PIN_AMARILLO  = 4;   // Semaforo: amarillo.
const int PIN_ROJO      = 5;   // Semaforo: rojo.
const int PIN_TRIG      = 7;   // HC-SR04: Trig.
const int PIN_ECHO      = 8;   // HC-SR04: Echo.
const int PIN_SERVO     = 9;   // Servo de la barrera.

// Cantidad de espacios del parqueo de la maqueta.
const int CAPACIDAD = 4;

// Posiciones del servo (grados) y velocidad de movimiento.
const int POSICION_CERRADA = 0;
const int POSICION_ABIERTA = 90;
const int PASO_GRADOS = 3;
const unsigned long INTERVALO_SERVO_MS = 15UL;

// Un vehiculo esta "en la barrera" si el ultrasonico mide esta distancia o
// menos. Ajustela a su maqueta.
const float UMBRAL_US_CM = 15.0;
// Tiempo maximo de espera del eco (25 ms equivalen a unos 4 m).
const unsigned long LIMITE_ECO_US = 25000UL;
// Cada cuantos ms se mide la distancia.
const unsigned long INTERVALO_MEDIDA_MS = 60UL;
// Lecturas seguidas que hacen falta para dar por valida una deteccion.
const int CONFIRMACIONES_US = 2;

// Tiempo con ambos sensores libres antes de bajar la barrera (ms).
const unsigned long DESPEJE_MS = 1500UL;
// Tiempo minimo que dura abierta una apertura remota (ms).
const unsigned long MINIMO_MANUAL_MS = 3000UL;
// Muestras para aprender el estado del infrarrojo sin objeto.
const int MUESTRAS_INICIALES = 20;

// Cuanto dura un mensaje temporal en la LCD (ms).
const unsigned long MENSAJE_MS = 2500UL;
// Cada cuantos ms se escribe un resumen en el Monitor Serial.
const unsigned long INTERVALO_RESUMEN_MS = 5000UL;

// Estados de la barrera.
const int ESTADO_REPOSO         = 0;  // Cerrada, esperando.
const int ESTADO_ABIERTA_ENTRADA = 1;
const int ESTADO_ABIERTA_SALIDA  = 2;
const int ESTADO_ABIERTA_REMOTA  = 3;

// Pagina web.
const int SEGUNDOS_RECARGA = 2;
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

Servo barrera;
WiFiServer servidor(80);

// Sensores.
int estadoIrLibre = LOW;        // Nivel del infrarrojo SIN objeto (se aprende).
bool irObjeto = false;
bool irAnterior = false;
bool usObjeto = false;
int contadorUs = 0;
float distanciaCm = -1.0;
unsigned long ultimaMedida = 0;

// Servo (se mueve de a poco, sin bloquear).
int posicionServo = POSICION_CERRADA;
int objetivoServo = POSICION_CERRADA;
unsigned long ultimoPasoServo = 0;

// Barrera.
int estado = ESTADO_REPOSO;
bool pasoDetectado = false;      // El segundo sensor ya vio el vehiculo.
unsigned long inicioApertura = 0;
bool contandoLibre = false;      // Ambos sensores libres.
unsigned long libreDesde = 0;

// Conteo.
int ocupados = 0;
unsigned long entradas = 0;
unsigned long salidas = 0;

// Mensajes.
String ultimoEvento = "Ninguno";
unsigned long tiempoUltimoEvento = 0;
String mensajeTemporal = "";
unsigned long mensajeHasta = 0;

unsigned long ultimoResumen = 0;
unsigned long ultimaRevisionRed = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: CUPOS, TEXTOS Y LCD
// ============================================================================

// Cupos libres actuales.
int cuposLibres() {
  return CAPACIDAD - ocupados;
}

// Texto del estado de la barrera.
const char* textoBarrera() {
  if (estado == ESTADO_REPOSO) {
    return "CERRADA";
  }
  if (estado == ESTADO_ABIERTA_ENTRADA) {
    return "ABIERTA (entrada)";
  }
  if (estado == ESTADO_ABIERTA_SALIDA) {
    return "ABIERTA (salida)";
  }
  return "ABIERTA (remota)";
}

// Anota el ultimo evento (para la pagina y el Monitor Serial).
void registrarEvento(String texto) {
  ultimoEvento = texto;
  tiempoUltimoEvento = millis();
  Serial.print("Evento: ");
  Serial.println(texto);
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

// Muestra un mensaje temporal en la segunda fila.
void mostrarMensaje(String texto) {
  mensajeTemporal = texto;
  mensajeHasta = millis() + MENSAJE_MS;
}

// Actualiza las dos filas de la LCD.
void actualizarPantalla() {
  // Fila 1: cupos.
  if (cuposLibres() <= 0) {
    escribirFila(0, "PARQUEO LLENO");
  } else {
    escribirFila(0, "Libres: " + String(cuposLibres()) + " de " + String(CAPACIDAD));
  }
  // Fila 2: estado.
  if (estado == ESTADO_ABIERTA_ENTRADA) {
    escribirFila(1, "Pase, bienvenido");
  } else if (estado == ESTADO_ABIERTA_SALIDA) {
    escribirFila(1, "Buen viaje");
  } else if (estado == ESTADO_ABIERTA_REMOTA) {
    escribirFila(1, "Apertura remota");
  } else if (millis() < mensajeHasta) {
    escribirFila(1, mensajeTemporal);
  } else if (cuposLibres() <= 0) {
    escribirFila(1, "Sin cupo");
  } else {
    escribirFila(1, "Bienvenido");
  }
}

// ============================================================================
// 5. FUNCIONES PROPIAS: SENSORES, SERVO Y SEMAFORO
// ============================================================================

// Mide la distancia con el HC-SR04. Devuelve -1 si no hay eco.
float medirDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long duracionUs = pulseIn(PIN_ECHO, HIGH, LIMITE_ECO_US);
  if (duracionUs == 0) {
    return -1.0;
  }
  return duracionUs * 0.0343 / 2.0;
}

// Lee los dos sensores y actualiza irObjeto y usObjeto.
void leerSensores() {
  // Infrarrojo: hay objeto si el nivel es distinto al de reposo.
  irObjeto = (digitalRead(PIN_SENSOR_IR) != estadoIrLibre);

  // Ultrasonico: se mide cada INTERVALO_MEDIDA_MS.
  unsigned long ahora = millis();
  if (ahora - ultimaMedida >= INTERVALO_MEDIDA_MS) {
    ultimaMedida = ahora;
    distanciaCm = medirDistanciaCm();
    bool cerca = (distanciaCm > 0.0 && distanciaCm <= UMBRAL_US_CM);
    if (cerca) {
      if (contadorUs < CONFIRMACIONES_US) {
        contadorUs++;
      }
    } else {
      contadorUs = 0;
    }
    usObjeto = (contadorUs >= CONFIRMACIONES_US);
  }
}

// Mueve el servo de a PASO_GRADOS hacia el objetivo, sin bloquear.
void moverServo() {
  unsigned long ahora = millis();
  if (posicionServo == objetivoServo || ahora - ultimoPasoServo < INTERVALO_SERVO_MS) {
    return;
  }
  ultimoPasoServo = ahora;
  if (posicionServo < objetivoServo) {
    posicionServo = min(posicionServo + PASO_GRADOS, objetivoServo);
  } else {
    posicionServo = max(posicionServo - PASO_GRADOS, objetivoServo);
  }
  barrera.write(posicionServo);
}

// Semaforo: verde = hay cupo, rojo = lleno, amarillo = barrera abierta.
void actualizarSemaforo() {
  bool verde = false;
  bool amarillo = false;
  bool rojo = false;
  if (estado != ESTADO_REPOSO) {
    amarillo = true;
  } else if (cuposLibres() > 0) {
    verde = true;
  } else {
    rojo = true;
  }
  digitalWrite(PIN_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, amarillo ? HIGH : LOW);
  digitalWrite(PIN_ROJO, rojo ? HIGH : LOW);
}

// ============================================================================
// 6. FUNCIONES PROPIAS: LOGICA DE LA BARRERA
// ============================================================================

// Abre la barrera en el modo indicado (entrada, salida o remota).
void abrirBarrera(int nuevoEstado) {
  estado = nuevoEstado;
  objetivoServo = POSICION_ABIERTA;
  pasoDetectado = false;
  contandoLibre = false;
  inicioApertura = millis();
  if (nuevoEstado == ESTADO_ABIERTA_ENTRADA) {
    registrarEvento("Barrera abierta: entrada");
  } else if (nuevoEstado == ESTADO_ABIERTA_SALIDA) {
    registrarEvento("Barrera abierta: salida");
  } else {
    registrarEvento("Barrera abierta desde la pagina web");
  }
}

// Cierra la barrera y cuenta el vehiculo si paso.
void cerrarBarrera() {
  objetivoServo = POSICION_CERRADA;
  if (estado == ESTADO_ABIERTA_ENTRADA) {
    if (pasoDetectado) {
      ocupados++;
      entradas++;
      registrarEvento("Entrada registrada. Libres: " + String(cuposLibres()));
    } else {
      registrarEvento("Entrada cancelada: el vehiculo no paso");
    }
  } else if (estado == ESTADO_ABIERTA_SALIDA) {
    if (pasoDetectado) {
      if (ocupados > 0) {
        ocupados--;
      }
      salidas++;
      registrarEvento("Salida registrada. Libres: " + String(cuposLibres()));
    } else {
      registrarEvento("Salida cancelada: el vehiculo no paso");
    }
  } else {
    registrarEvento("Apertura remota terminada");
  }
  estado = ESTADO_REPOSO;
  contandoLibre = false;
}

// Decide si hay que abrir (barrera cerrada y esperando).
void atenderReposo() {
  // Aviso de lleno: solo en el instante en que el infrarrojo detecta.
  if (irObjeto && !irAnterior && cuposLibres() <= 0 && !usObjeto) {
    mostrarMensaje("Lleno: espere");
    registrarEvento("Vehiculo en la entrada pero no hay cupo");
  }
  if (irObjeto && !usObjeto) {
    // Llega un vehiculo por afuera: entrada si hay cupo.
    if (cuposLibres() > 0) {
      abrirBarrera(ESTADO_ABIERTA_ENTRADA);
    }
  } else if (usObjeto && !irObjeto) {
    // Un vehiculo en la barrera desde adentro: salida.
    abrirBarrera(ESTADO_ABIERTA_SALIDA);
  }
  // Si ambos ven algo a la vez no se sabe el sentido: se espera.
}

// Vigila el paso del vehiculo y decide cuando bajar la barrera.
void atenderApertura() {
  // El segundo sensor confirma que el vehiculo paso.
  if (estado == ESTADO_ABIERTA_ENTRADA && usObjeto) {
    pasoDetectado = true;
  }
  if (estado == ESTADO_ABIERTA_SALIDA && irObjeto) {
    pasoDetectado = true;
  }
  // Tiempo con ambos sensores libres.
  bool libre = (!irObjeto && !usObjeto);
  unsigned long ahora = millis();
  if (!libre) {
    contandoLibre = false;
  } else if (!contandoLibre) {
    contandoLibre = true;
    libreDesde = ahora;
  }
  bool despejado = contandoLibre && (ahora - libreDesde >= DESPEJE_MS);
  bool tiempoMinimo = (estado != ESTADO_ABIERTA_REMOTA) || (ahora - inicioApertura >= MINIMO_MANUAL_MS);
  if (despejado && tiempoMinimo) {
    cerrarBarrera();
  }
}

// Escribe un resumen en el Monitor Serial.
void imprimirResumen() {
  Serial.print("Barrera: ");
  Serial.print(textoBarrera());
  Serial.print(" | IR: ");
  Serial.print(irObjeto ? "OBJETO" : "libre");
  Serial.print(" | US: ");
  if (distanciaCm < 0) {
    Serial.print("sin eco");
  } else {
    Serial.print((int)distanciaCm);
    Serial.print(" cm");
  }
  Serial.print(" | Ocupados: ");
  Serial.print(ocupados);
  Serial.print("/");
  Serial.print(CAPACIDAD);
  Serial.print(" | Entradas: ");
  Serial.print(entradas);
  Serial.print(" | Salidas: ");
  Serial.println(salidas);
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
  Serial.println("Sin red: el parqueo funciona igual, pero sin pagina web.");
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

// Envia la pagina con el cupo en tiempo real y el boton de apertura.
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
  cliente.println("<title>Parqueo inteligente - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;");
  cliente.println("margin:0;padding:20px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".tarjeta{max-width:320px;margin:12px auto;padding:14px;");
  cliente.println("border-radius:12px;background:#E3F4F6}");
  cliente.println(".tarjeta h2{margin:0;font-size:15px;color:#00606A}");
  cliente.println(".valor{font-size:40px;font-weight:bold;color:#007A86}");
  cliente.println(".chico{font-size:18px}");
  cliente.println(".lleno{background:#FBE3E3}.lleno .valor{color:#C62828}");
  cliente.println(".boton{display:inline-block;margin-top:8px;padding:12px 24px;");
  cliente.println("border-radius:8px;background:#007A86;color:#fff;");
  cliente.println("text-decoration:none;font-weight:bold}");
  cliente.println("p{font-size:13px;color:#7F8184}");