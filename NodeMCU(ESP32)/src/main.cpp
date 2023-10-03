#include "driver/can.h"
#include "driver/gpio.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Firebase_ESP_Client.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <addons/SDHelper.h>
#include <addons/TokenHelper.h>
#include <math.h>

#define BL_GET_HELP 0x48 // This command is used to get the supported commands.
#define BL_GET_VER                                                             \
  0x51 // This command is used to read the bootloader version from the MCU.
#define BL_FLASH_ERASE                                                         \
  0x56 // This command is used to mass erase or sector erase of the user flash.
#define BL_MEM_WRITE_SIZE 0x57 // This command is used to get size of the data.
#define BL_MEM_WRITE_ADDRESS                                                   \
  0x58 // This command is used to get address of the data.
#define BL_MEM_WRITE_DATA                                                      \
  0x110 // This command is used to write data in to different memories of the
        // MCU.
#define BL_GO_TO_ADDR                                                          \
  0x55 // This command is used to jump bootloader to specified address.
#define FIRMWARE_OVER_THE_AIR                                                  \
  0x76 // This command is used to update the firmware of the application

#define LIST_FILES                                                             \
  0x75 // This command is used to update the firmware of the application
/* 1. Define the WiFi credentials */

#define GOTO_BL 0x66 // This command is used to goto bl from user app

#define WIFI_SSID "Hello World"
#define WIFI_PASSWORD "Zz1234567"
/* 2. Define the API Key */
#define API_KEY "AIzaSyBRLVxz8mWs3cTkZiChXLglEiIwb3EIKMI"
#define DATABASE_URL                                                           \
  "nodemcu-5ffde-default-rtdb.europe-west1.firebasedatabase.app/" //<databaseName>.firebaseio.com
                                                                  // or
                                                                  //<databaseName>.<region>.firebasedatabase.app

/* 3. Define the user Email and password that alreadey registerd or added in
 * your project */
#define USER_EMAIL "feslam895@gmail.com"
#define USER_PASSWORD "123456789"

/* 4. Define the Firebase storage bucket ID e.g bucket-name.appspot.com */
#define STORAGE_BUCKET_ID "nodemcu-5ffde.appspot.com"

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
MDNSResponder mdns;
WebServer server(80);

int led = 2;
bool taskCompleted = false;
bool isClientConnected = false;

class MyObject {
  uint8_t id;
  String fileList[5]; // Assuming a fixed size array of 5 strings
  uint8_t response;

  int currentIndex = 0;

public:
  MyObject(uint8_t objId) { id = objId; }

  void addFile(const String &fileName) {
    // Add the file to the fileList array at the current index
    fileList[currentIndex] = fileName;

    // Update the current index for the next file
    currentIndex = (currentIndex + 1) % 5;
  }

  void setResponse(uint8_t resp) { response = resp; }

  uint8_t getId() { return id; }

  String *getFileList() { return fileList; }

  uint8_t getResponse() { return response; }
};

void clear_can_buffer() {
  can_message_t message;
  while (can_receive(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
  };
  return;
}

/* Recieve ACK */
uint8_t recieve_heartbeat() {
  can_message_t message;

  while (can_receive(&message, pdMS_TO_TICKS(10000)) != ESP_OK) {
  };
  Serial.print("ACK Type: ");

  switch (message.identifier) {
  case BL_GET_VER:
    Serial.println("Version");
    Serial.print("APP VER: ");
    Serial.println(message.data[0]);
    return message.data[0];
    break;
  case FIRMWARE_OVER_THE_AIR:
    Serial.println("OTA ");
    if (message.data[0] == 0) {
      Serial.println("Frame Flashed Successfully");
    } else {
      Serial.println("Problem FLashing Frame");
    }
    return message.data[0];
    break;
  case 3:
    // code to execute if num is equal to 3
    break;
  default:
    // code to execute if num is not equal to any of the above cases
    break;
  }
  return 0;
}

/* List Files from SPIFFS and send it as response for an HTTP request*/
MyObject listFiles() {

  File root = SPIFFS.open("/");
  File file = root.openNextFile();

  Serial.println("\nListing Files: \n");
  MyObject obj(LIST_FILES);
  while (file) {

    String fileName = file.name();
    Serial.print("FILE: ");
    Serial.println(fileName);
    // Add the file name to the fileList array
    obj.addFile(fileName);

    file = root.openNextFile();
  }
  Serial.println("Files Read Done\n");

  return obj;
}

/* Delete Files from SPIFFS */
MyObject deleteAllFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  MyObject obj(LIST_FILES);
  while (file) {
    if (!file.isDirectory()) {
      Serial.println(file.name());
      String filePath = "/";
      filePath += file.name();
      SPIFFS.remove(filePath.c_str());
    }
    file = root.openNextFile();
  }
  root.close();
  obj.setResponse(1);
  return obj;
}
// The Firebase Storage download callback function
void fcsDownloadCallback(FCS_DownloadStatusInfo info) {
  if (info.status == fb_esp_fcs_download_status_init) {
    Serial.printf("Downloading file %s (%d) to %s\n",
                  info.remoteFileName.c_str(), info.fileSize,
                  info.localFileName.c_str());
  } else if (info.status == fb_esp_fcs_download_status_download) {
    Serial.printf("Downloaded %d%s, Elapsed time %d ms\n", (int)info.progress,
                  "%", info.elapsedTime);
  } else if (info.status == fb_esp_fcs_download_status_complete) {
    Serial.println("Download completed\n");
  } else if (info.status == fb_esp_fcs_download_status_error) {
    Serial.printf("Download failed, %s\n", info.errorMsg.c_str());
  }
}

/* Send binary file from SPIFFS to STM32 Target */
void send_binary_file() {
  // Configure message to transmit
  clear_can_buffer();
  can_message_t message;
  message.flags = CAN_MSG_FLAG_NONE;
  message.identifier = 0x57;
  // message.extd =0; //standard format not extended format
  message.data_length_code = 2;
  // ack_no
  uint8_t ack_no = 0;

  File file = SPIFFS.open("/data0.bin", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("File size: ");
  Serial.println(file.size());
  int file_size = file.size();
  int frames = ceil(file_size / (float)8);
  Serial.print("frames no.: ");
  Serial.println(frames, HEX);
  // 0x 00000000 00000000
  message.data[0] = frames;
  message.data[1] = (frames >> 8);
  Serial.print("frames no.: ");
  Serial.println(message.data[0], HEX);
  Serial.println(message.data[1], HEX);

  if (can_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
    Serial.println("Message queued for transmission (Size)");
    ack_no++;
  } else {
    Serial.println("Failed to queue message for transmission(Size)");
  }

  message.identifier = 0x76;
  while (file_size > 0) {
    ack_no++;
    if (file_size < 8) {
      message.data_length_code = 4;
      for (int i = 0; i < 4; i++) {
        byte b = file.read();
        message.data[i] = b;
      }
      if (can_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {

        Serial.print("Message queued for transmission: 0x ");
        for (int j = 0; j < 4; j++) {
          Serial.print(message.data[j] < 16 ? "0" : "");
          Serial.print(message.data[j], HEX);
        }
        Serial.println();
        if (ack_no == 3) {
          ack_no = 0;
          recieve_heartbeat();
        }
      } else {
        printf("Failed to queue message for transmission\n");
      }
    } else {
      message.data_length_code = 8;
      for (int i = 0; i < 8; i++) {
        byte b = file.read();
        message.data[i] = b;
      }
      if (can_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {

        Serial.print("Message queued for transmission: 0x ");
        for (int j = 0; j < 8; j++) {
          Serial.print(message.data[j] < 16 ? "0" : "");
          Serial.print(message.data[j], HEX);
        }
        Serial.println();
        if (ack_no == 3) {
          ack_no = 0;
          recieve_heartbeat();
        }
      } else {
        printf("Failed to queue message for transmission\n");
        break;
      }
    }

    file_size -= 8;
  }

  printf("File Sent ------------------\n");

  file.close();
}

/* Receive user app version from the target and send it as response for an HTTP
 * request */
MyObject get_info() {
  clear_can_buffer();
  can_message_t message;
  message.flags = CAN_MSG_FLAG_NONE;
  message.identifier = BL_GET_VER;
  message.data_length_code = 1;
  if (can_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
    Serial.println("Message queued for transmission (Info)");
  } else {
    Serial.println("Failed to queue message for transmission(Info)");
  }

  MyObject obj(BL_GET_VER);
  obj.setResponse(recieve_heartbeat());

  return obj;
}

/* Download the binary file from firebase remote storage to SPIFFS */
void download_binary_file() {
  String tme = "0";
  if (Firebase.ready()) {
    Serial.printf("\nList files... %s\n",
                  Firebase.Storage.listFiles(&fbdo, STORAGE_BUCKET_ID)
                      ? "ok"
                      : fbdo.errorReason().c_str());
    FileList *files = fbdo.fileList();
    if (fbdo.httpCode() == FIREBASE_ERROR_HTTP_CODE_OK) {
      for (size_t i = 0; i < files->items.size(); i++) {
        Serial.printf("name: %s, bucket: %s\n", files->items[i].name.c_str(),
                      files->items[i].bucket.c_str());
      }
      Serial.printf(
          "Get timestamp... %s\n",
          Firebase.RTDB.getDouble(&fbdo, "/myFiles/metadata/timeCreated")
              ? "ok"
              : fbdo.errorReason().c_str());
      Firebase.RTDB.getString(&fbdo, "/myFiles/metadata/timeCreated");
      printf("TIMESTAMP is: %s\n", fbdo.to<String>().c_str());
      String timestamp = fbdo.to<String>().c_str();
      if (timestamp != tme) {
        tme = timestamp;

        Firebase.Storage.download(
            &fbdo, STORAGE_BUCKET_ID /* Firebase Storage bucket id */,
            "FOTA" /* path of remote file stored in the bucket */,
            "/data0.bin" /* path to local file */,
            mem_storage_type_flash /* memory storage type,
                                      mem_storage_type_flash and
                                      mem_storage_type_sd */
            ,
            fcsDownloadCallback /* callback function */);
      }
    }
  }
}

/*function to handle a POST request and according to parameter called data it
 * does diffrent functionality*/
void handleData() {
  // Get the values of the parameters
  String requestBody = server.arg("plain");
  // Parse the JSON data
  DynamicJsonDocument jsonDocument(1024);
  DeserializationError error = deserializeJson(jsonDocument, requestBody);
  if (error) {
    // Handle JSON parsing error
    Serial.println("Error parsing JSON");
    server.send(400, "text/plain", "Invalid JSON format.");
    return;
  }
  // Access the JSON data
  String param1Value = jsonDocument["data"].as<String>();
  // Do something with the received parameters
  // Example: Print the values to Serial monitor
  Serial.print("Param1: ");
  Serial.println(param1Value);
  // Create a new JSON object to include in the response
  JsonObject response = jsonDocument.to<JsonObject>();

  if (param1Value == "get-info") {
    MyObject obj = get_info();
    response["id"] = obj.getId();
    response["response"] = obj.getResponse();
    // Send a JSON response to the client
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  } else if (param1Value == "download-file") {
    // sending progress using sse or sockets is better improvment
    download_binary_file();
    response["id"] = FIRMWARE_OVER_THE_AIR;
    response["response"] = "Request recieved successfully";
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  } else if (param1Value == "send-binary") {
    // sending progress using sse or sockets is better improvment
    send_binary_file();
    response["id"] = FIRMWARE_OVER_THE_AIR;
    response["response"] = "Request recieved successfully";
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  }

  else if (param1Value == "delete-files") {
    MyObject obj = deleteAllFiles();
    response["id"] = obj.getId();
    response["response"] = obj.getResponse();
    // Send a JSON response to the client
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  } else if (param1Value == "list-files") {
    MyObject obj = listFiles();
    response["id"] = obj.getId();
    // Create a JSON array for the "files" parameter
    JsonArray filesArray = response.createNestedArray("files");
    // Populate the "files" array with file names
    String *fileList = obj.getFileList();
    for (int i = 0; i < 5; i++) {
      // Check if the file name is empty (assuming an empty string indicates
      // no more files)
      if (fileList[i].length() == 0)
        break;
      // Add the file name to the array
      filesArray.add(fileList[i]);
    }
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  } else if (param1Value == "goto-bl") {
    can_message_t message;
    message.flags = CAN_MSG_FLAG_NONE;
    message.identifier = GOTO_BL;
    message.data_length_code = 1;
    if (can_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
      Serial.println("Message queued for transmission (Info)");
    } else {
      Serial.println("Failed to queue message for transmission(Info)");
    }

    response["id"] = GOTO_BL;
    response["response"] = "Request recieved successfully";
    // Send a JSON response to the client
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  }
}

void setup() {
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  Serial.begin(115200);

  can_general_config_t g_config = CAN_GENERAL_CONFIG_DEFAULT(
      GPIO_NUM_5, GPIO_NUM_4,
      CAN_MODE_NORMAL); // el pins bta3et el tx w el rx
  can_timing_config_t t_config = CAN_TIMING_CONFIG_500KBITS(); // kanet 500kbits
  can_filter_config_t f_config = CAN_FILTER_CONFIG_ACCEPT_ALL();

  //    Install CAN driver
  if (can_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    printf("Driver installed\n");
  } else {
    printf("Failed to install driver\n");
    return;
  }

  //    Start CAN driver
  if (can_start() == ESP_OK) {
    printf("Driver started\n");
  } else {
    printf("Failed to start driver\n");
    return;
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  unsigned long ms = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  if (mdns.begin("esp")) {
    Serial.println("MDNS responder started");
  }
  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
  /* Assign the api key (required) */
  config.api_key = API_KEY;
  /* Assign the user sign in credentials */
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  /* Assign the callback function for the long running token generation task
   */
  config.token_status_callback =
      tokenStatusCallback; // see addons/TokenHelper.h
  /* Assign download buffer size in byte */
  // Data to be downloaded will read as multiple chunks with this size, to
  // compromise between speed and memory used for buffering. The memory from
  // external SRAM/PSRAM will not use in the TCP client internal rx buffer.
  config.fcs.download_buffer_size = 2048;

  Firebase.begin(&config, &auth);

  Firebase.reconnectWiFi(true);

  server.on("/ledON", []() {
    server.send(200, "text/plain", "Request received successfully.");
    digitalWrite(led, HIGH);
    delay(1000);
  });

  server.on("/ledOFF", []() {
    server.send(200, "text/plain", "Request received successfully.");
    digitalWrite(led, LOW);
    delay(1000);
  });

  /*Listen for POST request with url /data then calles handleData*/
  server.on("/data", HTTP_POST, handleData);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  /*listen for incoming requests*/
  server.handleClient();
}
