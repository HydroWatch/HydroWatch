#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

//WiFi Credentials
#define WIFI_SSID "wifi_ssid"
#define WIFI_PASSWORD "wifi_password"


//ThingsBoard Initialization
//Device access token
constexpr char TOKEN[] = "thingsboard_token";
//Thingsboard we want to establish a connection too
constexpr char THINGSBOARD_SERVER[] = "thingsboard.cloud";
//MQTT Password
constexpr char THINGSBOARD_PASSWORD[] = "";
//MQTT port used to communicate with the server, 1883 is the default unencrypted MQTT port
constexpr uint16_t THINGSBOARD_PORT = 1883U;                
//Maximum size packets will ever be sent or received by the underlying MQTT client,
//if the size is to small messages might not be sent or received messages will be discarded
constexpr uint16_t MAX_MESSAGE_SIZE = 1024U;


//Baud rate for debugging serial connections
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;


//Initialize underlying client, used to establish a connecion
WiFiClient wifiClient;
//Initialize the Mqtt client instance
PubSubClient mqttClient(wifiClient);


//Function Prototypes
void ConnectToWiFi();
void readFile(fs::FS &fs, const char *path);
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);
void deleteFile(fs::FS &fs, const char *path);
void updateSerial();
void storeMessage(const char *path);
void createPayload(String node_number);
void publishData();

//Global variables
uint8_t cardType;
uint64_t cardSize;
String message, date_and_time, sensor_reading, payload;
String passwordS1 = "password1", passwordS2 = "password2", passwordS3 = "password3", passwordSMS;
int param_n;    // No. of parameters sent
float sensor_reading_float[11];
uint32_t s1_date_and_time = 0, s2_date_and_time = 0, s3_date_and_time = 0, new_date_and_time;
unsigned long start_time, end_time, run_time;   // time variables for testing scalability


void setup() {
  // put your setup code here, to run once:
  Serial.begin(SERIAL_DEBUG_BAUD);    // Software Serial
  Serial2.begin(SERIAL_DEBUG_BAUD);   // Hardware Serial2 (RX2, TX2)
  ConnectToWiFi();
  mqttClient.setServer(THINGSBOARD_SERVER, THINGSBOARD_PORT);
  mqttClient.setBufferSize(1024);     // Set mqtt client buffer size to larger value to accomodate longer messages
  delay(3000);


  // Check SD card
  if (!SD.begin(5)) {
    Serial.println("Card Mount Failed");
    return;
  }

  cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");  // Check microSD card type
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  cardSize = SD.cardSize() / (1024 * 1024);  // Check microSD card size
  Serial.printf("SD Card Size: %lluMB\n", cardSize);

  // Check if sensor1_data.txt exists, if not create sensor1_data.txt
  File file = SD.open("/sensor1_data.txt");
  if (!file) {
    Serial.println("File doesn't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/sensor1_data.txt", "date&time, Temp, EC, pH, TDS, Turb, DO, Chl-a F1, Chl-a F2, Chl-a F3, Chl-a F4, Chl-a F5, Chl-a F6, Chl-a F7, Chl-a F8, Chl-a Clear, Chl-a NIR, P \r\n");
  } else {
    Serial.println("File already exists");
  }

  // Check if sensor2_data.txt exists, if not create sensor2_data.txt
  file = SD.open("/sensor2_data.txt");
  if (!file) {
    Serial.println("File doesn't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/sensor2_data.txt", "date&time, Temp, EC, pH, TDS, Turb, DO, Chl-a F1, Chl-a F2, Chl-a F3, Chl-a F4, Chl-a F5, Chl-a F6, Chl-a F7, Chl-a F8, Chl-a Clear, Chl-a NIR, P \r\n");
  } else {
    Serial.println("File already exists");
  }
  file.close();

  // Check if sensor3_data.txt exists, if not create sensor3_data.txt
  file = SD.open("/sensor3_data.txt");
  if (!file) {
    Serial.println("File doesn't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/sensor3_data.txt", "date&time, Temp, EC, pH, TDS, Turb, DO, Chl-a F1, Chl-a F2, Chl-a F3, Chl-a F4, Chl-a F5, Chl-a F6, Chl-a F7, Chl-a F8, Chl-a Clear, Chl-a NIR, P \r\n");
  } else {
    Serial.println("File already exists");
  }
  file.close();


  //Initializing GSM Module
  Serial2.println("AT");          //Once the handshake test is successful, it will send back OK
  updateSerial();
  Serial2.println("AT+CMGF=1");   //Configure to TEXT mode
  updateSerial();
  Serial2.println("AT+CNMI=1,2,0,0,0");  //Decides how newly arrived SMS messages should be handled
  updateSerial();

  Serial.println("Starting");
}

void loop() {
  // Reconnect to WiFi, if needed
  if (WiFi.status() != WL_CONNECTED) {
    ConnectToWiFi();
    return;
  }

  //Check if there is received message
  while (Serial.available()) {
    Serial2.write(Serial.read());
  }
  while (Serial2.available()) {
    start_time = millis();    // Start recording time for testing purposes
    String sms = Serial2.readString();

    int index_header = sms.lastIndexOf("\n\n\r\n");
    message = sms.substring(51, index_header);  //SMS header contains 51 characters

    Serial.println();
    Serial.println(message);

    /*
    Expected SMS payload:
    “<password>\n”
    “<unix_time>\n”
    “6,<Temp>,<EC>,<pH>,<TDS>,<Turb>,<DO>\n”

    or

    “<password>\n”
    “<unix_time>\n”
    “11,<Chl-a F1>,<Chl-a F2>,<Chl-a F3>,<Chl-a F4>,<Chl-a F5>,<Chl-a F6>,<Chl-a F7>,<Chl-a F8>,<Chl-a Clear>,<Chl-a NIR>,<P>\n”
    */

    // Check password
    if (passwordS1 == message.substring(0,9) || passwordS2 == message.substring(0, 9) || passwordS3 == message.substring(0, 9)) {
      // Extract password
      int index_message = message.indexOf("\n");
      passwordSMS = message.substring(0, index_message);
      message = message.substring(index_message + 1);

      // Extract Date and Time
      index_message = message.indexOf("\n");
      date_and_time = message.substring(0, index_message);
      message = message.substring(index_message + 1);

      // Extract Number of Parameters (expected value is 6 or 11) and Sensor Readings
      index_message = message.indexOf(",");
      param_n = message.substring(0, index_message).toInt();\
      sensor_reading = message.substring(index_message + 1);
      
      //Parse Date
      new_date_and_time = date_and_time.toInt();

      // Reconnect to ThingsBoard
      mqttClient.disconnect();
      
      if (!mqttClient.connected()) {
        
        //Connect to ThingsBoard
        Serial.print("Connecting to: ");
        Serial.print(THINGSBOARD_SERVER);
        Serial.print(" with token ");
        Serial.println(TOKEN);
        if (!mqttClient.connect("ESP32Client", TOKEN, THINGSBOARD_PASSWORD)) {
          Serial.println("Failed to connect");
          return;
        } else {
          Serial.println("Connected.\n");
        }
      }

      if (passwordS1 == passwordSMS) {
        storeMessage("/sensor1_data.txt");
        if (new_date_and_time > s1_date_and_time) {
          s1_date_and_time = new_date_and_time;
          unpackMessage();
          createPayload("1");
          publishData();
        } else if (new_date_and_time == s1_date_and_time) {
          unpackMessage();
          createPayload("1");
          publishData();
        } else {
          Serial.println("Invalid.");
          return;
        }
      }

      if (passwordS2 == passwordSMS) {
        storeMessage("/sensor2_data.txt");
        if (new_date_and_time > s2_date_and_time) {
          s2_date_and_time = new_date_and_time;
          unpackMessage();
          createPayload("2");
          publishData();
        } else if (new_date_and_time == s2_date_and_time) {
          unpackMessage();
          createPayload("2");
          publishData();
        } else {
          Serial.println("Invalid.");
          return;
        }
      }

      if (passwordS3 == passwordSMS) {
        storeMessage("/sensor3_data.txt");
        if (new_date_and_time > s3_date_and_time) {
          s3_date_and_time = new_date_and_time;
          unpackMessage();
          createPayload("3");
          publishData();
        } else if (new_date_and_time == s3_date_and_time) {
          unpackMessage();
          createPayload("3");
          publishData();
        } else {
          Serial.println("Invalid.");
          return;
        }
      }
    } else {
      return;
    }
    mqttClient.loop();
    end_time = millis();   // End recording time for testing purposes
    run_time = end_time - start_time;   // Compute run time
    Serial.printf("Run time: %d ms", run_time);
  }
}

void ConnectToWiFi() {
  Serial.println("Connecting to AP ...");

  //Attempt to connect to WiFi network

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Connected to AP");
}

void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void deleteFile(fs::FS &fs, const char *path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void updateSerial() {
  delay(500);

  while (Serial.available()) {
    Serial2.write(Serial.read()); //Forward what Serial received to Software Serial Port
  }
  
  while (Serial2.available()) {
    Serial.write(Serial2.read()); //Forward what Software Serial received to Serial Port
  }
}

void storeMessage(const char *path) {
  String log_entry;
  
  if (sensor_reading.endsWith("\n")) {
    sensor_reading.trim();
  }

  Serial.println("Saving data...");
  if (param_n == 6) {
    log_entry = date_and_time + "," + sensor_reading + ", , , , , , , , , , , \n";
  } else if (param_n == 11) {
    log_entry = date_and_time + ", , , , , , ," + sensor_reading + "\n";
  }
  Serial.print("Log entry: " + log_entry);

  appendFile(SD, path, log_entry.c_str());  // Append the data to file
}

void unpackMessage() {
  String sr_str = sensor_reading;  // sr_str = sensor reading buffer
  String temp_sr_str;

  for (int i = 0; i < param_n; i++) {
    int index_message = sr_str.indexOf(",");
    temp_sr_str = sr_str.substring(0, index_message);
    sensor_reading_float[i] = temp_sr_str.toFloat();
    // add print here for checking
    sr_str = sr_str.substring(index_message + 1);
  }
}

void createPayload(String node_number) {
  String parameters[17] = {"Temp", "EC", "pH", "TDS", "Turb", "DO", "Chl-a F1", "Chl-a F2", "Chl-a F3", "Chl-a F4", "Chl-a F5", "Chl-a F6", "Chl-a F7", "Chl-a F8", "Chl-a Clear", "Chl-a NIR", "P"}; 

  /*
  Desired payload format:
  if param_n = 6:
  {
    "ts":date_and_time,   //date_and_time should be in millisecond accuracy (append "000" at the end)
    "values":{
      "S1 Temp": sensor_reading[0],
      "S1 EC": sensor_reading[1],
      "S1 pH": sensor_reading[2],
      "S1 TDS": sensor_reading[3],
      "S1 Turb": sensor_reading[4],
      "S1 DO": sensor_reading[5]
    }
  }

  if param_n = 11:
  {
    "ts":date_and_time,   //date_and_time should be in millisecond accuracy (append "000" at the end)
    "values":{
      "S1 Chl-a F1": sensor_reading[0],
      "S1 Chl-a F2": sensor_reading[1],
      "S1 Chl-a F3": sensor_reading[2],
      "S1 Chl-a F4": sensor_reading[3],
      "S1 Chl-a F5": sensor_reading[4],
      "S1 Chl-a F6": sensor_reading[5],
      "S1 Chl-a F7": sensor_reading[6],
      "S1 Chl-a F8": sensor_reading[7],
      "S1 Chl-a Clear": sensor_reading[8],
      "S1 Chl-a NIR": sensor_reading[9],
      "S1 P": sensor_reading[10]
    }
  }
  */

  payload = "{";
  payload += "\"ts\":" + date_and_time + "000";
  payload += ",\"values\":{";

  for (int i = 0; i < param_n; i++) {
    if (param_n == 6) {
      payload += "\"S" + node_number + " " + parameters[i] + "\":" + String(sensor_reading_float[i]);
    } else if (param_n == 11) {
      payload += "\"S" + node_number + " " + parameters[i+6] + "\":" + String(sensor_reading_float[i]);
    }
    
    if (i < (param_n - 1)) {
      payload += ",";
    }
  }

  payload += "}}";

  // Print payload for verification
  Serial.println(payload);
}

void publishData() {
  char attributes[1024];
  payload.toCharArray(attributes, 1024);
  if (mqttClient.publish("v1/devices/me/telemetry", attributes)){
    Serial.println("published successfully");
  } else {
    Serial.println("publish unsuccessful");
  }
}