#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Audio.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>

// ==========================================
// 📌 पिन कॉन्फिगरेशन (Hardware Pins)
// ==========================================
#define I2S_MIC_SD   32
#define I2S_MIC_SCK  33
#define I2S_MIC_WS   27
const int audioOutPin = 25; // PAM8403 च्या 'L' ला जोडण्यासाठी

// ==========================================
// 🔐 युजर क्रेडेंशियल्स
// ==========================================
const char* ssid = "vivo Y19 5G";
const char* password = "Nerkar@2008";
const char* gemini_key = "AQ.Ab8RN6KFU0XjUsnO3u6lzHY4N2sECyOmHxiwkb_zS7AWuxGapQ";

// ऑब्जेक्ट्स
Audio audio;
WebServer server(80);
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ड्युअल-कोर सुरक्षा (Mutex)
SemaphoreHandle_t dataMutex;
String pendingGeminiQuery = "";
bool isGeminiBusy = false;
bool isSystemActive = false;
unsigned long activeTimerStart = 0;

// ==========================================
// 🔊 हायब्रीड ऑडिओ स्पीक फंक्शन
// ==========================================
void speak(String text) {
    Serial.println("System Voice: " + text);
    if (WiFi.status() == WL_CONNECTED) {
        String url = "http://translate.google.com/translate_tts?ie=UTF-8&tl=en&client=tw-ob&q=";
        String encodedText = text;
        encodedText.replace(" ", "%20");
        url += encodedText;
        audio.connecttohost(url.c_str());
    }
}

// ==========================================
// 📡 रिसीव्हरला ब्रॉडकास्ट
// ==========================================
void sendCommandToReceiver(String cmd) {
    cmd.trim();
    char cmdArray[64];
    memset(cmdArray, 0, sizeof(cmdArray));
    strncpy(cmdArray, cmd.c_str(), sizeof(cmdArray) - 1);
    esp_now_send(broadcastAddress, (uint8_t *) cmdArray, sizeof(cmdArray));
    Serial.println("Broadcast Sent: " + cmd);
}

// ==========================================
// 🎛️ लोकल कमांड फायरवॉल
// ==========================================
bool processLocalCommands(String cmd) {
    cmd.trim();
    if (cmd == "") return false; 
    
    if (cmd.equalsIgnoreCase("Hi SN") || cmd.equalsIgnoreCase("Hay SN") || 
        cmd.equalsIgnoreCase("SN") || cmd.equalsIgnoreCase("Ok SN")) {
        isSystemActive = true;
        activeTimerStart = millis(); 
        speak("Ok Boss");
        return true;
    }

    if (isSystemActive) {
        activeTimerStart = millis(); 
        if (cmd.equalsIgnoreCase("Light ON")) { speak("Ok Boss Light ON"); sendCommandToReceiver("Light ON"); return true; }
        else if (cmd.equalsIgnoreCase("Light off")) { speak("Ok Boss light off"); sendCommandToReceiver("Light off"); return true; }
        else if (cmd.equalsIgnoreCase("fan ON")) { speak("Ok BOSS fan ON"); sendCommandToReceiver("fan ON"); return true; }
        else if (cmd.equalsIgnoreCase("Fan off")) { speak("Ok Boss fan off"); sendCommandToReceiver("Fan off"); return true; }
        else if (cmd.equalsIgnoreCase("Turn on everything")) { speak("Welcome home, Boss. All active."); sendCommandToReceiver("Turn on everything"); return true; }
        else if (cmd.equalsIgnoreCase("Turn off everything")) { speak("Good night, Boss."); sendCommandToReceiver("Turn off everything"); return true; }
        else if (cmd.equalsIgnoreCase("Check Status")) { speak("All systems normal, Boss."); return true; }
    }
    return false; 
}

// ==========================================
// 🌐 पूर्ण वेब सर्व्हर (UI Buttons)
// ==========================================
void handleRoot() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:sans-serif; text-align:center; background:#1e1e1e; color:#fff;}";
    html += ".btn{display:block; width:90%; padding:15px; margin:10px auto; font-size:18px; font-weight:bold; border:none; border-radius:10px; cursor:pointer;}";
    html += ".on{background:#2ecc71; color:#fff;} .off{background:#e74c3c; color:#fff;} .master{background:#3498db; color:#fff;}</style></head>";
    html += "<body><h1>SN Smart Hub</h1><p>Master Control Panel</p>";
    
    html += "<button class='btn master' onclick=\"fetch('/cmd?val=Turn on everything')\">ALL DEVICES ON</button>";
    html += "<button class='btn master' onclick=\"fetch('/cmd?val=Turn off everything')\">ALL DEVICES OFF</button>";
    
    html += "<h3>Lights</h3>";
    html += "<button class='btn on' onclick=\"fetch('/cmd?val=Light ON')\">Light ON</button>";
    html += "<button class='btn off' onclick=\"fetch('/cmd?val=Light off')\">Light OFF</button>";
    
    html += "<h3>Fans</h3>";
    html += "<button class='btn on' onclick=\"fetch('/cmd?val=fan ON')\">Fan ON</button>";
    html += "<button class='btn off' onclick=\"fetch('/cmd?val=Fan off')\">Fan OFF</button>";
    
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleCommand() {
    String val = server.arg("val");
    bool tempState = isSystemActive;
    isSystemActive = true; 
    processLocalCommands(val); 
    isSystemActive = tempState;
    server.send(200, "text/plain", "Command Executed: " + val);
}

String sampleMicAndRecognize() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n'); 
        input.trim(); 
        return input;
    }
    return "";
}

// ==========================================
// 🤖 जेमिनी API (Core 0)
// ==========================================
void askGemini(String question) {
    WiFiClientSecure client;
    client.setInsecure(); 
    
    if (client.connect("generativelanguage.googleapis.com", 443)) {
        String url = "/v1beta/models/gemini-1.5-flash:generateContent?key=" + String(gemini_key);
        JsonDocument doc;
        doc["contents"][0]["parts"][0]["text"] = question + " (Answer shortly in English under 130 characters)";
        String json;
        serializeJson(doc, json);

        client.println("POST " + url + " HTTP/1.1");
        client.println("Host: generativelanguage.googleapis.com");
        client.println("Content-Type: application/json");
        client.print("Content-Length: ");
        client.println(json.length());
        client.println();
        client.println(json);

        if (client.find("\r\n\r\n")) {
            JsonDocument responseDoc; 
            if (!deserializeJson(responseDoc, client)) {
                const char* reply = responseDoc["candidates"][0]["content"]["parts"][0]["text"];
                if (reply) { speak(String(reply)); }
            }
        }
    } else {
        speak("Server unreachable.");
    }
    client.stop(); 
}

// ==========================================
// 🧠 Core 0 Task: इंटरनेट आणि सर्व्हर
// ==========================================
void networkTask(void * pvParameters) {
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect();
            WiFi.begin(ssid, password);
            vTaskDelay(5000 / portTICK_PERIOD_MS); // कनेक्ट होण्याची वाट पाहणे
        }
        
        server.handleClient(); // वेब ॲप चालवणे

        // जर जेमिनीला प्रश्न विचारला असेल तर
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        String currentQuery = pendingGeminiQuery;
        pendingGeminiQuery = "";
        xSemaphoreGive(dataMutex);

        if (currentQuery != "") {
            isGeminiBusy = true;
            askGemini(currentQuery);
            isGeminiBusy = false;
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS); // Watchdog Crash टाळण्यासाठी अत्यंत आवश्यक
    }
}

// ==========================================
// 🚀 मुख्य सेटअप
// ==========================================
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
    Serial.begin(115200);
    dataMutex = xSemaphoreCreateMutex();

    // ऑडिओ पिन 25 (Internal DAC) वर सेट करणे
    audio.setPinout(26, audioOutPin, 26); 
    audio.setVolume(21); 

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    if (esp_now_init() == ESP_OK) {
        esp_now_peer_info_t peerInfo = {};
        peerInfo.channel = 1;  
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    server.on("/", handleRoot);
    server.on("/cmd", handleCommand);
    server.begin();

    // Core 0 वर नेटवर्क टास्क सुरू करणे
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", 10000, NULL, 1, NULL, 0);
}

// ==========================================
// 🔄 मुख्य लूप (Core 1: ऑडिओ आणि माईक)
// ==========================================
void loop() {
    audio.loop(); 

    if (isSystemActive && !audio.isRunning() && !isGeminiBusy) {
        if (millis() - activeTimerStart > 6000) {
            isSystemActive = false;
            speak("System locked.");
        }
    }

    String voiceCmd = sampleMicAndRecognize();
    if (voiceCmd != "") {
        bool isLocal = processLocalCommands(voiceCmd);
        
        if (!isLocal && isSystemActive) {
            xSemaphoreTake(dataMutex, portMAX_DELAY);
            if (pendingGeminiQuery == "") {
                pendingGeminiQuery = voiceCmd; 
            }
            xSemaphoreGive(dataMutex);
            activeTimerStart = millis(); 
        }
    }
    
    vTaskDelay(1 / portTICK_PERIOD_MS); // Core 1 ला शांत ठेवण्यासाठी
}

