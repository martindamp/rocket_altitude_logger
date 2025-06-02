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
const int sampleInterval = 25; // 25ms (40 samples/sec)
const float P0 = 1013.25; // Standard sea-level pressure in hPa
String statusMessage = "";
String currentFileName = "data.txt";

float pressureToAltitude(float pressure) {
  return 44330.77 * (1 - pow(pressure / P0, 0.190263));
}

void setup() {
  Serial.begin(115200);
  Serial.println("Formating SPIFFS, this might take some time");
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    while (1);
  }
  Serial.println("SPIFFS mounted successfully");

  Wire.begin(8,9);
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
  float pressure = bmp.readPressure() / 100.0F;
  Serial.print("Pressure: ");
  Serial.println(pressure);
}

void loop() {
  server.handleClient();
  if (recording && millis() - lastSample >= sampleInterval) {
    float pressure = bmp.readPressure() / 100.0F;
    float altitude = pressureToAltitude(pressure);
    String filePath = "/" + currentFileName;
    dataFile = SPIFFS.open(filePath, "a");
    if (dataFile) {
      String json = "{\"altitude\": " + String(altitude, 2) + ", \"pressure\": " + String(pressure, 2) + ", \"time\": " + String(millis()) + "}";
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
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 2vw; background: #f4f4f9; color: #333; }";
  html += ".container { max-width: 1200px; margin: auto; padding: 20px; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += "h1 { font-size: 2rem; color: #2c3e50; margin: 1rem 0; }";
  html += "p { font-size: 1rem; margin: 1rem 0; }";
  html += "form { margin: 1.5rem 0; }";
  html += "label { font-size: 1rem; }";
  html += "input[type='text'] { padding: 8px; width: 300px; border: 1px solid #ddd; border-radius: 4px; font-size: 1rem; }";
  html += "input[type='submit'] { padding: 8px 16px; background: #3498db; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 1rem; }";
  html += "input[type='submit']:hover { background: #2980b9; }";
  html += "input[disabled] { background: #ccc; cursor: not-allowed; }";
  html += "button { padding: 8px 16px; background: #3498db; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 1rem; margin-right: 10px; }";
  html += "button:hover { background: #2980b9; }";
  html += "button[disabled] { background: #ccc; cursor: not-allowed; }";
  html += "ul { list-style: none; padding: 0; }";
  html += "li { padding: 10px; font-size: 1rem; }";
  html += ".file-entry:nth-child(even) { background: #e8ecef; }";
  html += ".file-entry:nth-child(odd) { background: #ffffff; }";
  html += ".file-info { margin-bottom: 0.5rem; }";
  html += ".file-actions { margin-top: 0.5rem; }";
  html += "a { color: #3498db; text-decoration: none; margin-right: 10px; font-size: 1rem; }";
  html += "a:hover { text-decoration: underline; }";
  html += "@media (max-width: 600px) {";
  html += "  h1 { font-size: 2.8rem; }";
  html += "  p, a, input, button, li { font-size: 1.6rem; }";
  html += "  label { font-size: 1.8rem; }";
  html += "  input[type='text'] { width: 80%; max-width: none; padding: 12px; }";
  html += "  input[type='submit'], button { padding: 12px 24px; }";
  html += "  .container { padding: 25px; }";
  html += "}";
  html += "</style><script>";
  html += "function confirmDelete(filename) {";
  html += "return confirm('Are you sure you want to delete ' + filename + '?');";
  html += "}";
  html += "</script></head><body><div class='container'>";
  html += "<h1>BMP280 Data</h1>";
  html += "<p>Pressure: " + String(pressure, 2) + " hPa</p>";
  html += "<p>Altitude: " + String(altitude, 2) + " m</p>";
  html += "<p>Temperature: " + String(bmp.readTemperature(), 2) + " °C</p>";
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
  html += "<a href='/stop'><button" + String(recording ? "" : " disabled") + ">Stop Recording</button></a>";
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

void handleGraph() {
  if (server.hasArg("file")) {
    String fileName = server.arg("file");
    String filePath = "/" + fileName;
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 2vw; background: #f4f4f9; color: #333; }";
    html += ".container { max-width: 1200px; margin: auto; padding: 20px; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += "h1 { font-size: 2rem; color: #2c3e50; margin: 1rem 0; }";
    html += "canvas { width: 100%; max-height: 400px; border: 1px solid #ddd; }";
    html += "p { font-size: 1rem; margin: 1rem 0; }";
    html += "a { color: #3498db; text-decoration: none; font-size: 1rem; }";
    html += "a:hover { text-decoration: underline; }";
    html += "@media (max-width: 600px) {";
    html += "  h1 { font-size: 2.8rem; }";
    html += "  p, a { font-size: 1.6rem; }";
    html += "  canvas { max-height: 400px; }";
    html += "  .container { padding: 25px; }";
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
          html += "function smoothData(data, windowSize) {";
          html += "  if (!data || data.length === 0) return [];";
          html += "  var smoothed = [];";
          html += "  for (var i = 0; i < data.length; i++) {";
          html += "    var sum = 0, count = 0;";
          html += "    for (var j = Math.max(0, i - Math.floor(windowSize/2)); j <= Math.min(data.length - 1, i + Math.floor(windowSize/2)); j++) {";
          html += "      var value = parseFloat(data[j]);";
          html += "      if (!isNaN(value)) { sum += value; count++; }";
          html += "    }";
          html += "    smoothed.push(count > 0 ? sum / count : data[i]);";
          html += "  }";
          html += "  return smoothed;";
          html += "}";
          html += "function drawChart(c, w, h, v, l, b) {";
          html += "  var x = c.getContext('2d'); c.width = w; c.height = h;";
          html += "  x.fillStyle = '#fff'; x.fillRect(0, 0, w, h);";
          html += "  x.strokeStyle = '#000'; x.beginPath(); x.moveTo(0, h - b);";
          html += "  for (var i = 0; i < v.length; i++) { x.lineTo((i * w) / (v.length - 1), h - (v[i] * h / l)); }";
          html += "  x.stroke();";
          html += "}";
          html += "var data = " + content + ";";
          html += "console.log('Parsed data:', data);";
          html += "if (data && data.length > 0) {";
          html += "  var altitudes = data.map(function(d) { return parseFloat(d.altitude); }).filter(function(a) { return !isNaN(a); });";
          html += "  console.log('Raw altitudes:', altitudes);";
          html += "  if (altitudes.length > 0) {";
          html += "    var smoothedAltitudes = smoothData(altitudes, 10);";
          html += "    console.log('Smoothed altitudes:', smoothedAltitudes);";
          html += "    if (smoothedAltitudes.length > 0 && smoothedAltitudes.every(function(a) { return !isNaN(a); })) {";
          html += "      var maxAlt = Math.max(...smoothedAltitudes);";
          html += "      var minAlt = Math.min(...smoothedAltitudes);";
          html += "      var range = maxAlt - minAlt || 1;";
          html += "      document.write('<p>Altitude Range: ' + (maxAlt - minAlt).toFixed(2) + ' m</p>');";
          html += "      var canvas = document.getElementById('chart');";
          html += "      drawChart(canvas, 600, 400, smoothedAltitudes.map(function(a) { return (a - minAlt) / range; }), 1, 0.1);";
          html += "    } else {";
          html += "      console.error('Invalid smoothed altitudes:', smoothedAltitudes);";
          html += "      document.write('<p>Error: Invalid smoothed data</p>');";
          html += "    }";
          html += "  } else {";
          html += "    console.error('No valid altitude data:', altitudes);";
          html += "    document.write('<p>No valid altitude data found</p>');";
          html += "  }";
          html += "} else {";
          html += "  console.error('No data in file:', data);";
          html += "  document.write('<p>No data in file</p>');";
          html += "}";
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
    server.sendHeader("Location", "/");
    server.send(302);
  }
}

