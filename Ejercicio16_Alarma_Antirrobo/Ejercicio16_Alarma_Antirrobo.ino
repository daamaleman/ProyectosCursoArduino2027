/*
  ============================================================================
  EJERCICIO 16: ALARMA ANTIRROBO MULTISENSOR
  Curso Arduino IoT y Domotica - Universidad Americana (UAM)
  ============================================================================

  PLACA:        Arduino UNO R4 WiFi
  COMPONENTES:  - Sensor PIR Keyestudio (S, V, G)            -> zona MOVIMIENTO
                - Modulo reed (puerta) con iman (S, V, G)    -> zona PUERTA
                - Sensor de vibracion piezoelectrico (S, V, G) -> zona VIBRACION
                - Modulo buzzer pasivo Keyestudio (S, V, G)
                - Modulo semaforo Keyestudio (GND, G, Y, R)
                - Pantalla LCD 16x2 con adaptador I2C (GND, VCC, SDA, SCL)
                - Modulo boton (pulsador) (S, V, G)
  BIBLIOTECAS:  "hd44780" de Bill Perry (LCD I2C). WiFiS3 viene con la placa.
  ARCHIVOS:     Necesita "arduino_secrets.h" en la misma carpeta (nombre y
                clave de la red WiFi) para la pagina web.

  CONEXIONES:   PIR        S -> D2     V -> 5V   G -> GND
                Reed       S -> D3     V -> 5V   G -> GND
                Boton      S -> D4     V -> 5V   G -> GND
                Buzzer     S -> D5     V -> 5V   G -> GND
                Semaforo   G (verde) -> D6   Y (amarillo) -> D7
                           R (rojo)  -> D8   GND -> GND
                Vibracion  S -> A0     V -> 5V   G -> GND
                LCD I2C    SDA -> A4   SCL -> A5   VCC -> 5V   GND -> GND

  QUE HACE:     MAQUINA DE ESTADOS con cuatro estados:
                  DESARMADA (semaforo verde)   : no vigila nada.
                  SALIENDO  (amarillo parpadea): 10 s para salir; el boton
                                                 cancela.
                  ARMADA    (amarillo fijo)    : vigila las tres zonas.
                  ALARMA    (rojo parpadea)    : suena la sirena y la LCD
                                                 muestra la zona que disparo.
                El boton arma (desde DESARMADA) y desarma (desde SALIENDO,
                ARMADA o ALARMA).
                La pagina web local (solo lectura) muestra el estado y el
                ultimo evento. La alarma NO depende del WiFi.

  SEGURIDAD:    Es una maqueta didactica. Desarma cualquiera que pulse el
                boton y la pagina no pide usuario ni clave. Un sistema real
                usaria codigo de acceso, sensores supervisados y bateria.

  MONITOR SERIAL: 9600 baudios.
  ============================================================================
*/

// ============================================================================
// 1. BIBLIOTECAS Y DATOS DE LA RED
// ============================================================================

// Bus I2C (incluida en el nucleo de la placa).
#include <Wire.h>
// Pantalla LCD (biblioteca "hd44780" de Bill Perry).
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
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

// Entradas digitales.
const int PIN_PIR   = 2;   // Sensor PIR (HIGH con movimiento).
const int PIN_REED  = 3;   // Modulo reed de la puerta.
const int PIN_BOTON = 4;   // Boton de armar y desarmar.

// Salidas.
const int PIN_BUZZER   = 5;   // Buzzer pasivo (se maneja con tone()).
const int PIN_VERDE    = 6;   // Semaforo: verde.
const int PIN_AMARILLO = 7;   // Semaforo: amarillo.
const int PIN_ROJO     = 8;   // Semaforo: rojo.

// Entrada analogica del sensor de vibracion.
const int PIN_VIBRACION = A0;

// Nivel que entrega el modulo reed cuando la PUERTA ESTA ABIERTA (el iman
// esta lejos). Segun la documentacion de Keyestudio, con el iman cerca el
// modulo entrega LOW, asi que abierta = HIGH. Si su modulo es al reves
// (la alarma suena con la puerta cerrada), cambie este valor a LOW.
const int NIVEL_PUERTA_ABIERTA = HIGH;

// Lectura (0 a 1023) a partir de la cual se considera una vibracion.
// Es un valor de ejemplo: calibrelo con SU sensor (ver la guia).
const int UMBRAL_VIBRACION = 100;

// Tiempo para salir despues de pulsar el boton (ms).
const unsigned long SALIDA_MS = 10000UL;

// Tiempo que necesita el PIR para estabilizarse al encender (ms).
const unsigned long ESTABILIZACION_PIR_MS = 30000UL;

// Cuanto suena la sirena despues de una alarma (ms). Luego se calla, pero
// el sistema sigue en ALARMA hasta que se desarme.
const unsigned long DURACION_SIRENA_MS = 30000UL;

// Tiempo de antirrebote del boton (ms).
const unsigned long ANTIREBOTE_MS = 50UL;

// Cuanto tiempo se muestra un mensaje temporal en la LCD (ms).
const unsigned long MENSAJE_MS = 2500UL;

// Cada cuantos ms se escribe un resumen en el Monitor Serial.
const unsigned long INTERVALO_RESUMEN_MS = 5000UL;

// Frecuencias del buzzer (Hz).
const int TONO_SIRENA_ALTO = 1500;
const int TONO_SIRENA_BAJO = 1000;
const int TONO_CONFIRMAR   = 2000;
const int TONO_TICK        = 1800;
const int TONO_ERROR       = 400;

// Estados de la maquina.
const int ESTADO_DESARMADA = 0;
const int ESTADO_SALIENDO  = 1;
const int ESTADO_ARMADA    = 2;
const int ESTADO_ALARMA    = 3;

// Zonas (cada una es un bit, para poder combinarlas).
const byte ZONA_PUERTA     = 1;
const byte ZONA_MOVIMIENTO = 2;
const byte ZONA_VIBRACION  = 4;

// Cada cuantos segundos el navegador recarga la pagina solo.
const int SEGUNDOS_RECARGA = 2;

// Intentos y pausa para conectarse a la red (solo al arrancar y en DESARMADA).
const int INTENTOS_CONEXION = 3;
const unsigned long PAUSA_INTENTO_MS = 5000UL;

// Tiempo maximo para recibir la peticion de un cliente (ms). Es corto porque
// mientras espera, la alarma no vigila.
const unsigned long ESPERA_CLIENTE_MS = 500UL;

// Cada cuantos ms se comprueba que la red siga conectada.
const unsigned long INTERVALO_REVISION_RED_MS = 10000UL;

// Largo maximo que se guarda de cada linea de la peticion (caracteres).
const unsigned int LARGO_MAXIMO_LINEA = 100;

// ============================================================================
// 3. OBJETOS Y VARIABLES DE ESTADO
// ============================================================================

// Pantalla LCD I2C. La biblioteca busca sola la direccion.
hd44780_I2Cexp lcd;
bool lcdDisponible = false;

// Servidor web en el puerto 80.
WiFiServer servidor(80);

// Estado actual de la maquina y marcas de tiempo de cada estado.
int estado = ESTADO_DESARMADA;
unsigned long inicioSalida = 0;
unsigned long inicioAlarma = 0;
int ultimoTick = -1;

// Zonas disparadas (combinadas) y zona que disparo primero.
byte zonasDisparadas = 0;
byte zonaPrincipal = 0;

// El PIR solo cuenta despues de verlo en LOW una vez armado el sistema:
// asi se ignora el movimiento de quien acaba de salir.
bool pirLiberado = false;

// Ultimas lecturas.
int lecturaVibracion = 0;
int picoVibracion = 0;

// Ultimo evento (para la pagina) y cuando ocurrio.
String ultimoEvento = "Ninguno";
unsigned long tiempoUltimoEvento = 0;

// Mensaje temporal de la LCD (segunda fila).
String mensajeTemporal = "";
unsigned long mensajeHasta = 0;

// Texto actual de cada fila de la LCD (para escribir solo si cambia).
String filaActual[2] = {"", ""};

// Boton: nivel en reposo (se aprende al arrancar) y antirrebote.
int nivelBotonReposo = LOW;
int estadoEstableBoton = LOW;
int ultimaLecturaBoton = LOW;
unsigned long ultimoCambioBoton = 0;

// Frecuencia de la sirena que suena ahora (0 = silencio).
int frecuenciaSirena = 0;

// Instantes (millis) de las tareas periodicas.
unsigned long ultimoResumen = 0;
unsigned long ultimaRevisionRed = 0;

// ============================================================================
// 4. FUNCIONES PROPIAS: ENTRADAS
// ============================================================================

// true si la puerta esta abierta (el iman esta lejos del reed).
bool puertaAbierta() {
  return digitalRead(PIN_REED) == NIVEL_PUERTA_ABIERTA;
}

// true mientras el PIR se estabiliza despues de encender la placa.
bool pirEstabilizando() {
  return millis() < ESTABILIZACION_PIR_MS;
}

// Devuelve true UNA sola vez por cada pulsacion del boton (con antirrebote).
bool botonPulsado() {
  int lectura = digitalRead(PIN_BOTON);
  if (lectura != ultimaLecturaBoton) {
    ultimaLecturaBoton = lectura;
    ultimoCambioBoton = millis();
  }
  // Si la lectura lleva estable el tiempo de antirrebote y es nueva:
  if ((millis() - ultimoCambioBoton) >= ANTIREBOTE_MS && lectura != estadoEstableBoton) {
    estadoEstableBoton = lectura;
    // Es una pulsacion si el nivel es distinto del nivel en reposo.
    if (estadoEstableBoton != nivelBotonReposo) {
      return true;
    }
  }
  return false;
}

// Devuelve las zonas que estan activas en este momento (varios bits).
byte leerZonasActivas() {
  byte zonas = 0;
  if (puertaAbierta()) {
    zonas |= ZONA_PUERTA;
  }
  if (pirLiberado && digitalRead(PIN_PIR) == HIGH) {
    zonas |= ZONA_MOVIMIENTO;
  }
  if (lecturaVibracion >= UMBRAL_VIBRACION) {
    zonas |= ZONA_VIBRACION;
  }
  return zonas;
}

// ============================================================================
// 5. FUNCIONES PROPIAS: TEXTOS, LCD Y EVENTOS
// ============================================================================

// Texto del estado para la pagina y el Monitor Serial.
const char* textoEstado() {
  if (estado == ESTADO_DESARMADA) {
    return "DESARMADA";
  }
  if (estado == ESTADO_SALIENDO) {
    return "SALIENDO";
  }
  if (estado == ESTADO_ARMADA) {
    return "ARMADA";
  }
  return "ALARMA";
}

// Clase de estilo de la pagina para cada estado.
const char* claseEstado() {
  if (estado == ESTADO_DESARMADA) {
    return "des";
  }
  if (estado == ESTADO_ALARMA) {
    return "ala";
  }
  return "arm";
}

// Nombre de una zona (una sola) para la LCD.
String nombreZona(byte zona) {
  if (zona == ZONA_PUERTA) {
    return "PUERTA";
  }
  if (zona == ZONA_MOVIMIENTO) {
    return "MOVIMIENTO";
  }
  if (zona == ZONA_VIBRACION) {
    return "VIBRACION";
  }
  return "-";
}

// Lista de zonas disparadas, por ejemplo "PUERTA, VIBRACION".
String textoZonas(byte mascara) {
  String texto = "";
  if (mascara & ZONA_PUERTA) {
    texto += "PUERTA";
  }
  if (mascara & ZONA_MOVIMIENTO) {
    if (texto.length() > 0) {
      texto += ", ";
    }
    texto += "MOVIMIENTO";
  }
  if (mascara & ZONA_VIBRACION) {
    if (texto.length() > 0) {
      texto += ", ";
    }
    texto += "VIBRACION";
  }
  if (texto.length() == 0) {
    texto = "Ninguna";
  }
  return texto;
}

// Segundos que faltan para terminar el tiempo de salida.
int segundosRestantes() {
  unsigned long transcurrido = millis() - inicioSalida;
  if (transcurrido >= SALIDA_MS) {
    return 0;
  }
  return (int)((SALIDA_MS - transcurrido + 999UL) / 1000UL);
}

// Escribe un texto en una fila de la LCD, solo si cambio. Se completa con
// espacios hasta 16 caracteres para borrar restos del texto anterior.
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

// Muestra un mensaje temporal en la segunda fila de la LCD.
void mostrarMensaje(String texto) {
  mensajeTemporal = texto;
  mensajeHasta = millis() + MENSAJE_MS;
}

// Anota el ultimo evento, para la pagina y el Monitor Serial.
void registrarEvento(String texto) {
  ultimoEvento = texto;
  tiempoUltimoEvento = millis();
  Serial.print("Evento: ");
  Serial.println(texto);
}

// Actualiza las dos filas de la LCD segun el estado.
void actualizarPantalla() {
  if (estado == ESTADO_DESARMADA) {
    escribirFila(0, "DESARMADA");
    if (millis() < mensajeHasta) {
      escribirFila(1, mensajeTemporal);
    } else if (pirEstabilizando()) {
      escribirFila(1, "PIR calentando");
    } else {
      escribirFila(1, "Boton: armar");
    }
  } else if (estado == ESTADO_SALIENDO) {
    escribirFila(0, "SALIENDO...");
    escribirFila(1, "Armando en " + String(segundosRestantes()) + " s");
  } else if (estado == ESTADO_ARMADA) {
    escribirFila(0, "ARMADA");
    escribirFila(1, "Vigilando");
  } else {
    escribirFila(0, "!!! ALARMA !!!");
    escribirFila(1, "Zona: " + nombreZona(zonaPrincipal));
  }
}

// ============================================================================
// 6. FUNCIONES PROPIAS: SEMAFORO, BUZZER Y MAQUINA DE ESTADOS
// ============================================================================

// Enciende o apaga cada luz del semaforo.
void ponerSemaforo(bool verde, bool amarillo, bool rojo) {
  digitalWrite(PIN_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, amarillo ? HIGH : LOW);
  digitalWrite(PIN_ROJO, rojo ? HIGH : LOW);
}

// Actualiza el semaforo y el buzzer segun el estado.
void actualizarSalidas() {
  unsigned long ahora = millis();
  bool sirenaActiva = (estado == ESTADO_ALARMA) && ((ahora - inicioAlarma) < DURACION_SIRENA_MS);

  // Semaforo.
  if (estado == ESTADO_DESARMADA) {
    ponerSemaforo(true, false, false);
  } else if (estado == ESTADO_SALIENDO) {
    // Amarillo parpadeando cada 500 ms.
    ponerSemaforo(false, ((ahora / 500UL) % 2UL) == 0UL, false);
  } else if (estado == ESTADO_ARMADA) {
    ponerSemaforo(false, true, false);
  } else {
    // Rojo parpadeando mientras suena la sirena y fijo despues.
    bool encendido = true;
    if (sirenaActiva) {
      encendido = ((ahora / 250UL) % 2UL) == 0UL;
    }
    ponerSemaforo(false, false, encendido);
  }

  // Tic de cuenta regresiva: un pitido corto por segundo al salir.
  if (estado == ESTADO_SALIENDO) {
    int segundo = (int)((ahora - inicioSalida) / 1000UL);
    if (segundo != ultimoTick) {
      ultimoTick = segundo;
      tone(PIN_BUZZER, TONO_TICK, 80);
    }
  }

  // Sirena: alterna dos tonos cada 300 ms.
  if (sirenaActiva) {
    int frecuencia = (((ahora / 300UL) % 2UL) == 0UL) ? TONO_SIRENA_ALTO : TONO_SIRENA_BAJO;
    if (frecuencia != frecuenciaSirena) {
      frecuenciaSirena = frecuencia;
      tone(PIN_BUZZER, frecuencia);
    }
  } else if (frecuenciaSirena != 0) {
    frecuenciaSirena = 0;
    noTone(PIN_BUZZER);
  }
}

// Pasa a DESARMADA y limpia las zonas.
void desarmar(const char* motivo) {
  estado = ESTADO_DESARMADA;
  zonasDisparadas = 0;
  zonaPrincipal = 0;
  registrarEvento(motivo);
}

// Dispara la alarma por una o varias zonas.
void dispararAlarma(byte zonas) {
  estado = ESTADO_ALARMA;
  inicioAlarma = millis();
  zonasDisparadas = zonas;
  // La zona principal es la primera por prioridad: puerta, movimiento, vibracion.
  if (zonas & ZONA_PUERTA) {
    zonaPrincipal = ZONA_PUERTA;
  } else if (zonas & ZONA_MOVIMIENTO) {
    zonaPrincipal = ZONA_MOVIMIENTO;
  } else {
    zonaPrincipal = ZONA_VIBRACION;
  }
  registrarEvento("ALARMA: " + nombreZona(zonaPrincipal));
}

// Termina el tiempo de salida: arma el sistema si la puerta esta cerrada.
void terminarSalida() {
  if (puertaAbierta()) {
    // No se puede armar con la puerta abierta.
    mostrarMensaje("Puerta abierta");
    tone(PIN_BUZZER, TONO_ERROR, 500);
    desarmar("No se armo: la puerta seguia abierta");
    return;
  }
  estado = ESTADO_ARMADA;
  pirLiberado = false;
  zonasDisparadas = 0;
  zonaPrincipal = 0;
  tone(PIN_BUZZER, TONO_CONFIRMAR, 300);
  registrarEvento("Sistema armado");
}

// Atiende una pulsacion del boton segun el estado.
void atenderBoton() {
  if (estado == ESTADO_DESARMADA) {
    if (pirEstabilizando()) {
      mostrarMensaje("PIR calentando");
      tone(PIN_BUZZER, TONO_ERROR, 300);
      registrarEvento("No se armo: el PIR se esta estabilizando");
    } else if (puertaAbierta()) {
      mostrarMensaje("Puerta abierta");
      tone(PIN_BUZZER, TONO_ERROR, 300);
      registrarEvento("No se armo: puerta abierta");
    } else {
      estado = ESTADO_SALIENDO;
      inicioSalida = millis();
      ultimoTick = -1;
      registrarEvento("Salida iniciada (10 s)");
    }
  } else if (estado == ESTADO_SALIENDO) {
    desarmar("Armado cancelado");
    tone(PIN_BUZZER, 1200, 150);
  } else {
    // ARMADA o ALARMA.
    desarmar("Sistema desarmado");
    tone(PIN_BUZZER, 1200, 150);
  }
}

// Vigila las zonas mientras el sistema esta armado o en alarma.
void vigilar() {
  // El PIR solo se tiene en cuenta despues de verlo en LOW una vez.
  if (!pirLiberado && digitalRead(PIN_PIR) == LOW) {
    pirLiberado = true;
  }
  byte zonas = leerZonasActivas();
  if (estado == ESTADO_ARMADA && zonas != 0) {
    dispararAlarma(zonas);
  } else if (estado == ESTADO_ALARMA) {
    // Ya en alarma: se agregan las otras zonas que se activen.
    zonasDisparadas |= zonas;
  }
}

// Escribe un resumen del estado en el Monitor Serial.
void imprimirResumen() {
  Serial.print("Estado: ");
  Serial.print(textoEstado());
  Serial.print(" | Puerta: ");
  Serial.print(puertaAbierta() ? "ABIERTA" : "CERRADA");
  Serial.print(" | PIR: ");
  Serial.print(digitalRead(PIN_PIR) == HIGH ? "HIGH" : "LOW");
  Serial.print(" | Vibracion (pico): ");
  Serial.println(picoVibracion);
  picoVibracion = 0;
}

// ============================================================================
// 7. FUNCIONES PROPIAS: RED Y PAGINA WEB
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
  Serial.println("Sin red: la alarma funciona igual, pero sin pagina web.");
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

// Envia la pagina de estado (solo lectura).
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
  cliente.println("<title>Alarma antirrobo - UAM</title>");
  cliente.println("<style>");
  cliente.println("body{font-family:Arial,sans-serif;text-align:center;");
  cliente.println("margin:0;padding:20px;color:#3A3D40}");
  cliente.println("h1{color:#00606A;font-size:24px}");
  cliente.println(".tarjeta{max-width:320px;margin:12px auto;padding:14px;");
  cliente.println("border-radius:12px;background:#E3F4F6}");
  cliente.println(".tarjeta h2{margin:0;font-size:15px;color:#00606A}");
  cliente.println(".valor{font-size:28px;font-weight:bold;color:#007A86}");
  cliente.println(".arm{background:#FFF3D6}.arm .valor{color:#B07A00}");
  cliente.println(".ala{background:#FBE3E3}.ala .valor{color:#C62828}");
  cliente.println(".chico{font-size:18px}");
  cliente.println("p{font-size:13px;color:#7F8184}");
  cliente.println("</style></head><body>");
  cliente.println("<h1>Alarma antirrobo</h1>");
  cliente.println("<p>Arduino UNO R4 WiFi - Curso Arduino UAM</p>");

  // Tarjeta del estado (su clase cambia el color).
  cliente.print("<div class=\"tarjeta ");
  cliente.print(claseEstado());
  cliente.print("\"><h2>Estado</h2><div class=\"valor\">");
  cliente.print(textoEstado());
  cliente.println("</div>");
  if (estado == ESTADO_SALIENDO) {
    cliente.print("<div>Armando en ");
    cliente.print(segundosRestantes());
    cliente.println(" s</div>");
  }
  cliente.println("</div>");

  // Tarjeta de las zonas disparadas.
  cliente.print("<div class=\"tarjeta\"><h2>Zonas disparadas</h2>");
  cliente.print("<div class=\"valor chico\">");
  cliente.print(textoZonas(zonasDisparadas));
  cliente.println("</div></div>");

  // Tarjeta del ultimo evento.
  cliente.print("<div class=\"tarjeta\"><h2>Ultimo evento</h2>");
  cliente.print("<div class=\"valor chico\">");
  cliente.print(ultimoEvento);
  cliente.print("</div><div>hace ");
  cliente.print((millis() - tiempoUltimoEvento) / 1000UL);
  cliente.println(" s</div></div>");

  // Tarjeta de los sensores.
  cliente.print("<div class=\"tarjeta\"><h2>Sensores ahora</h2><div>Puerta: ");
  cliente.print(puertaAbierta() ? "ABIERTA" : "CERRADA");
  cliente.print("</div><div>PIR: ");
  if (pirEstabilizando()) {
    cliente.print("estabilizando");
  } else {
    cliente.print(digitalRead(PIN_PIR) == HIGH ? "movimiento" : "sin movimiento");
  }
  cliente.println("</div></div>");

  cliente.print("<p>Solo lectura: esta pagina no arma ni desarma. Se actualiza sola cada ");
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

// Atiende a un cliente: lee la peticion y responde.
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

  // Solo existe la pagina principal; cualquier otra ruta da 404.
  String ruta = extraerRuta(primeraLinea);
  if (ruta == "/") {
    enviarPagina(cliente);
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
  // Inicia el Monitor Serial lo primero para no perder mensajes.
  Serial.begin(9600);

  // Salidas: semaforo y buzzer apagados.
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  ponerSemaforo(false, false, false);
  noTone(PIN_BUZZER);

  // Entradas.
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_REED, INPUT);
  pinMode(PIN_BOTON, INPUT);

  // Inicia la pantalla (16 columnas, 2 filas). Devuelve 0 si la encontro.
  int estadoLcd = lcd.begin(16, 2);
  if (estadoLcd == 0) {
    lcdDisponible = true;
    lcd.backlight();
    lcd.clear();
  }

  // Pausa inicial: el Monitor Serial y los sensores necesitan un momento.
  delay(1500);

  // El nivel del boton en reposo se aprende ahora: NO lo pulse al encender.
  nivelBotonReposo = digitalRead(PIN_BOTON);
  estadoEstableBoton = nivelBotonReposo;
  ultimaLecturaBoton = nivelBotonReposo;

  Serial.println("ALARMA ANTIRROBO MULTISENSOR - ARDUINO UNO R4 WIFI");
  Serial.println("--------------------------------------------------");
  if (!lcdDisponible) {
    Serial.println("Aviso: no se encontro la pantalla LCD. Revise SDA y SCL.");
  }
  Serial.println("Estabilizando el sensor PIR (30 s). No se mueva frente a el.");
  Serial.print("Puerta: ");
  Serial.println(puertaAbierta() ? "ABIERTA" : "CERRADA");

  escribirFila(0, "Alarma UAM");
  escribirFila(1, "Conectando WiFi");

  // Comprueba que exista el modulo WiFi de la placa.
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("No se detecta el modulo WiFi: la alarma funciona sin pagina web.");
  } else {
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
  }

  registrarEvento("Sistema encendido (desarmado)");
  ultimaRevisionRed = millis();
}

// ============================================================================
// 9. LOOP
// ============================================================================

void loop() {
  unsigned long ahora = millis();

  // Lectura de la vibracion en cada vuelta (los golpes son muy breves).
  lecturaVibracion = analogRead(PIN_VIBRACION);
  if (lecturaVibracion > picoVibracion) {
    picoVibracion = lecturaVibracion;
  }

  // Boton: arma, cancela o desarma segun el estado.
  if (botonPulsado()) {
    atenderBoton();
  }

  // Fin del tiempo de salida.
  if (estado == ESTADO_SALIENDO && (ahora - inicioSalida) >= SALIDA_MS) {
    terminarSalida();
  }

  // Vigilancia de las zonas.
  if (estado == ESTADO_ARMADA || estado == ESTADO_ALARMA) {
    vigilar();
  }

  // Semaforo, buzzer y pantalla.
  actualizarSalidas();
  actualizarPantalla();

  // Cada 5 s se escribe un resumen en el Monitor Serial.
  if (ahora - ultimoResumen >= INTERVALO_RESUMEN_MS) {
    ultimoResumen = ahora;
    imprimirResumen();
  }

  // Si la red cayo, solo se reconecta con el sistema DESARMADA, porque
  // reconectar bloquea la placa varios segundos y la alarma no vigilaria.
  if (ahora - ultimaRevisionRed >= INTERVALO_REVISION_RED_MS) {
    ultimaRevisionRed = ahora;
    if (estado == ESTADO_DESARMADA && WiFi.status() != WL_CONNECTED && WiFi.status() != WL_NO_MODULE) {
      Serial.println("Sin conexion a la red. Reintentando...");
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