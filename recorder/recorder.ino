#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_BMP280.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Wire.h>

const char *ssid = "MISSION_CONTROL";
const char *password = "password123";
WebServer server(80);
Adafruit_BMP280 bmp;
File dataFile;
bool recording = false;
bool firstEntry = true;
unsigned long lastSample = 0;
const int sampleInterval = 100; // 100ms
const float P0 = 1013.25; // Standard sea-level pressure in hPa
String statusMessage = "";
String currentFileName = "data.txt";

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

  Wire.begin(8, 9);
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
    dataFile = SPIFFS.open(filePath, "a");
    if (dataFile) {
      String json = "{\"altitude\": " + String(altitude, 2) + ", \"time\": " + String(millis()) + "}";
      if (!firstEntry) dataFile.print(",");
      dataFile.println(json);
      firstEntry = false;
      dataFile.close();
      Serial.println("Data written to " + filePath + ": " + json + (firstEntry ? " (first)" : ""));
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
  html += "body { font-family: Arial, sans-serif; margin: 5vw; background: #f4f4f9; color: #333; }";
  html += ".container { max-width: 90vw; margin: auto; padding: 3vw; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += "h1 { font-size: 5vw; color: #2c3e50; margin: 2vw 0; }";
  html += "p { font-size: 3.5vw; margin: 2vw 0; }";
  html += "form { margin: 3vw 0; }";
  html += "label { font-size: 4vw; }";
  html += "input[type='text'] { padding: 2vw; width: 50vw; max-width: 200px; border: 1px solid #ddd; border-radius: 4px; font-size: 3.5vw; }";
  html += "input[type='submit'] { padding: 2vw 4vw; background: #3498db; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 3.5vw; }";
  html += "input[type='submit']:hover { background: #2980b9; }";
  html += "input[disabled] { background: #ccc; cursor: not-allowed; }";
  html += "button { padding: 2vw 4vw; background: #3498db; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 3.5vw; }";
  html += "button:hover { background: #2980b9; }";
  html += "ul { list-style: none; padding: 0; }";
  html += "li { padding: 2vw; font-size: 3.5vw; }";
  html += ".file-entry:nth-child(even) { background: #e8ecef; }";
  html += ".file-entry:nth-child(odd) { background: #ffffff; }";
  html += ".file-info { margin-bottom: 1vw; }";
  html += ".file-actions { margin-top: 1vw; }";
  html += "a { color: #3498db; text-decoration: none; margin-right: 2vw; font-size: 3.5vw; }";
  html += "a:hover { text-decoration: underline; }";
  html += "@media (max-width: 600px) {";
  html += "h1 { font-size: 6vw; }";
  html += "p, a, input, button, li { font-size: 4vw; }";
  html += "label { font-size: 4.5vw; }";
  html += "input[type='text'] { width: 70vw; }";
  html += ".container { padding: 4vw; }";
  html += "}";
  html += "</style><script>";
  html += "function confirmDelete(filename) {";
  html += "return confirm('Are you sure you want to delete ' + filename + '?');";
  html += "}";
  html += "</script></head><body><div class='container'>";
  html += "<h1>BMP280 Data</h1>";
  html += "<p>Pressure: " + String(pressure) + " hPa</p>";
  html += "<p>Altitude: " + String(altitude) + " m</p>";
  html += "<p>Temperature: " + String(bmp.readTemperature()) + " °C</p>";
  html += "<p>Recording: " + String(recording ? "In Progress" : "Not Active") + "</p>";
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
  html += "<h2>All Recordings</h2><ul>";
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  int fileIndex = 0;
  while (file) {
    if (!file.isDirectory()) {
      String fname = file.name();
      if (fname != "state.txt") {
        size_t fsize = file.size();
        html += "<li class='file-entry'>";
        html += "<div class='file-info'>" + fname + " (" + String(fsize) + " bytes)</div>";
        html += "<div class='file-actions'>";
        html += "<a href='/download?file=" + fname + "'>Download</a>";
        html += "<a href='/delete?file=" + fname + "' onclick=\"return confirmDelete('" + fname + "')\">Delete</a>";
        if (!recording || fname != currentFileName) {
          html += "<a href='/graph?file=" + fname + "'>Graph</a>";
        }
        if (recording && fname == currentFileName) {
          html += "<a href='/stop'>Stop</a>";
        }
        html += "</div></li>";
        fileIndex++;
      }
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
  firstEntry = true;
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
  firstEntry = true;
  String filePath = "/" + currentFileName;
  dataFile = SPIFFS.open(filePath, "r+");
  if (dataFile) {
    if (dataFile.size() > 1) {
      dataFile.seek(dataFile.size() - 1);
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
    html += "body { font-family: Arial, sans-serif; margin: 5vw; background: #f4f4f9; color: #333; }";
    html += ".container { max-width: 90vw; margin: auto; padding: 3vw; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += "h1 { font-size: 5vw; color: #2c3e50; margin: 2vw 0; }";
    html += "canvas { width: 100%; height: 50vw; max-height: 300px; border: 1px solid #ddd; }";
    html += "p { font-size: 3.5vw; margin: 2vw 0; }";
    html += "a { color: #3498db; text-decoration: none; font-size: 3.5vw; }";
    html += "a:hover { text-decoration: underline; }";
    html += "@media (max-width: 600px) {";
    html += "h1 { font-size: 6vw; }";
    html += "p, a { font-size: 4vw; }";
    html += "canvas { height: 60vw; }";
    html += ".container { padding: 4vw; }";
    html += "}";
    html += "</style></head><body><div class='container'>";
    html += "<h1>Altitude Graph for " + fileName + "</h1>";
    if (SPIFFS.exists(filePath)) {
      dataFile = SPIFFS.open(filePath, "r");
      if (dataFile) {
        String content = "";
        while (dataFile.available()) {
          content += (char)dataFile.read();
        }
        dataFile.close();
        content.trim();
        Serial.println("Raw content for " + filePath + ": [" + content + "]");
        if (content.length() > 2 && content.startsWith("[") && content.endsWith("]")) {
          html += "<canvas id='chart'></canvas>";
          html += "<script>";
          html += "function drawChart(c, w, h, v, l, b) {";
          html += "var x=c.getContext('2d');c.width=w;c.height=h;";
          html += "x.fillStyle='#fff';x.fillRect(0,0,w,h);";
          html += "x.strokeStyle='#000';x.beginPath();x.moveTo(0,h-b);";
          html += "for(var i=0;i<v.length;i++){x.lineTo((i*w)/(v.length-1),h-(v[i]*h/l));}";
          html += "x.stroke();}";
          html += "var data = " + content + ";";
          html += "if (data && data.length > 0) {";
          html += "var altitudes = data.map(function(d) { return parseFloat(d.altitude); }).filter(function(a) { return !isNaN(a); });";
          html += "var times = data.map(function(d) { return parseFloat(d.time); }).filter(function(t) { return !isNaN(t); });";
          html += "if (altitudes.length > 0) {";
          html += "var maxAlt = Math.max(...altitudes);";
          html += "var minAlt = Math.min(...altitudes);";
          html += "var range = maxAlt - minAlt || 1;";
          html += "document.write('<p>Altitude Range: ' + (maxAlt - minAlt).toFixed(2) + ' m</p>');";
          html += "var canvas = document.getElementById('chart');";
          html += "drawChart(canvas, 600, 300, altitudes.map(function(a) { return (a - minAlt) / range; }), 1, 0.1);";
          html += "} else { document.write('<p>No valid altitude data found</p>'); }";
          html += "} else { document.write('<p>No data in file</p>'); }";
          html += "</script>";
          Serial.println("Graph for " + filePath + ": " + String(content.length()) + " bytes parsed");
        } else {
          html += "<p>Invalid or empty JSON file: " + fileName + "</p>";
          Serial.println("Invalid JSON in " + filePath + ": " + content);
        }
      } else {
        html += "<p>Error opening file: " + fileName + "</p>";
        Serial.println("Failed to open " + filePath + " for graph");
      }
    } else {
      html += "<p>File not found: " + fileName + "</p>";
      Serial.println("File " + filePath + " does not exist for graph");
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