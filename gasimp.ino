#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ArduinoJson.h>

#ifndef APSSID
#define APSSID "gasimp"
#define APPSK  "gasimp123"
#endif


//activated            1 byte        direccion de inicio 0
//ssid                 4 bytes       direccion de inicio 1
//password             4 bytes       direccion de inicio 5
//hostname             4 bytes       direccion de inicio 9

//direccion logging   4 bytes       direccion de inicio 13
//total              17 bytes total

#define EEPROM_ADDRESS_ACTIVATED     0
#define EEPROM_ADDRESS_SSID          1
//Direccion de la eeprom en donde se guarda la direccion en la que guardo el password del wifi del usuario
#define EEPROM_ADDRESS_SSID_PASSWORD 5
//Direccion de la eeprom donde se guarda la direccion en la que se guardo el hostname
#define EEPROM_ADDRESS_HOSTNAME      9
//Direccion de la eeprom donde se guarda la direccion en la que se guarda el valor de la varuiable
// consumtionPerDay
#define EEPROM_ADDRESS_CONSUMPTION_PER_DAY 13

#define EEPROM_ADDRESS_SENSING_COUNTER_PER_DAY 21

//Direccion de la eeprom donde se guarda la direccion en la que se quedo el apuntador
// del logging de los datos historios
#define EEPROM_ADDRESS_LOGGING_EEPROM_ADDRESS 13

//Indica la direccion en donde se va a empezar a escribir los datos de configuracion del dispositivo,
//por ejemplo a partir de esta direccion se empezara a escribir el ssid del usuario. De 
//acuerdo a la longitud de la cadena del ssid, la siguiente direccion disponible para guardar
// el password se guarda en la variable currentEepromAddres. Se tiene contemplado un total
// de 80 bytes para los datos de configuracion, 17 bytes para los punteros de memoria de los datos
// y 63 bytes para escribir los datos
#define EEPROM_CONFIG_ADDRESS_OFFSET 17
#define EEPROM_LOGGING_ADDRESS_OFFSET 80

// 80 bytes are for configuration data (pointers and data), from 0 to 79
//2304 bytes are for data logging, from 64 to 2367 
#define EEPROM_SIZE 2384
#define SUCESS_CODE "0"
#define SUCESS_ACTIVATED_CODE "1"
#define ERROR_CONNECTING_TO_WIFI_CODE "2"
#define SUCESS_ACTIVATING_CODE "3"

//Indica la tolerancia de la diferencia cuando se comparan dos medidas de peso. Esto ayuda a determinar si ha habido un cambio
//entre dos mediciones.
#define DIFFERENCE_TOLERANCE_BETWEEN_MEASURES 0.02
//Indica el tiempo de actualizacion entre cada medicion en milisegundos
#define UPDATE_INTERVAL_MS 1000*60*5 //
#define UPDATE_INTERVAL_MIN (UPDATE_INTERVAL_MS)/(1000*60) //

/* Set these to your desired credentials. */
const char *ssid = APSSID;
const char *password = APPSK;

//Indica la direccion actual de la eeprom donde se estaran guardando los datos de configuracion
int currentEepromAddres = EEPROM_CONFIG_ADDRESS_OFFSET; 
//Indica la direccion actual de la eeprom donde se estaran guardando los datos historicos
int loggingEepromAddres = EEPROM_LOGGING_ADDRESS_OFFSET;


// Indica si el dispositivo esta activado o no
bool activated = false;

// Indica el nombre del host
String currentHostName = "";
// Indica el tiempo de desplazamiento en segundos con respecto a la zona UTC. El valor es establecido cuando el
// la app verifica la conexion
int timezoneoffsetInSec;

ESP8266WebServer server(80);
ESP8266WiFiMulti WiFiMulti;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

struct MeasureRecord {
  float weight; //peso
  unsigned long dateTime; //fecha 
  byte day; //Dia del mes
};

MeasureRecord currentMeasureRecord;
MeasureRecord lastMeasureRecord;
// Indica el tiempo promedio en minutos que se ha consumido el gas
//La manera de saber si ha consumido el gas durante cierto tiempo, es comparando
// si la medicion pasada y la actual, si es diferente entonces se suma el intervalo de tiempo de actualizacion
// que esta en minutos
int averageTimeONPerDayMin;

float averageConsumptionPerDay = 0.0f;
// Contador de cuantas veces se ha sensado durante el dia
float sensingCounterPerDay = 0.0f;
float consumptionPerDay = 0.0f;

int saveMeasure(int address, MeasureRecord measureRecord)
{
     EEPROM.put(address, measureRecord);
     EEPROM.commit();
     MeasureRecord measureRecordCopy;
     EEPROM.get(address, measureRecordCopy);
     Serial.println("Verificando");
     Serial.println(measureRecordCopy.dateTime);
     Serial.println(measureRecordCopy.day);
     return address + sizeof(MeasureRecord) + 1;
}


byte writeStringToEEPROM(int addrOffset, const String &strToWrite)
{
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }

  return addrOffset + len + 1;
}


String readStringFromEEPROM(int addrOffset)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0'; // !!! NOTE !!! Remove the space between the slash "/" and "0" (I've added a space because otherwise there is a display bug)
  return String(data);
}

void handleWeight()
{
    DynamicJsonDocument doc(32);
    doc["data"] =  String(currentMeasureRecord.weight,2);
    doc["code"] = SUCESS_CODE;
    String buf;
    serializeJson(doc, buf);
    server.send(200, F("application/json"), buf);
}

void handleCheckConnection() 
{
  if (server.arg("timeStamp") != "")
  {    
    // Este el nombre que se usara para registrarse en el servicio de MDNS
    currentHostName = "esp" + server.arg("timeStamp");
    String timezoneOffsetParam = server.arg("timezoneoffset");
    timezoneoffsetInSec = timezoneOffsetParam.toInt();
    // Se multiplica por menos 1 porque el cliente lo envia de esta manera, si es positivo rl time zone esta antes de la
    //hora utc, si es negativo esta despues de la hora utc. Asi lo maneja la funcion getTimezoneOffset() que es la 
    // se usa en la app cliente
    timeClient.setTimeOffset(-1 * timezoneoffsetInSec);
    DynamicJsonDocument doc(128);
    doc["data"] =  currentHostName;
    doc["code"] = SUCESS_CODE;
    String buf;
    serializeJson(doc, buf);
    server.send(200, F("application/json"), buf);
  }
  else
  {
     server.send(400, "text/plain", "400: Invalid Request");
  }
}

void handleRoot() 
{
    DynamicJsonDocument doc(16);
    doc["data"] = "OK";
    doc["code"] = SUCESS_CODE;
    String buf;
    serializeJson(doc, buf);
    server.send(200, F("application/json"), buf);
}

void handleConfirming()
{
  
  DynamicJsonDocument doc(64);
  doc["data"] = "OK";
  doc["error"] = "";
  doc["resultCode"] = SUCESS_ACTIVATED_CODE;
  String buf;
  serializeJson(doc, buf);
  server.send(200, F("application/json"), buf);
  activated = true;
  EEPROM.put(EEPROM_ADDRESS_ACTIVATED, activated);
  Serial.println("Activated");
}

void handleActivate() {
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  // StaticJsonDocument<512> doc;
  // You can use DynamicJsonDocument as well
  DynamicJsonDocument doc(512);
  if(!server.hasArg("ssid") || ! server.hasArg("ssidpassword") || server.arg("ssid") == NULL || server.arg("ssidpassword") == NULL)
  { 

    server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }
  else
  {
     String ssid          = server.arg("ssid");
     String ssidPassword = server.arg("ssidpassword");
     Serial.println(ssid);
     Serial.println(ssidPassword);
     doc["data"] = "OK";
     doc["error"] = "";
     doc["resultCode"] = SUCESS_ACTIVATING_CODE;
     String buf;
     serializeJson(doc, buf);
     server.send(200, F("application/json"), buf);
     delay(1000);
     if(testWifiConnection(ssid, ssidPassword) == true)
     {
        if(setMDNS(currentHostName) == true) {

           Serial.println(currentEepromAddres);
           EEPROM.put(EEPROM_ADDRESS_SSID, currentEepromAddres); //Aqui se guarda (en la direccion 1) la direccion en donde se guardo el ssid
           currentEepromAddres = writeStringToEEPROM(currentEepromAddres, ssid) + 1;// La funcion writeStringToEEPROM devuelve la direccion
                                                                              // en donde termina de escribir la cadena
           Serial.println(currentEepromAddres);
           EEPROM.put(EEPROM_ADDRESS_SSID_PASSWORD, currentEepromAddres); //Aqui se guarda (en la direccion 5) la direccion en donde se guardo el password
           currentEepromAddres = writeStringToEEPROM(currentEepromAddres, ssidPassword) + 1;
            
            // Se guarda la direccion en donde se guarda el hostname
            EEPROM.put(EEPROM_ADDRESS_HOSTNAME, currentEepromAddres);
            //Ultima asignacion de currentEepromAddres
            currentEepromAddres = writeStringToEEPROM(currentEepromAddres, currentHostName) + 1;

            EEPROM.put(EEPROM_ADDRESS_LOGGING_EEPROM_ADDRESS, loggingEepromAddres);
            
            EEPROM.commit();
          }
     }
     else
     {
        //Se vuelve a configurar en modo access point
        setAccessPointMode();  
     }
  }
}



bool setMDNS(String currentHostNameLocal) {
 
 //Start the mDNS responder for currentHostNameLocal
 if (MDNS.begin(currentHostNameLocal) == true) {
      MDNS.addService("http", "tcp", 80);
      return true;
    } 

    return false;
}

bool testWifiConnection(String ssid, String password) 
{

  byte intents = 0;
  char ssidChar[ssid.length() + 1];
  char passwordChar[password.length() + 1];
  ssid.toCharArray(ssidChar, ssid.length() + 1);
  password.toCharArray(passwordChar, password.length() + 1);
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssidChar, passwordChar);
  Serial.println();
  Serial.print("Testing WiFi... ");
  while (WiFiMulti.run() != WL_CONNECTED && intents < 1) {
    Serial.print(".");
    delay(5);
    intents++;
  }

  if(intents >= 1)
  {
      return false;
  }

  return true;
  
}

void setServer()
{
    server.on("/", handleRoot); //Manejador del root
    server.on("/checkconnection", handleCheckConnection); //Manejador de check connection
    server.on("/activate", HTTP_POST, handleActivate); //Manejador de la ruta activate
    server.on("/confirming", handleConfirming); //Manejador de la ruta confirming
    server.on("/weight", handleWeight); //Manejador de get weight
    server.begin(); // inicia el servidor
    Serial.println("HTTP server started");
}

void setAccessPointMode() 
{
    Serial.print("Device is not activated");
    Serial.println();
    Serial.print("Configuring access point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
}

void setClientMode()
{
  byte userSsIdEepromAddres;
  byte userSsIdPasswordEepromAddres;
  EEPROM.get(EEPROM_ADDRESS_SSID, userSsIdEepromAddres);
  EEPROM.get(EEPROM_ADDRESS_SSID_PASSWORD, userSsIdPasswordEepromAddres);
  //Obtiene el ssid del usuario
  String ssid     = readStringFromEEPROM(userSsIdEepromAddres);
  // Obtiene el password del ssid del usuario
  String password = readStringFromEEPROM(userSsIdPasswordEepromAddres);
  
  Serial.println(ssid);
  Serial.println(password);
  char ssidChar[ssid.length() + 1];
  char passwordChar[password.length() + 1];
  
  ssid.toCharArray(ssidChar, ssid.length() + 1);
  password.toCharArray(passwordChar, password.length() + 1);

  Serial.println(ssidChar);
  Serial.println(passwordChar);
  
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssidChar, passwordChar);
  Serial.println();
  Serial.print("Wait for WiFi... ");

  while (WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

      Serial.println("");
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      int hostNameAddress;
      EEPROM.get(EEPROM_ADDRESS_HOSTNAME, hostNameAddress);
      //Obtiene el hostName
      currentHostName    = readStringFromEEPROM(hostNameAddress);
      Serial.println("hostname");
      Serial.println(currentHostName);
      setMDNS(currentHostName);
    
  delay(500);
  
}

float sensing()
{
   return 12.2;
}

void setup() {
  
  delay(2000);
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  Serial.print("Initialiazing");
  EEPROM.get(EEPROM_ADDRESS_ACTIVATED, activated);

  
  Serial.print("Verifiying if device is activated");
  Serial.println();
  Serial.print(activated);
  Serial.println();
  if(activated)
  { 
    Serial.print("Device is activated");
    setClientMode(); //Inicia el modo cliente
    setServer(); //Inicia el servidor
    timeClient.begin();
  }
  else
  {
      setAccessPointMode();
      setServer();
      while(!activated)
      {
          server.handleClient();
          MDNS.update();
          delay(100);
      }
  }
  
  EEPROM.end();
}

void loop() {
  
  MDNS.update();
  server.handleClient();
  
    Serial.println("Sensing");
    timeClient.update();
    currentMeasureRecord.weight = sensing();
    currentMeasureRecord.dateTime = timeClient.getEpochTime();
    currentMeasureRecord.day = timeClient.getDay();
    EEPROM.begin(EEPROM_SIZE);
    //Se obtien la direccion en donde se quedo el apuntador de direcciones del logging 
    EEPROM.get(EEPROM_ADDRESS_LOGGING_EEPROM_ADDRESS, loggingEepromAddres);
    Serial.println(loggingEepromAddres);
    // Si la direccion en donde se quedo es la misma que la inicial, entonces es el
    // primer registro de medicion y no se hace la comparacion
    if(loggingEepromAddres > EEPROM_LOGGING_ADDRESS_OFFSET)
    {
       // Se obtiene la medicion anterior antes de guardar la medicion actual y perder el valor actual de loggingEepromAddres
       EEPROM.get(loggingEepromAddres - sizeof(MeasureRecord) - 1, lastMeasureRecord);
       
       // Se guarda la medicion
       loggingEepromAddres = saveMeasure(loggingEepromAddres, currentMeasureRecord);
       EEPROM.put(EEPROM_ADDRESS_LOGGING_EEPROM_ADDRESS, loggingEepromAddres);
       
       //Si hay diferencia entre las mediciones se considera que ha habido un cambio
      if((currentMeasureRecord.weight - lastMeasureRecord.weight) >= DIFFERENCE_TOLERANCE_BETWEEN_MEASURES)
      {
        // Se suman el tiempo de intervalo en minutos
        averageTimeONPerDayMin+=UPDATE_INTERVAL_MIN;
      }
      Serial.print("last: ");
      Serial.println(lastMeasureRecord.dateTime);
      //Se suma la diferencia entre la medicion actual y la pasada
      float intervalInMinutes = UPDATE_INTERVAL_MIN;
      consumptionPerDay+=((currentMeasureRecord.weight - lastMeasureRecord.weight)/intervalInMinutes);
      sensingCounterPerDay+=1.0;
      if(sensingCounterPerDay != 0.0f)
      {
        averageConsumptionPerDay = consumptionPerDay/sensingCounterPerDay;
      }
      Serial.println(currentMeasureRecord.weight);
      Serial.println(lastMeasureRecord.weight);
      Serial.print("Consumption: ");
      Serial.print(averageConsumptionPerDay); // kg/min
    }
    else
    {
       loggingEepromAddres = saveMeasure(loggingEepromAddres, currentMeasureRecord);
       EEPROM.put(EEPROM_ADDRESS_LOGGING_EEPROM_ADDRESS, loggingEepromAddres);
    }
      Serial.println(loggingEepromAddres);
      EEPROM.commit();
      EEPROM.end();
      delay(UPDATE_INTERVAL_MS);
}
