/* ------------------------------------------------------------------------------------------------------------------------------
 *                            -----     Termostato-Broker     -----
 * ------------------------------------------------------------------------------------------------------------------------------
 * Este programa realiza el control de la calefacción mediante un termostato domótico. El dispositivo está formado por:
 * - Wemos D1 mini
 * - Sensor de temperatura I2C SHT30
 * - Pulsador de control
 * - Placa pug&play wemos para conectar el hardware
 * - Relé Shelly 1 como dispositivo de control final
 * En el Wemos se instala un broker mqtt que se encarga de enviar las ordenes al relé de control de la calefacción y se puede usar 
 * para la configuración del termostato. Además también se desarrolla el código de control del termostato.
 * Para la configuración y control del termostato domótico se dispone de un app desarrollada en Blink.
 *
 * ------------------------------------------------------------------------------------------------------------------------------*/

#define BLYNK_TEMPLATE_ID "XXXXXXXXXX"
#define BLYNK_DEVICE_NAME "TermostatoBroker"
#define BLYNK_FIRMWARE_VERSION  "1.0.2"

// Comentar para deshabilitar el Debug de Blynk
#define BLYNK_PRINT Serial   // Defines the object that is used for printing
//#define BLYNK_DEBUG        // Optional, this enables more detailed prints

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "Switch.h"               // Control del pulsador
#include "uMQTTBroker.h"          // Broker mqtt
#include <WEMOS_SHT3X.h>          // Sensor de temperatura
#include <BlynkSimpleEsp8266.h>   // Control externo mediante app
#include <Ticker.h>               // Temporizador funciones asincronas
#include <WidgetRTC.h>            // Fecha, dia y hora
#include <TimeLib.h>              // Fecha, dia y hora
#include <ArduinoOTA.h>           // Actualización por OTA
#include <ESP8266mDNS.h>          // Actualización por OTA
#include <WiFiUdp.h>              // Actualización por OTA
#include "LittleFS.h"             // Sistema archivos WifiManager
#include <ESPAsyncWebServer.h>    // WifiManager
#include <ESPAsyncWiFiManager.h>  // WifiManager         
#include <ArduinoJson.h>          // WifiManager  
#include <ESP8266HTTPClient.h>    // OTA a través de Blynk
#include <ESP8266httpUpdate.h>    // OTA a través de Blynk

//  Comentar para anular del debug serie Termostato-Broker
#define ___DEBUG___
//  Comentar para anular del debug serie correspondiente al Programador horario Termostato-Broker
//#define ___DEBUG2___

// Comentar si no se dispone de sensor SHT30 y el sensor de control es externo mediante mqtt
#define sensor_sht30

// IP fija Termostato Broker
IPAddress ip(192, 168, 1, 230); 
IPAddress gateway(192,168,1,1);
IPAddress subnet (255,255,255,0);
IPAddress dns1(8,8,8,8);

//Token app Blynk 
char blynk_token[34] = "BLYNK_TOKEN";

//Sensor de temperatura I2C SHT30
#ifdef sensor_sht30
  SHT3X sht30(0x45);
  float time_data = 30;     // número de segundos de muestreo del sensor de temperatura
  const int PIN_SDA = D2;                  
  const int PIN_SCL = D1; 
#endif

// Programadores de eventos Ticker
Ticker ticker_Datos, ticker_Ahora, mqttReconnectTimer, wifiReconnectTimer, watchdogSensor, watchdogBroker;

// Gestores de eventos WIFI
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

// Pulsador y led control AUTO
const int PIN_PULSADOR = D3;                                
const int PIN_LED = D4;         

// Objeto pulsador
Switch PulsadorSwitch = Switch(PIN_PULSADOR);

// WifiManager
AsyncWebServer server(80);
DNSServer dns;

    // Variables Globales
float temp_medida;                        // temperatura medida actualmente
float hum_medida;                         // humedad medida actualmente
float temp_setpoint = 21;                 // temperatura a la que se para la calefacción
int Banda_Diferencial = 1;                // intervalo de grados que está funcionando la calefacción (anterior al setpoint)
float ajuste = -3;                        // ajuste del sensor de temperatura (puede ser + o -)
int estado_calefac = 0;                   // estado calefacción -> AUTO / APAGADA
int rele_calefac = 0;                     // estado actual del rele de la calefacción (activado / desactivado)
int mem_rele_calefac = 0;                 // estado anterior del rele de la calefacción (activado / desactivado)
bool inicio = true;                       // control estado inical
bool evalua = false;                      // true -> Cuando hay que actualizar algún valor / false -> no hay nada para actualizar
bool ota = false;                         // actualización por OTA (OTA activa = true)
bool latido = false;                      // Estado rele cada 5 min (true envio activo)
bool watchdog_mqtt;                       // Perro guardiar de buen funcionamiento del Broker 
bool fabrica = false;                     // Para resetear todas las SSID del dispositio
bool shouldSaveConfig = false;            // Permisivo para guardar datos de configuración LittleFS
String client_rele_init = "caldera";      // cliente a controlar (rele shelly 1)
String client_rele;                       // cliente a controlar (rele shelly 1)
String ordenes;                           // ordenes desde la terminal de la app Blink
String overTheAirURL = "";                // URL para OTA desde Blynk

    //Variable del Programador horario
int Programador = 0;                      // Control programador
int Hora_Start_sec1;                      // Hora inicio calefacción en segundos
int Hora_Stop_sec1;                       // Hora paro calefacción en segundos
int Hora_Start_sec2;                      // Hora inicio calefacción en segundos
int Hora_Stop_sec2;                       // Hora paro calefacción en segundos
int Hora_Actual;                          // Hora actual
int Min_Actual;                           // Minuto actual
int Hora_Actual_sec;                      // Hora actual en segundos
String Hora_Start1;                       // Hora inicio Progrogramador 1
String Hora_Stop1;                        // Hora fin Progrogramador 1
String Hora_Start2;                       // Hora inicio Progrogramador 2
String Hora_Stop2;                        // Hora fin Progrogramador 2
String Prog_Dias1;                        // Dias1 programados de calefacción
String Prog_Dias2;                        // Dias2 programados de calefacción
String Dia_Actual;                        // Dia actual

    // topic dispositivo a controlar con la temperatura (en este caso relé shelly 1)
String ReleControl = "cmnd/shelly/POWER";       // Topic que comanda el relé shelly 1
String EstadoReleControl = "stat/shelly/POWER"; // Topic que muestra el estado del relé
String Dispositivo ="tele/shelly/LWT";          // Online -> Cuando está conectado al Termostato-Broker
String EstadoDispositivo = "tele/shelly/STATE"; // Estado del relé cuando está conectado al Termostato-Broker
String Watchdog = "MqttBroker";                 // Estado del Mqtt-Broker

//---------------------------------------------------------------------------------------------------------------------------------------
//              ********************  FUCIONES  ********************
//---------------------------------------------------------------------------------------------------------------------------------------

//Función que obtiene la Hora actual cada 10 segundos  --------------------------------------------------------------------
void Ahora(){

  String Domingo="0";

  Hora_Actual = hour();
  Min_Actual= minute();
  Hora_Actual_sec=(Hora_Actual*3600)+(Min_Actual*60);
  Dia_Actual= String(weekday(now())-1);
  if (Dia_Actual==Domingo){
    Dia_Actual="7";
  }
  //Dias de la semana rutina Ahora() ----------> 1.Domingo 2.Lunes 3.Martes 4.Miercoles 5.Jueves 6.Viernes 7.Sabado
  //Dias de la senama rutina Time input widge -> 1.Lunes 2.Martes 3.Miercoles 4.Jueves 5.Viernes 6.Sabado 7.Domingo
  //Es necesario acomodar el Domingo como 7

  #ifdef ___DEBUG2___
    Serial.print("DIA_Actual_Ahora: "); Serial.println(Dia_Actual);
    Serial.print("Hora_Actual: "); Serial.println(Hora_Actual); 
    Serial.print("Minuto_Actual: "); Serial.println(Min_Actual); 
    Serial.print("Hora_Actual_sec: "); Serial.println(Hora_Actual_sec);    
  #endif 

}

// Funciones app Blink  ---------------------------------------------------------------------------------------------------------

// temperatura app Blink
BLYNK_WRITE(V5){ 
  temp_medida = param.asFloat();
}

// Setpoint app Blink
BLYNK_WRITE(V8){ 
  temp_setpoint = param.asFloat();
  evalua = true;
}

// MODO MANUAL -> AUTO/APAGADA Calefacción app Blink
BLYNK_WRITE(V1){ 
  if (Programador == 0){
    estado_calefac = param.asInt();
    if (estado_calefac)
    evalua = true;
  }
}

// Objeto RTC (reloj)
WidgetRTC rtc; 

// Led estado rele
WidgetLED led(V2); 

// Led estado mqtt
WidgetLED mqtt(V0);

// Terminal
WidgetTerminal terminal(V9); 

// Ordene enviadas desde la terminal de la app Blink
BLYNK_WRITE(V9){
  ordenes = param.asStr();
}

// Horario Programador 1 app Blink
BLYNK_WRITE(V3) {
  int Hora_Start=0;
  int Min_Start=0;
  int Hora_Stop=0;
  int Min_Stop=0;
  
  // Horas programador
  TimeInputParam t(param);

  // Process start time
  if (t.hasStartTime()){
    Hora_Start = t.getStartHour(); 
    Min_Start = t.getStartMinute();
  }
  
  // Process stop time
  if (t.hasStopTime()) {
    Hora_Stop = t.getStopHour();
    Min_Stop = t.getStopMinute();
  }
  
  Hora_Start_sec1=(Hora_Start*3600)+(Min_Start*60);
  Hora_Stop_sec1=(Hora_Stop*3600)+(Min_Stop*60);

  // Process weekdays (1. Lunes 2. Martes 3. Miercoles 4. Jueves 5. Viernes 6.Sabado 7.Domingo)
  Prog_Dias1="";
  for (int i = 1; i <= 7; i++) {
    if (t.isWeekdaySelected(i)) {
      Prog_Dias1 = Prog_Dias1 + i;
    }
  }

  if (Min_Start<10){
    Hora_Start1 =  String(Hora_Start) + ":0" + String(Min_Start);
  }
  else{
    Hora_Start1 =  String(Hora_Start) + ":" + String(Min_Start);
  }
  if (Min_Stop<10){
    Hora_Stop1 = String(Hora_Stop) + ":0" + String(Min_Stop);
  }
  else{
    Hora_Stop1 = String(Hora_Stop) + ":" + String(Min_Stop);
  }

}

// Horario Programador 2 app Blink
BLYNK_WRITE(V7) {
  int Hora_Start=0;
  int Min_Start=0;
  int Hora_Stop=0;
  int Min_Stop=0;
  
  // Horas programador
  TimeInputParam t(param);

  // Process start time
  if (t.hasStartTime()){
    Hora_Start = t.getStartHour(); 
    Min_Start = t.getStartMinute();
  }
  
  // Process stop time
  if (t.hasStopTime()) {
    Hora_Stop = t.getStopHour();
    Min_Stop = t.getStopMinute();
  }
  
  Hora_Start_sec2=(Hora_Start*3600)+(Min_Start*60);
  Hora_Stop_sec2=(Hora_Stop*3600)+(Min_Stop*60);

  // Process weekdays (1. Lunes 2. Martes 3. Miercoles 4. Jueves 5. Viernes 6.Sabado 7.Domingo)
  Prog_Dias2="";
  for (int i = 1; i <= 7; i++) {
    if (t.isWeekdaySelected(i)) {
      Prog_Dias2 = Prog_Dias2 + i;
    }
  }

  if (Min_Start<10){
    Hora_Start2 =  String(Hora_Start) + ":0" + String(Min_Start);
  }
  else{
    Hora_Start2 =  String(Hora_Start) + ":" + String(Min_Start);
  }
  if (Min_Stop<10){
    Hora_Stop2 = String(Hora_Stop) + ":0" + String(Min_Stop);
  }
  else{
    Hora_Stop2 = String(Hora_Stop) + ":" + String(Min_Stop);
  }

}

// Horario Programador app Blink
BLYNK_WRITE(V4){ 
  Programador = param.asInt();
}

//Función que controla el Programador de funcionamiento Caldera
void Control_Programador(){
  bool Evalua_PRG1 = false;  // Variable local estado Programador 1
  bool Evalua_PRG2 = false;  // Variable local estado Programador 2

  // Programador_1
  //Si el dia no está programado NO se activa la calefacción
  if (Prog_Dias1.indexOf(Dia_Actual)==-1){
      Evalua_PRG1 = false;
  }

  // Verifico que el dia este programado y la hora actual este entre la hora start y stop (en segundos) para programar la Calefacción
  //   22:00  23:45 LMXJVSD  Actual 23:30 M  Actual 6:30 X
  if ((Prog_Dias1.indexOf(Dia_Actual)>=0)&&(Prog_Dias1!="")
                &&(
                  ((Hora_Actual_sec>=Hora_Start_sec1)&&(Hora_Actual_sec<Hora_Stop_sec1)&&(Hora_Stop_sec1>Hora_Start_sec1))
                ||((Hora_Actual_sec>=Hora_Start_sec1)&&(Hora_Actual_sec>Hora_Stop_sec1)&&(Hora_Stop_sec1<Hora_Start_sec1))
                ||((Hora_Actual_sec<=Hora_Start_sec1)&&(Hora_Actual_sec<Hora_Stop_sec1)&&(Hora_Stop_sec1<Hora_Start_sec1))
                )
      ){
      Evalua_PRG1 = true;
  }
  else{
      Evalua_PRG1 = false;
  }

  // Programador_2
  //Si el dia no está programado NO se activa la calefacción
  if (Prog_Dias2.indexOf(Dia_Actual)==-1){
      Evalua_PRG2 = false;
  }

  // Verifico que el dia este programado y la hora actual este entre la hora start y stop (en segundos) para programar la Calefacción
  //   22:00  23:45 LMXJVSD  Actual 23:30 M  Actual 6:30 X
  if ((Prog_Dias2.indexOf(Dia_Actual)>=0)&&(Prog_Dias2!="")
                &&(
                  ((Hora_Actual_sec>=Hora_Start_sec2)&&(Hora_Actual_sec<Hora_Stop_sec2)&&(Hora_Stop_sec2>Hora_Start_sec2))
                ||((Hora_Actual_sec>=Hora_Start_sec2)&&(Hora_Actual_sec>Hora_Stop_sec2)&&(Hora_Stop_sec2<Hora_Start_sec2))
                ||((Hora_Actual_sec<=Hora_Start_sec2)&&(Hora_Actual_sec<Hora_Stop_sec2)&&(Hora_Stop_sec2<Hora_Start_sec2))
                )
      ){
      Evalua_PRG2 = true;
  }
  else{
      Evalua_PRG2 = false;
  }

  // Acciones sobre el Control de la caldera por Programador
  if ((Evalua_PRG1)||(Evalua_PRG2)){
      estado_calefac = 1;
      Blynk.virtualWrite(V1, estado_calefac);
      evalua = true;
  }
  else{
      estado_calefac = 0;
      Blynk.virtualWrite(V1, estado_calefac);
  }

  #ifdef ___DEBUG2___
    Serial.print("Estado Calefacción = "); Serial.println(estado_calefac);
    Serial.print("Prog_Dias= ");  Serial.println(Prog_Dias1);
    Serial.print("Dia_Actual= ");  Serial.println(Dia_Actual);
    Serial.print("Indexof Dia:Actual= "); Serial.println(Prog_Dias1.indexOf(Dia_Actual));
    Serial.print("Hora_Actual_sec= ");  Serial.println(Hora_Actual_sec);
    Serial.print("Hora_Start_sec= ");  Serial.println(Hora_Start_sec1);
    Serial.print("Hora_Stop_sec= ");  Serial.println(Hora_Stop_sec1);
  #endif 

}

// OTA desde Blynk
BLYNK_WRITE(InternalPinOTA) {
  Serial.println("OTA Started"); 
  overTheAirURL = param.asString();
  Serial.print("overTheAirURL = ");  
  Serial.println(overTheAirURL);  
  WiFiClient my_wifi_client;
  HTTPClient http;
  Blynk.disconnect();
  http.begin(my_wifi_client, overTheAirURL);
  int httpCode = http.GET();
  Serial.print("httpCode = ");  
  Serial.println(httpCode);  
  if (httpCode != HTTP_CODE_OK) {
    return;
  }
  int contentLength = http.getSize();
  Serial.print("contentLength = ");  
  Serial.println(contentLength);   
  if (contentLength <= 0) {
    return; 
  }
  bool canBegin = Update.begin(contentLength);
  Serial.print("canBegin = ");  
  Serial.println(canBegin);    
  if (!canBegin) { 
    return;
  }
  Client& client = http.getStream();
  int written = Update.writeStream(client);
  Serial.print("written = ");  
  Serial.println(written);   
  if (written != contentLength) {
    return;
  }
  if (!Update.end()) {
    return;
  }
  if (!Update.isFinished()) {
    return;
  }
  ESP.restart();
}

// Esta función se ejecutará cada vez que se establezca la conexión Blynk
BLYNK_CONNECTED() {
  // Solicitar al servidor Blynk que vuelva a enviar los últimos valores para todos los pines
  Blynk.syncAll();
}

void startBrokerMqtt();

// Conexión e Inicio variables a la app Blink
void startBlink(){
  #ifdef ___DEBUG___
    Serial.println("------------------------Iniciando Blynk ------------------------");
  #endif
  Blynk.config(blynk_token);  
  while (Blynk.connect() == false) {
  }
  rtc.begin();
  mqtt.off(); // Rele control caldera desconectado
  terminal.clear();
  terminal.println("   ---- TERMOSTATO BROKER INICIALIZADO ----");
  terminal.println("Firmware Versión: " + String(BLYNK_FIRMWARE_VERSION));
  terminal.println("IP address Termostato - BROKER: " + WiFi.localIP().toString());
  terminal.println("Para obtener ayuda del dispositivo teclee en el terminal HELP");
  terminal.flush();
  mqttReconnectTimer.once(10, startBrokerMqtt); // Me conecto el Broker MQTT
}

// Clase y Funciones Broker mqtt  ---------------------------------------------------------------------------------------------

class myMQTTBroker: public uMQTTBroker
{
public:
    virtual bool onConnect(IPAddress addr, uint16_t client_count) {
      #ifdef ___DEBUG___
        Serial.println(addr.toString()+" connected Client");
      #endif
      if (Min_Actual<10)
        terminal.println(String(Hora_Actual) + ":0" + String(Min_Actual) + "h " + addr.toString() + " conectado Cliente");
      else
        terminal.println(String(Hora_Actual) + ":" + String(Min_Actual) + "h " + addr.toString() + " conectado Cliente");
      terminal.flush();
      return true;
    }

    virtual void onDisconnect(IPAddress addr, String client_id) {
      #ifdef ___DEBUG___
        Serial.println(addr.toString()+" ("+client_id+") disconnected");
      #endif
      if (Min_Actual<10)
        terminal.println(String(Hora_Actual) + ":0" + String(Min_Actual) + "h " + addr.toString() + " (" + client_id + ") desconectado");
      else
        terminal.println(String(Hora_Actual) + ":" + String(Min_Actual) + "h " + addr.toString() + " (" + client_id + ") desconectado");
      terminal.flush();
      if (client_id == client_rele_init){
        client_rele = "";  // Rele control caldera desconectado
        mqtt.off();
        terminal.println(String(Hora_Actual) + ":" + String(Min_Actual) + "h " + "Rele caldera OFF, desconectado del Broker MQTT");
        terminal.flush();
      }

    }

    virtual bool onAuth(String username, String password, String client_id) {
      #ifdef ___DEBUG___
        Serial.println("ClientId: "+client_id);
      #endif
      if (client_id == client_rele_init){
        client_rele = client_id;  // Rele control caldera conectado
        mqtt.on();
      }
      terminal.println("ClienteId: " + client_id + " conectado Termostato-Broker");
      terminal.flush();
      return true;
    }
    
    virtual void onData(String topic, const char *data, uint32_t length) {
      char data_str[length+1];
      os_memcpy(data_str, data, length);
      data_str[length] = '\0';

      #ifdef ___DEBUG___
        Serial.println("received topic '"+topic+"' with data '"+(String)data_str+"'");
        printClients();
      #endif

      // Arranque en MANUAL Calefacción -> 1 AUTO / 0 PARADA
      if (topic == "calefaccion"){
        estado_calefac = ((String)data_str).toInt();
        Blynk.virtualWrite(V1, estado_calefac);
        #ifdef ___DEBUG___
          Serial.print("Estado Calefacción = "); 
          if (estado_calefac == 1){
            Serial.println(" AUTO ON mqtt");
            Serial.print("Setpoint = "); Serial.println(temp_setpoint);
            Serial.print("Banda Diferencial = "); Serial.println(Banda_Diferencial);
            Serial.print("Ajuste Temperatura = "); Serial.println(ajuste);
          }
          if (estado_calefac == 0){
            Serial.println(" AUTO OFF mqtt");
          }
        #endif
      }

      // Sensor de temperatura Externo
      if (topic == "sensor_temp"){
        String str_temp_medida = (String)data_str;
        temp_medida = str_temp_medida.toFloat();
        temp_medida = temp_medida + ajuste;
        Blynk.virtualWrite(V5, temp_medida);
        evalua = true;
        #ifdef ___DEBUG___
          Serial.print("Temperatura Actual = "); Serial.println(temp_medida);
        #endif
      }
      // Sensor de humedad Externo
      if (topic == "sensor_hum"){
        String str_hum_medida = (String)data_str;
        hum_medida = str_hum_medida.toFloat();
        Blynk.virtualWrite(V6, hum_medida);
        #ifdef ___DEBUG___
          Serial.print("Humedad Actual = "); Serial.println(hum_medida);
        #endif
      }

      if (topic == "config"){
        String config = (String)data_str;
        // Ajuste de temperatura
        if ((config.substring(0, 1) == "A")){
          String str_ajuste = config.substring(1, config.length());
          float m_ajuste = str_ajuste.toFloat();
          if (ajuste > m_ajuste)
            temp_medida = temp_medida - 0.5;
          if (ajuste < m_ajuste)
            temp_medida = temp_medida + 0.5;
          if (ajuste == 0)
            temp_medida = temp_medida + ajuste;
          ajuste = m_ajuste;
          Blynk.virtualWrite(V7, ajuste);
          Blynk.virtualWrite(V5, temp_medida);
          evalua = true;
          #ifdef ___DEBUG___
            Serial.print("Ajuste = "); Serial.println(ajuste);
          #endif
        }
        // Diferencial de funcionamiento 
        if ((config.substring(0, 1) == "B")){
          Banda_Diferencial = (config.substring(1, config.length())).toInt();
          Blynk.virtualWrite(V3, Banda_Diferencial);
          evalua = true;
          #ifdef ___DEBUG___
            Serial.print("Banda_Diferencial = "); Serial.println(Banda_Diferencial);
          #endif
        }
        // Setpoint de Temperatura
        if ((config.substring(0, 1) == "S")){
          temp_setpoint = (config.substring(1, config.length())).toInt();
          Blynk.virtualWrite(V8, temp_setpoint);
          evalua = true;
          #ifdef ___DEBUG___
            Serial.print("Temperatura Setpoint = "); Serial.println(temp_setpoint);
          #endif
        }
      }
      // Estado del Rele (Respuesta Shelly_1)
      if (topic == EstadoReleControl){
        if(((String)data_str) == "ON") {
          led.on();
          if (Min_Actual<10)
            terminal.println(String(Hora_Actual) + ":0" + String(Min_Actual) + "h " + "Rele caldera ON");
          else
          terminal.println(String(Hora_Actual) + ":" + String(Min_Actual) + "h " + "Rele caldera ON");
          terminal.flush();
          #ifdef ___DEBUG___
            Serial.print("mem_rele_calefac = "); Serial.println(mem_rele_calefac);
            Serial.print("rele_calefac = "); Serial.println(rele_calefac);
          #endif
          if (rele_calefac == 0){   // watchdog Shelly Timer
            rele_calefac =1;
            mem_rele_calefac = 0;
          }
        }
        if(((String)data_str) == "OFF"){
          led.off();
          if (Min_Actual<10)
            terminal.println(String(Hora_Actual) + ":0" + String(Min_Actual) + "h " + "Rele caldera OFF");
          else
          terminal.println(String(Hora_Actual) + ":" + String(Min_Actual) + "h " + "Rele caldera OFF");
          terminal.flush();
          #ifdef ___DEBUG___
            Serial.print("mem_rele_calefac = "); Serial.println(mem_rele_calefac);
            Serial.print("rele_calefac = "); Serial.println(rele_calefac);
          #endif
          if (rele_calefac == 1){   // watchdog Shelly Timer
            rele_calefac = 0;
            mem_rele_calefac = 0;
          }
          
        }
        evalua = true;
      }
      // Estado Dispositivo Primera conexion (Respuesta Shelly_1)
      if (topic == Dispositivo){
        if(((String)data_str) == "Online"){
          terminal.println("ClienteId: caldera Online Termostato-Broker");
          terminal.flush();
          evalua = true;
        }
        if(((String)data_str) == "Offline"){
          terminal.println("ClienteId: caldera Offline Termostato-Broker");
          terminal.flush();
          evalua = true;
        }
      }
      // Estado Dispositivo (Informe periodico Shelly_1)
      if (topic == EstadoDispositivo){
        mqtt.on();  // Se envia cada 5 min, en Blynk se revisa cada 7 min
        if (latido){  // Se muestra por el terminal
          terminal.println((String)data_str);
          terminal.flush();
        }
      }
      // Watchdog -> Estado del mqtt Broker
      if (topic == Watchdog)
        watchdog_mqtt = ((String)data_str).toInt();
    }

    // Sample for the usage of the client info methods
    #ifdef ___DEBUG___
      virtual void printClients() {
        for (int i = 0; i < getClientCount(); i++) {
          IPAddress addr;
          String client_id;
          
          getClientAddr(i, addr);
          getClientId(i, client_id);
          Serial.println("Client "+client_id+" on addr: "+addr.toString());
        }
      }
    #endif
};

// Objeto Broker mqtt
myMQTTBroker myBroker;

// Inicio Broker y me conecto a la app Blink
void startBrokerMqtt(){
  #ifdef ___DEBUG___
    Serial.println("------------------------Iniciando Broker MQTT ------------------------");
  #endif
  MQTT_server_start(1883, 10, 5);
  myBroker.init();

  // Topic Subscritos 
  // Rele de control del termostato
  myBroker.subscribe(ReleControl);
  // Estado Rele de control del termostato
  myBroker.subscribe(EstadoReleControl);
  // Estado del Dispositivo de control del termostato Priemra Conexión
  myBroker.subscribe(Dispositivo);
  // Estado del Dispositivo de control del termostato
  myBroker.subscribe(EstadoDispositivo);
  // Estado del MQTT Broker
  myBroker.subscribe(Watchdog);
  // Arranque / Para Calefacción: 1 -> Calefacción AUTO / 0 -> calefacción APAGADA
  myBroker.subscribe("calefaccion");   
  // topic donde mandaría los valores de temperatura un sensor externo al SHT30 usado en este dispositivo   
  myBroker.subscribe("sensor_temp");
  // topic donde mandaría los valores de humedad un sensor externo al SHT30 usado en este dispositivo   
  myBroker.subscribe("sensor_hum");
  // Configuración Termostato: 
  // Ajuste -> es el valor de ajuste de la temperatura medida por el sensor de control (defecto = -3ºC) / ejemplo -> A+2 ó A-3
  // Banda Diferencial -> es el margen para que pare la calefacción despues de pasar el setpoint (defecto = 1ºC) / ejemplo -> B+2 ó B-3
  // Setpoint -> es el valor de temperatura de paro de la calefacción (defecto = 21ºC) / ejemplo -> S22 ó S18
  myBroker.subscribe("config");   
  watchdog_mqtt = false; // Activo el perro guardian del MQTT 

}

// Función Sensor Temperatura  -------------------------------------------------------------------------------------------------

#ifdef sensor_sht30
  void dataSensor(){
    if(sht30.get()==0){
      temp_medida = sht30.cTemp + ajuste;
      hum_medida = sht30.humidity - 5;
      Blynk.virtualWrite(V5, temp_medida);
      Blynk.virtualWrite(V6, hum_medida);
      #ifdef ___DEBUG___
        Serial.println("_______________________________");
        Serial.print("Temperatura = ");
        Serial.println(temp_medida);
        Serial.print("Humedad = ");
        Serial.println(hum_medida);
        Serial.print("Temperatura sin ajuste = ");
        Serial.println(temp_medida - ajuste);
        Serial.print("Humedad sin ajuste = ");
        Serial.println(hum_medida + 5);
      #endif
      evalua = true;
    }
    else
    {
      terminal.println("Error sensor SHT30");
      terminal.flush();
      evalua = false;
      myBroker.publish(ReleControl, "0", 0, 1);
      digitalWrite(PIN_LED, HIGH);
      #ifdef ___DEBUG___
        Serial.println("Error! sensor SHT30");
      #endif
    }

  }
#endif

// Fallo sensor de temperatura
void Fallo_Sensor(){

  myBroker.publish(ReleControl, "0", 0, 1); // Paro calefacción
  digitalWrite(PIN_LED, HIGH);
  delay(300);
  terminal.println("Fallo sensor de temperatura, ejecuta RESET");
  terminal.println("Si es problema persiste contacta con Servicio Técnico");
  terminal.flush();

}

// Verifico que unos de los clientes conectados al Broker MQTT sea el relé caldera
// y que el Broker este funcionando
void verifico_mqtt (){
  #ifdef ___DEBUG___
    Serial.println("----------------------------  Watchdog BROKER MQTT -----");
  #endif

  myBroker.publish(Watchdog, "1"); // mqtt OK

  //Verifico relé shelly conectado al Broker mqtt
  if (client_rele != client_rele_init)
    ESP.restart();

  //Verifico funcionamiento correcto del Broker mqtt
  if (watchdog_mqtt){
    watchdog_mqtt = false;
    if (Min_Actual<10)
      terminal.println(String(Hora_Actual) + ":0" + String(Min_Actual) + "h " + "watchdog Termostato-Broker -->> OK");
    else
      terminal.println(String(Hora_Actual) + ":" + String(Min_Actual) + "h " + "watchdog Termostato-Broker -->> OK");
    terminal.flush();
  }
  else
    ESP.restart();
}

// Funciones WiFi  -------------------------------------------------------------------------------------------------

//callback de notificación de configuración guardada
void saveConfigCallback () {
  #ifdef ___DEBUG___
    Serial.println("Se debe guardar configuración");
  #endif
  shouldSaveConfig = true;
}

// Conexión a WIFI y WifiManager
void startWiFiClient(){

  //Lectura configuración desde config.json
  if (LittleFS.begin()) {
    #ifdef ___DEBUG___
      Serial.println("Montando sistema de archivos LittleFS...");
    #endif
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      #ifdef ___DEBUG___
        Serial.println("reading config file");
      #endif
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        #ifdef ___DEBUG___
          Serial.println("opened config file");
        #endif
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        #ifdef ___DEBUG___
          Serial.print("JsonObject ->");
        #endif
        json.printTo(Serial);
      
        if (json.success()) {
          #ifdef ___DEBUG___
            Serial.println("\nparsed json");
          #endif
          // Parametros del proyecto a guardar en el arranque
          strcpy(blynk_token, json["blynk_token"]);

        } else {
          #ifdef ___DEBUG___
            Serial.println("failed to load json config");
          #endif
        }
      }
    }
  } else {
    #ifdef ___DEBUG___
      Serial.println("failed to mount  LittleFS.");
    #endif
  }
  //end read
  AsyncWiFiManagerParameter custom_blynk_token("blynk1", "blynk token", blynk_token, 34);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  AsyncWiFiManager wifiManager(&server,&dns);

  //Se notifica que es necesario grabar la configuración -> notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //Portal cautivo
   wifiManager.setAPStaticIPConfig (IPAddress ( 192 , 168 , 4 , 1 ), IPAddress ( 192 , 168 , 1 , 1 ), IPAddress ( 255 , 255 , 255 , 0 ));

  //Parametros para almacenar
  wifiManager.addParameter(&custom_blynk_token);

  //IP estatica a designar, por defecto 192.168.1.240 dentro de la Red Local donde se conecte el monitor
  wifiManager.setSTAStaticIPConfig(ip, gateway, subnet, dns1);

  //programar conexion
  if (!wifiManager.autoConnect("Termostato_Broker_AP")) {
    #ifdef ___DEBUG___
      Serial.println("failed to connect, we should reset as see if it connects");
    #endif
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  //read updated parameters
  strcpy(blynk_token, custom_blynk_token.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    #ifdef ___DEBUG___
      Serial.println("saving config");
    #endif
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    json["blynk_token"] = blynk_token;

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      #ifdef ___DEBUG___
        Serial.println("failed to open config file for writing");
      #endif
    }
    #ifdef ___DEBUG___
      Serial.println("json -> ");
    #endif
    json.printTo(Serial);
    json.printTo(configFile); 
    configFile.close();
    //end save
  }

  #ifdef ___DEBUG___
    Serial.println("");
    Serial.println("");
    Serial.println("WiFi connectado");
    Serial.println("IP address Termostato-BROKER: " + WiFi.localIP().toString());
  #endif
}

void startWiFiClient_fabrica(){

  //Borro el Token de Blynk
  strcpy(blynk_token,"BLYNK_TOKEN");

  #ifdef ___DEBUG___
    Serial.println("saving config Fabrica");
  #endif
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  json["blynk_token"] = blynk_token;

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    #ifdef ___DEBUG___
      Serial.println("failed to open config file for writing");
    #endif
    }
  #ifdef ___DEBUG___
    Serial.println("json -> ");
  #endif
  json.printTo(Serial);
  json.printTo(configFile); 
  configFile.close();
  
  AsyncWiFiManagerParameter custom_blynk_token("blynk1", "blynk token", blynk_token, 34);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  AsyncWiFiManager wifiManager(&server,&dns);
  //reset settings - Borra las SSID almacenadas en la Flash (falla en ocasiones)
  wifiManager.resetSettings();
  delay(3000);
  ESP.reset();
  delay(5000);

}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  #ifdef ___DEBUG___
    Serial.println("Connected to Wi-Fi.");
  #endif
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  #ifdef ___DEBUG___
    Serial.println("Disconnected from Wi-Fi.");
  #endif
  if (fabrica)
    wifiReconnectTimer.once(2, startWiFiClient_fabrica);
  if (!(fabrica))
    wifiReconnectTimer.once(2, startWiFiClient);
}

// Funciones de Control Pulsador  --------------------------------------------------------------------------------------------------

void ReleasedCallbackFunction(void* s) {
  #ifdef ___DEBUG___
    Serial.println("");
    Serial.print("Pulsación: "); Serial.println((char*)s);
  #endif
  if (client_rele == "caldera"){
    Programador = 0;
    Blynk.virtualWrite(V4, Programador);
    estado_calefac = !(estado_calefac);
    digitalWrite(PIN_LED, estado_calefac);
    Blynk.virtualWrite(V1, estado_calefac);
    evalua = true;
    #ifdef ___DEBUG___ 
      if(estado_calefac){
        Serial.println(" AUTO ON pulsador");
        Serial.print("Setpoint = "); Serial.println(temp_setpoint);
        Serial.print("Banda Diferencial = "); Serial.println(Banda_Diferencial);
        Serial.print("Ajuste Temperatura = "); Serial.println(ajuste);
      }
      else{
        digitalWrite(PIN_LED, HIGH);
        Serial.print("Califaccion = "); Serial.println("AUTO OFF");
      }
    #endif
  }
  else{
    #ifdef ___DEBUG___
      Serial.print(client_rele + " No esta conectado al Broker mqtt");
    #endif
    terminal.println(client_rele_init + " No esta conectado a Termostato-Broker");
    terminal.flush();
  }
}

void LongPressCallbackFunction(void* s) {
  #ifdef ___DEBUG___
    Serial.println("");
    Serial.print("Pulsación Larga: ");
    Serial.println((char*)s);
  #endif
    myBroker.publish(ReleControl, "0", 0, 1);
    digitalWrite(PIN_LED, HIGH);
    delay(300);
    ESP.restart();
  }

//-----------------------------------------------------------------------------------------------------------------------------------

void setup()
{
  #ifdef ___DEBUG___
    Serial.begin(115200);
    Serial.println();
    Serial.println();
  #endif

  //Led de control
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  // Inicializo objetos pulsador
  PulsadorSwitch.setLongPressCallback(&LongPressCallbackFunction, (void*)"long press");
  PulsadorSwitch.setReleasedCallback(&ReleasedCallbackFunction, (void*)"released");
  
  // Inicio I2C
  #ifdef sensor_sht30
    Wire.begin(PIN_SDA, PIN_SCL);
  #endif

  // Inicializo gestor de eventos WIFI a la conexión
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);

  // Conexión WiFi con WIFIManager
  startWiFiClient();
  #ifdef ___DEBUG___
    Serial.println("IP address Termostato - BROKER: " + WiFi.localIP().toString());
  #endif

  // Inicializo gestor de eventos WIFI a la desconexión
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  
  // Cada time_data se ejecuta dataSensor y se recogen valores de temperatura y humedad
  #ifdef sensor_sht30
    ticker_Datos.attach_scheduled(time_data, dataSensor);
    dataSensor(); // Inicializo sensor
  #endif

  // Cada 10 segundos actualizo la hora actual
  ticker_Ahora.attach_scheduled(10, Ahora);
  // Cada 5 minutos verifico que el relé caldera esta conectado al Broker MQTT 
  // y que el Broker funcione, si no se cumple reinicio TermostatoBroker
  watchdogBroker.attach_scheduled(300, verifico_mqtt);

  #ifdef ___DEBUG___
    Serial.println("INICIALIZADO Termostato-Broker");
  #endif

  // Inicializo OTA
  ArduinoOTA.setHostname("Termo-Broker"); // Hostname OTA 
  ArduinoOTA.begin();

}

//--------------------------------------------------------------------------------------------------------------------------------

void loop(){

  // Control pulsador
  PulsadorSwitch.poll();
  // Actualización código por OTA-LOCAL
  if (ota){
    ArduinoOTA.handle();  
  }
  else {  // Resto del código del dispositivo (Termostato-Broker)
    // Me conecto a la app Blink si no estoy conectado y si no estoy actualizando por OTA
    if ((Blynk.connected()) && (!(ota)))
      Blynk.run(); // Actualización app Blynk
    if ((!(Blynk.connected())) && (!(ota)&& (!(fabrica)))){  
      startBlink();  
      // Asisgno el estado del relé al requerido por la app tras conexión
      if ((client_rele == client_rele_init) && (estado_calefac == 0)){
        myBroker.publish(ReleControl, "0", 0, 1);
        digitalWrite(PIN_LED, HIGH);
      }
      if ((client_rele == client_rele_init) && (estado_calefac == 1)){
       evalua = true; 
       myBroker.publish("cmnd/shelly/POWER", ""); // Estado del rele
      }
    }

    // ORDENES DESDE EL TERMINAL DE LA APP
    if (ordenes == "HELP"){              // Ayuda Ordes 
      ordenes = "";
      terminal.println("");
      terminal.println("ORDENES:");
      terminal.println("RESET -> Reinicio Termostato-Broker");
      terminal.println("RESET_ALL -> Termostato-Broker configuración de fábrica");
      terminal.println("UPDATE_OTA -> Permisivo para la actualización OTA-LOCAL");
      terminal.println("RESET_RELE -> Reinicio relé Shelly 1");
      terminal.println("ESTADO_RELE -> Muestra estado actual relé");
      terminal.println("ESTADO_MQTT -> Muestra estado actual del Broker Mqtt");
      terminal.println("PROGRAMADOR -> Muestra las horas de los Programadores Horarios");
      terminal.println("COMUNICACION_RELE -> Muestra/Oculta la comunicación periódica con el relé");
      terminal.println("AJUSTE -> Muestra el ajuste de temperatura configurado");
      terminal.println("AJUSTE= -> Configura ajuste de temperatura ej: AJUSTE=-2.5");
      terminal.println("BANDA -> Muestra la banda de funcionamiento, configurado");
      terminal.println("BANDA= -> Configura banda de funcionamiento, ej: BANDA=1");
      terminal.flush();
    }
    if (ordenes == "RESET"){              // RESET Termostato_Broker
      myBroker.publish(ReleControl, "0", 0, 1);
      digitalWrite(PIN_LED, HIGH);
      delay(300);
      ESP.restart();
    }
    // ORDENES DESDE EL TERMINAL DE LA APP
    if (ordenes == "RESET_ALL"){          // RESET Termostato_Broker
      terminal.println("");
      terminal.println("Conectate a la red WIFI Termostato_Broker_AP");
      terminal.println("Pon esta dirección en tu navegador http://192.168.4.1");
      terminal.println("Configura tu red WIFI y Token Blink");
      terminal.flush();
      delay(300);
      ordenes = "";
      myBroker.publish(ReleControl, "0", 0, 1);
      digitalWrite(PIN_LED, HIGH);
      fabrica = true;
      Blynk.disconnect(); // Me desconecto de Blynk
      WiFi.disconnect(true); // Me desconecto del WIFI
    }
    if (ordenes == "UPDATE_OTA"){         // Permisivo Actualización por OTA
      myBroker.publish(ReleControl, "0", 0, 1);
      digitalWrite(PIN_LED, HIGH);
      terminal.println(" Esperando actualización OTA-LOCAL");
      terminal.flush();
      delay(300);
      ota = true;
      Blynk.disconnect();
    }
    if (ordenes == "RESET_RELE"){         // RESET Relé
      ordenes = "";
      myBroker.publish("cmnd/shelly/Restart", "1");
      terminal.println("Rele Caldera Reiniciado");
      terminal.flush();
    }

    if (ordenes == "BANDA"){         // Banda de Funcionamiento
      ordenes = "";
      terminal.println(" Banda Diferencial de Funcionamiento Caldera = " + String(Banda_Diferencial));
      terminal.flush();
    }
    if (ordenes.substring(0, 6) == "BANDA="){  //  Configuración Banda de  Funcionamiento
      Banda_Diferencial = ordenes.substring(6, ordenes.length()).toInt();
      ordenes = "";
      evalua = true;
    }

    if (ordenes == "AJUSTE"){         // Ajuste Temperatura
      ordenes = "";
      terminal.println(" Ajuste Temperatura = " + String(ajuste));
      terminal.flush();
    }
    if (ordenes.substring(0, 7) == "AJUSTE="){  //  Configuración  Ajuste Temperatura
      ajuste = ordenes.substring(7, ordenes.length()).toFloat();
      ordenes = "";
      terminal.println(" Ajuste Temperatura = " + String(ajuste));
      terminal.flush();
      dataSensor();
      evalua = true;
    }
    
    if (ordenes == "ESTADO_RELE"){         // Estado Relé
      ordenes = "";
      myBroker.publish("cmnd/shelly/POWER", "");
    }

    if (ordenes == "ESTADO_MQTT"){         // Estado MQTT
      ordenes = "";
      verifico_mqtt ();
    }

    if (ordenes == "PROGRAMADOR"){         // Estado PROGRAMADORES
      ordenes = "";
      terminal.println("PROGRAMADOR 1");
      terminal.println("Inicio: " + Hora_Start1 + "h - Fin: " + Hora_Stop1 + "h");
      terminal.println("1. Lunes 2. Martes 3. Miercoles 4. Jueves 5. Viernes 6.Sabado 7.Domingo");   
      terminal.println("PROGRAMADOR 1 Dias Semana: " + Prog_Dias1);
      terminal.println("PROGRAMADOR 2");
      terminal.println("Inicio: " + Hora_Start2 + "h - Fin: " + Hora_Stop2 + "h");
      terminal.println("1. Lunes 2. Martes 3. Miercoles 4. Jueves 5. Viernes 6.Sabado 7.Domingo");
      terminal.println("PROGRAMADOR 2 Dias Semana: " + Prog_Dias2);
      terminal.flush();
    }
    if (ordenes == "COMUNICACION_RELE"){   // Comunicación periódica Relé
      ordenes = "";
      latido = !latido;
      if (latido)
        terminal.println("Activada comunicación periódica Relé");
      if (!(latido))
        terminal.println("Desactivada comunicación periódica Relé");
      terminal.flush();
    }

    // watchdog SENSOR TEMPERATURA (5 min se mantiene el error )
    if(temp_medida < -10)
      watchdogSensor.attach_scheduled(600, Fallo_Sensor);
    else
      watchdogSensor.detach();

    // Control del Programador horario de la cáldera
    if (Programador)
      Control_Programador();

    // CONTROL DEL TERMOSTATO
    if ((client_rele == client_rele_init) && (estado_calefac) && 
      ((evalua) || ((mem_rele_calefac == 0) && (rele_calefac == 0)))){   // TERMOSTATO AUTO
      digitalWrite(PIN_LED, LOW);

      if (temp_medida < temp_setpoint - Banda_Diferencial){
        inicio = false;
        if ((rele_calefac == 0) || ((mem_rele_calefac == 0) && (rele_calefac == 0))) {   // CALEFACCION ON
          #ifdef ___DEBUG___
            Serial.print("Califaccion = "); Serial.println("ON Control");
          #endif  
          mem_rele_calefac = 0;
          rele_calefac = 1;
          myBroker.publish(ReleControl, (String)rele_calefac, 0, 1);
        }
      }
      
      if (((temp_medida > temp_setpoint - Banda_Diferencial) &&   // BANDA Diferencial CALEFACCION ON SOLO AL INICIAR
          (temp_medida < temp_setpoint) && (inicio))) {  
        #ifdef ___DEBUG___
          Serial.print("Califaccion = "); Serial.println("ON Control Banda Diferencial");
        #endif  
        mem_rele_calefac = 0;
        rele_calefac = 1;
        inicio = false;
        myBroker.publish(ReleControl, (String)rele_calefac, 0, 1); 
      }

      if (temp_medida >= temp_setpoint){
        inicio = false;
        if ((rele_calefac == 1) || ((mem_rele_calefac == 0) && (rele_calefac == 0))) {   // CALEFACCION OFF
          #ifdef ___DEBUG___
            Serial.print("Califaccion = "); Serial.println("OFF Control");
          #endif
          mem_rele_calefac = 1;
          rele_calefac = 0;
          myBroker.publish(ReleControl, (String)rele_calefac);
        }
      }
      
      evalua = false;
    } 

    if (!(estado_calefac)){                             // TERMOSTATO APAGADO
      digitalWrite(PIN_LED, HIGH);
      if((client_rele == client_rele_init) && ((rele_calefac == 1) ||     
        ((mem_rele_calefac == 0) && (rele_calefac == 0)))){  // inicio
        myBroker.publish(ReleControl, "0", 0, 1);
        mem_rele_calefac = 1;
        rele_calefac = 0;
        #ifdef ___DEBUG___
          Serial.print("Califaccion = "); Serial.println("APAGADA Control");
        #endif
      }
      inicio = true;
    }
  }

}