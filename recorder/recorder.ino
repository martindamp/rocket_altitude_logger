#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_BMP280.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Wire.h>

const char *ssid = "ESP32AP";
const char *password = "password123";
WebServer server(80);
Adafruit_BMP280 bmp;
File dataFile;
bool recording = false;
unsigned long lastSample = 0;
const int sampleInterval = 100; // 100ms
const float P0 = 1013.25; // Standard sea-level pressure in hPa
String statusMessage = "";
String currentFileName = "data.txt"; // Default recording name

float pressureToAltitude(float pressure) {
  return 44330.77 * (1 - pow(pressure / P0, 0.190263));
}

void setup() {
  Serial.begin(115200);
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    while (1);
  }
  Serial.println("SPIFFS mounted successfully");

  Wire.begin(8, 9); // SDA = GPIO 8, SCL = GPIO 9
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  if (!bmp.begin(0x76)) {
    Serial.println("BMP280 not found");
    while (1);
  }

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/download", handleDownload);
  server.on("/delete", handleDelete);
  server.on("/setname", handleSetName);
  server.on("/graph", handleGraph);
  server.begin();
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();
  if (recording && millis() - lastSample >= sampleInterval) {
    float pressure = bmp.readPressure() / 100.0F;
    float altitude = pressureToAltitude(pressure);
    String filePath = "/" + currentFileName;
    dataFile = SPIFFS.open(filePath, "r");
    bool isEmpty = !dataFile || dataFile.size() == 0;
    if (dataFile) dataFile.close();
    dataFile = SPIFFS.open(filePath, "a");
    if (dataFile) {
      String json = "{\"altitude\": " + String(altitude, 2) + ", \"time\": " + String(millis()) + "}";
      if (!isEmpty) dataFile.print(",");
      dataFile.println(json);
      dataFile.close();
      Serial.println("Data written to " + filePath);
    } else {
      Serial.println("Failed to open " + filePath + " for writing");
    }
    lastSample = millis();
  }
}

void handleRoot() {
  float pressure = bmp.readPressure() / 100.0F;
  float altitude = pressureToAltitude(pressure);
  String html = "<html><head><style>";
  html += "body { font-family: Arial, sans-serif; margin: 40px; background: #f4f4f9; color: #333; }";
  html += ".container { max-width: 600px; margin: auto; padding: 20px; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += "h1 { color: #2c3e50; }";
  html += "p { margin: 10px 0; }";
  html += "form { margin: 20px 0; }";
  html += "input[type='text'] { padding: 8px; width: 200px; border: 1px solid #ddd; border-radius: 4px; }";
  html += "input[type='submit'] { padding: 8px 16px; background: #3498db; color: white; border: none; border-radius: 4px; cursor: pointer; }";
  html += "input[type='submit']:hover { background: #2980b9; }";
  html += "input[disabled] { background: #ccc; cursor: not-allowed; }";
  html += "button { padding: 10px 20px; background: #3498db; color: white; border: none; border-radius: 4px; cursor: pointer; margin-right: 10px; }";
  html += "button:hover { background: #2980b9; }";
  html += "ul { list-style: none; padding: 0; }";
  html += "li { padding: 10px; border-bottom: 1px solid #ddd; }";
  html += "a { color: #3498db; text-decoration: none; margin-right: 10px; }";
  html += "a:hover { text-decoration: underline; }";
  html += "</style></head><body><div class='container'>";
  html += "<h1>BMP280 Data</h1>";
  html += "<p>Pressure: " + String(pressure) + " hPa</p>";
  html += "<p>Altitude: " + String(altitude) + " m</p>";
  html += "<p>Temperature: " + String(bmp.readTemperature()) + " °C</p>";
  html += "<p>Recording: " + String(recording ? "In Progress" : "Not Active") + "</p>";
  size_t fileSize = 0;
  String filePath = "/" + currentFileName;
  if (SPIFFS.exists(filePath)) {
    dataFile = SPIFFS.open(filePath, "r");
    if (dataFile) {
      fileSize = dataFile.size();
      dataFile.close();
    }
  }
  html += "<p>File Size: " + String(fileSize) + " bytes</p>";
  size_t freeBytes = SPIFFS.totalBytes() - SPIFFS.usedBytes();
  html += "<p>Free Space: " + String(freeBytes) + " bytes (of 1.5MB)</p>";
  html += "<p>Status: " + statusMessage + "</p>";
  html += "<form action='/setname' method='POST'>";
  html += "<label>Recording Name: </label>";
  html += "<input type='text' name='filename' value='" + currentFileName + "'";
  html += String(recording ? " disabled" : "") + String(">");
  html += "<input type='submit' value='Set Name'";
  html += String(recording ? " disabled>" : ">");
  html += "</form>";
  html += "<a href='/start'><button>Start Recording</button></a>";
  html += "<a href='/stop'><button>Stop Recording</button></a>";
  html += "<h2>All Recordings</h2><ul>";
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String fname = file.name();
      size_t fsize = file.size();
      html += "<li>" + fname + " (" + String(fsize) + " bytes) ";
      html += "(<a href='/download?file=" + fname + "'>Download</a> | ";
      html += "<a href='/delete?file=" + fname + "'>Delete</a>";
      if (!recording || fname != currentFileName) {
        html += " | <a href='/graph?file=" + fname + "'>Graph</a>";
      }
      if (recording && fname == currentFileName) {
        html += " | <a href='/stop'>Stop</a>";
      }
      html += ")</li>";
    }
    file = root.openNextFile();
  }
  root.close();
  html += "</ul></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSetName() {
  if (!recording && server.hasArg("filename")) {
    String newName = server.arg("filename");
    if (newName.length() > 0 && newName.indexOf("/") == -1) {
      if (!newName.endsWith(".txt")) newName += ".txt";
      currentFileName = newName;
      statusMessage = "File name set to " + currentFileName;
      Serial.println("File name set to " + currentFileName);
    } else {
      statusMessage = "Invalid file name";
      Serial.println("Invalid file name: " + newName);
    }
  } else {
    statusMessage = "Cannot change name while recording";
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleStart() {
  recording = true;
  String filePath = "/" + currentFileName;
  dataFile = SPIFFS.open(filePath, "w");
  if (dataFile) {
    dataFile.println("[");
    dataFile.close();
    statusMessage = "Recording started: " + currentFileName;
    Serial.println("Started recording, cleared " + filePath);
  } else {
    statusMessage = "Failed to start recording";
    Serial.println("Failed to create/clear " + filePath);
  }
  lastSample = millis();
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleStop() {
  recording = false;
  String filePath = "/" + currentFileName;
  dataFile = SPIFFS.open(filePath, "r+"); // Open for read/write
  if (dataFile) {
    if (dataFile.size() > 1) { // If file has data (more than "[")
      dataFile.seek(dataFile.size() - 1); // Move to end
      dataFile.println("]");
    } else {
      dataFile.println("]");
    }
    dataFile.close();
    statusMessage = "Recording stopped: " + currentFileName;
    Serial.println("Recording stopped: " + filePath);
  } else {
    statusMessage = "Failed to finalize recording";
    Serial.println("Failed to open " + filePath + " for finalizing");
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleDownload() {
  if (server.hasArg("file")) {
    String fileName = server.arg("file");
    String filePath = "/" + fileName;
    if (SPIFFS.exists(filePath)) {
      dataFile = SPIFFS.open(filePath, "r");
      if (dataFile) {
        server.sendHeader("Content-Type", "application/json");
        server.sendHeader("Content-Disposition", "attachment; filename=" + fileName);
        server.streamFile(dataFile, "application/json");
        dataFile.close();
        Serial.println("File " + filePath + " sent for download");
      } else {
        statusMessage = "Error opening file: " + fileName;
        Serial.println("Failed to open " + filePath + " for reading");
        server.sendHeader("Location", "/");
        server.send(302);
      }
    } else {
      statusMessage = "File not found: " + fileName;
      Serial.println("File " + filePath + " does not exist");
      server.sendHeader("Location", "/");
      server.send(302);
    }
  } else {
    statusMessage = "No file specified";
    server.sendHeader("Location", "/");
    server.send(302);
  }
}

void handleDelete() {
  if (server.hasArg("file")) {
    String fileName = server.arg("file");
    String filePath = "/" + fileName;
    if (!recording || fileName != currentFileName) {
      if (SPIFFS.exists(filePath)) {
        if (SPIFFS.remove(filePath)) {
          statusMessage = "File deleted: " + fileName;
          Serial.println("File " + filePath + " deleted");
        } else {
          statusMessage = "Failed to delete: " + fileName;
          Serial.println("Failed to delete " + filePath);
        }
      } else {
        statusMessage = "File not found: " + fileName;
        Serial.println("File " + filePath + " does not exist");
      }
    } else {
      statusMessage = "Cannot delete active recording: " + fileName;
      Serial.println("Cannot delete active file " + filePath);
    }
  } else {
    statusMessage = "No file specified";
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleGraph() {
  if (server.hasArg("file")) {
    String fileName = server.arg("file");
    String filePath = "/" + fileName;
    String html = "<html><head><style>";
    html += "body { font-family: Arial, sans-serif; margin: 40px; background: #f4f4f9; color: #333; }";
    html += ".container { max-width: 600px; margin: auto; padding: 20px; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += "h1 { color: #2c3e50; }";
    html += "table { width: 100%; border-collapse: collapse; margin: 20px 0; }";
    html += "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }";
    html += "th { background: #3498db; color: white; }";
    html += "a { color: #3498db; text-decoration: none; }";
    html += "a:hover { text-decoration: underline; }";
    html += "</style></head><body><div class='container'>";
    html += "<h1>Altitude Graph for " + fileName + "</h1>";
    if (SPIFFS.exists(filePath)) {
      dataFile = SPIFFS.open(filePath, "r");
      if (dataFile) {
        html += "<table><tr><th>Time (ms)</th><th>Altitude (m)</th></tr>";
        String content = dataFile.readString();
        dataFile.close();
        // Simple JSON parsing (assumes valid format)
        content.trim();
        if (content.startsWith("[") && content.endsWith("]")) {
          content = content.substring(1, content.length() - 1); // Remove [ and ]
          int start = 0;
          while (start < content.length()) {
            int end = content.indexOf("},", start);
            if (end == -1) end = content.length();
            String entry = content.substring(start, end);
            if (entry.indexOf("}") == -1) entry += "}";
            int altStart = entry.indexOf("\"altitude\":") + 10;
            int altEnd = entry.indexOf(",", altStart);
            int timeStart = entry.indexOf("\"time\":") + 7;
            int timeEnd = entry.indexOf("}", timeStart);
            if (altStart > 10 && altEnd > altStart && timeStart > altEnd && timeEnd > timeStart) {
              String alt = entry.substring(altStart, altEnd);
              String time = entry.substring(timeStart, timeEnd);
              html += "<tr><td>" + time + "</td><td>" + alt + "</td></tr>";
            }
            start = end + 2; // Skip }, or end
          }
        }
        html += "</table>";
      } else {
        html += "<p>Error opening file: " + fileName + "</p>";
      }
    } else {
      html += "<p>File not found: " + fileName + "</p>";
    }
    html += "<p><a href='/'>Back to Home</a></p>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  } else {
    statusMessage = "No file specified for graph";
    server.sendHeader("Location", "/");
    server.send(302);
  }
}