#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <vector>

// Forward declarations
String getMainPage();
String getNetworksJSON();
String getNetworkDetailsJSON(String ssid);
String getRSSIHistoryJSON(String ssid);
String getOpenNetworkInfo(String ssid);
void setupRoutes();
void scanNetworks();
bool connectToOpenNetwork(String ssid);

// WiFi Configuration
const char* ssid = "Enter your SSID";
const char* password = "Enter your password";

WebServer server(80);

// WiFi Network Structure
struct WiFiNetwork {
  String ssid;
  String bssid;
  int32_t rssi;
  uint8_t channel;
  wifi_auth_mode_t encryption;
  unsigned long lastSeen;
  String gatewayIP;
  String subnetMask;
  bool isConnectable;
};

std::vector<WiFiNetwork> networks;
std::vector<std::vector<int>> rssiHistory(50); // Store RSSI history for up to 50 networks
std::vector<String> timeLabels;
unsigned long lastScan = 0;
const unsigned long SCAN_INTERVAL = 3000; // Scan every 3 seconds
int selectedNetworkIndex = -1;

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  
  // Set WiFi to station mode for scanning
  WiFi.mode(WIFI_STA);
  
  // Setup web server routes
  setupRoutes();
  
  server.begin();
  Serial.println("Web server started");
  
  // Initial scan
  scanNetworks();
}

void loop() {
  server.handleClient();
  
  // Periodic network scanning
  if (millis() - lastScan > SCAN_INTERVAL) {
    scanNetworks();
    lastScan = millis();
  }
}

void setupRoutes() {
  // Main page
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getMainPage());
  });
  
  // API endpoint for network list
  server.on("/api/networks", HTTP_GET, []() {
    server.send(200, "application/json", getNetworksJSON());
  });
  
  // API endpoint for detailed network info
  server.on("/api/network-details", HTTP_GET, []() {
    String ssidParam = server.arg("ssid");
    server.send(200, "application/json", getNetworkDetailsJSON(ssidParam));
  });
  
  // API endpoint for RSSI history
  server.on("/api/rssi-history", HTTP_GET, []() {
    String ssidParam = server.arg("ssid");
    server.send(200, "application/json", getRSSIHistoryJSON(ssidParam));
  });
  
  // API endpoint for open network connection
  server.on("/api/connect-open", HTTP_GET, []() {
    String ssidParam = server.arg("ssid");
    server.send(200, "application/json", getOpenNetworkInfo(ssidParam));
  });
}

void scanNetworks() {
  int n = WiFi.scanNetworks(false, true); // Scan with hidden networks
  
  networks.clear();
  
  // Add timestamp for signal tracking
  String currentTime = String(millis() / 1000) + "s";
  timeLabels.push_back(currentTime);
  if (timeLabels.size() > 50) {
    timeLabels.erase(timeLabels.begin());
  }
  
  for (int i = 0; i < n && i < 50; i++) {
    WiFiNetwork net;
    net.ssid = WiFi.SSID(i);
    net.bssid = WiFi.BSSIDstr(i);
    net.rssi = WiFi.RSSI(i);
    net.channel = WiFi.channel(i);
    net.encryption = WiFi.encryptionType(i);
    net.lastSeen = millis();
    net.isConnectable = (net.encryption == WIFI_AUTH_OPEN);
    net.gatewayIP = "";
    net.subnetMask = "";
    
    networks.push_back(net);
    
    // Store RSSI history
    if (rssiHistory.size() <= i) {
      rssiHistory.resize(i + 1);
    }
    rssiHistory[i].push_back(net.rssi);
    if (rssiHistory[i].size() > 50) {
      rssiHistory[i].erase(rssiHistory[i].begin());
    }
  }
  
  WiFi.scanDelete(); // Free memory
}

bool connectToOpenNetwork(String targetSSID) {
  // Disconnect from current network
  WiFi.disconnect();
  delay(1000);
  
  // Try to connect to open network
  WiFi.begin(targetSSID.c_str());
  Serial.print("Attempting to connect to open network: ");
  Serial.println(targetSSID);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to open network!");
    return true;
  } else {
    Serial.println("Failed to connect to open network");
    // Reconnect to original network
    WiFi.begin(ssid, password);
    return false;
  }
}

String getOpenNetworkInfo(String targetSSID) {
  DynamicJsonDocument doc(1024);
  
  // Find the network in our scan results
  for (auto& network : networks) {
    if (network.ssid == targetSSID && network.encryption == WIFI_AUTH_OPEN) {
      if (connectToOpenNetwork(targetSSID)) {
        doc["connected"] = true;
        doc["ssid"] = targetSSID;
        doc["localIP"] = WiFi.localIP().toString();
        doc["gatewayIP"] = WiFi.gatewayIP().toString();
        doc["subnetMask"] = WiFi.subnetMask().toString();
        doc["dnsIP"] = WiFi.dnsIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["macAddress"] = WiFi.macAddress();
        
        // Reconnect to original network after getting info
        delay(2000);
        WiFi.disconnect();
        delay(1000);
        WiFi.begin(ssid, password);
      } else {
        doc["connected"] = false;
        doc["error"] = "Failed to connect to open network";
      }
      break;
    }
  }
  
  if (!doc.containsKey("connected")) {
    doc["connected"] = false;
    doc["error"] = "Network not found or not open";
  }
  
  String output;
  serializeJson(doc, output);
  return output;
}

String getNetworksJSON() {
  DynamicJsonDocument doc(8192);
  JsonArray networkArray = doc.to<JsonArray>();
  
  for (const auto& network : networks) {
    JsonObject obj = networkArray.createNestedObject();
    obj["ssid"] = network.ssid;
    obj["bssid"] = network.bssid;
    obj["rssi"] = network.rssi;
    obj["channel"] = network.channel;
    obj["encryption"] = (int)network.encryption;
    obj["lastSeen"] = network.lastSeen;
    obj["isConnectable"] = network.isConnectable;
  }
  
  String output;
  serializeJson(doc, output);
  return output;
}

String getNetworkDetailsJSON(String ssid) {
  for (const auto& network : networks) {
    if (network.ssid == ssid) {
      DynamicJsonDocument doc(1024);
      doc["ssid"] = network.ssid;
      doc["bssid"] = network.bssid;
      doc["rssi"] = network.rssi;
      doc["channel"] = network.channel;
      doc["encryption"] = (int)network.encryption;
      doc["lastSeen"] = network.lastSeen;
      doc["isConnectable"] = network.isConnectable;
      
      String output;
      serializeJson(doc, output);
      return output;
    }
  }
  return "{}";
}

String getRSSIHistoryJSON(String ssid) {
  // Find network index
  int networkIndex = -1;
  for (int i = 0; i < networks.size(); i++) {
    if (networks[i].ssid == ssid) {
      networkIndex = i;
      break;
    }
  }
  
  if (networkIndex == -1 || networkIndex >= rssiHistory.size() || rssiHistory[networkIndex].empty()) {
    return "[]";
  }
  
  DynamicJsonDocument doc(4096);
  JsonObject result = doc.to<JsonObject>();
  
  JsonArray historyArray = result.createNestedArray("data");
  for (int rssi : rssiHistory[networkIndex]) {
    historyArray.add(rssi);
  }
  
  JsonArray labelsArray = result.createNestedArray("labels");
  int startIndex = max(0, (int)timeLabels.size() - (int)rssiHistory[networkIndex].size());
  for (int i = startIndex; i < timeLabels.size(); i++) {
    labelsArray.add(timeLabels[i]);
  }
  
  String output;
  serializeJson(doc, output);
  return output;
}

String getMainPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 WiFi Scanner Pro</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 1600px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
        }
        
        h1 {
            text-align: center;
            color: #2c3e50;
            margin-bottom: 30px;
            font-size: 2.5em;
            background: linear-gradient(45deg, #667eea, #764ba2);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        
        .grid {
            display: grid;
            grid-template-columns: 1fr 2fr;
            gap: 30px;
            margin-bottom: 30px;
        }
        
        .network-list {
            background: white;
            border-radius: 15px;
            padding: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.1);
            max-height: 700px;
            overflow-y: auto;
        }
        
        .network-item {
            padding: 15px;
            margin: 10px 0;
            border-radius: 12px;
            cursor: pointer;
            transition: all 0.3s ease;
            border-left: 5px solid transparent;
            position: relative;
        }
        
        .network-item:hover {
            background: linear-gradient(45deg, #f8f9fa, #e9ecef);
            transform: translateX(5px);
            box-shadow: 0 8px 25px rgba(0,0,0,0.1);
        }
        
        .network-item.selected {
            background: linear-gradient(45deg, #667eea, #764ba2);
            color: white;
            border-left-color: #ffd700;
        }
        
        .network-item.open-network {
            border-right: 4px solid #28a745;
        }
        
        .network-item.open-network::after {
            content: "🔓 OPEN";
            position: absolute;
            top: 10px;
            right: 10px;
            background: #28a745;
            color: white;
            padding: 2px 6px;
            border-radius: 4px;
            font-size: 0.7em;
            font-weight: bold;
        }
        
        .network-name {
            font-weight: bold;
            font-size: 1.1em;
            margin-bottom: 5px;
        }
        
        .network-info {
            font-size: 0.9em;
            opacity: 0.8;
        }
        
        .signal-bar {
            width: 100%;
            height: 10px;
            background: rgba(255,255,255,0.3);
            border-radius: 5px;
            margin: 10px 0;
            overflow: hidden;
        }
        
        .signal-fill {
            height: 100%;
            border-radius: 5px;
            transition: width 0.3s ease;
        }
        
        .charts-container {
            background: white;
            border-radius: 15px;
            padding: 25px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.1);
        }
        
        .chart-section {
            margin-bottom: 35px;
        }
        
        .chart-title {
            color: #2c3e50;
            margin-bottom: 20px;
            font-size: 1.4em;
            font-weight: bold;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        
        .chart-container {
            height: 350px;
            position: relative;
        }
        
        .heatmap-container {
            height: 300px;
            position: relative;
        }
        
        .network-details {
            background: white;
            border-radius: 15px;
            padding: 25px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.1);
            margin-top: 30px;
        }
        
        .detail-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        
        .detail-card {
            background: linear-gradient(135deg, #f8f9fa, #e9ecef);
            padding: 25px;
            border-radius: 15px;
            text-align: center;
            transition: transform 0.3s ease;
        }
        
        .detail-card:hover {
            transform: translateY(-3px);
        }
        
        .detail-value {
            font-size: 2.2em;
            font-weight: bold;
            color: #667eea;
            margin-bottom: 8px;
        }
        
        .detail-label {
            color: #6c757d;
            font-size: 0.9em;
            font-weight: 500;
        }
        
        .connect-btn {
            background: linear-gradient(45deg, #28a745, #20c997);
            color: white;
            border: none;
            padding: 12px 25px;
            border-radius: 8px;
            cursor: pointer;
            font-weight: bold;
            transition: all 0.3s ease;
            margin-top: 15px;
        }
        
        .connect-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(40, 167, 69, 0.3);
        }
        
        .connect-btn:disabled {
            background: #6c757d;
            cursor: not-allowed;
            transform: none;
        }
        
        .ip-info {
            background: linear-gradient(135deg, #e3f2fd, #bbdefb);
            padding: 20px;
            border-radius: 12px;
            margin-top: 20px;
            border-left: 5px solid #2196f3;
        }
        
        .loading {
            text-align: center;
            padding: 20px;
            color: #6c757d;
        }
        
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        
        .loading::after {
            content: '...';
            animation: pulse 1.5s infinite;
        }

        @media (max-width: 768px) {
            .grid {
                grid-template-columns: 1fr;
            }
            
            h1 {
                font-size: 2em;
            }
            
            .container {
                padding: 15px;
            }
            
            .chart-container {
                height: 250px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🛜 ESP32 WiFi Scanner Pro</h1>
        
        <div class="grid">
            <div class="network-list">
                <h3>📡 Available Networks</h3>
                <div id="networkList" class="loading">Scanning networks</div>
            </div>
            
            <div class="charts-container">
                <div class="chart-section">
                    <div class="chart-title">
                        📈 Signal Strength Timeline
                    </div>
                    <div class="chart-container">
                        <canvas id="signalChart"></canvas>
                    </div>
                </div>
                
                <div class="chart-section">
                    <div class="chart-title">
                        🗺️ Network Density Heatmap
                    </div>
                    <div class="heatmap-container">
                        <canvas id="heatmapChart"></canvas>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="network-details" id="networkDetails" style="display: none;">
            <h3>📡 Network Analysis</h3>
            <div id="detailsContent" class="detail-grid"></div>
            <div id="ipInfo" class="ip-info" style="display: none;"></div>
        </div>
    </div>

    <script>
        let networks = [];
        let selectedSSID = null;
        let signalChart = null;
        let heatmapChart = null;
        
        const signalCtx = document.getElementById('signalChart').getContext('2d');
        const heatmapCtx = document.getElementById('heatmapChart').getContext('2d');
        
        function initCharts() {
            // NYC Borough-style Signal Strength Chart (Area Chart)
            signalChart = new Chart(signalCtx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Signal Strength (dBm)',
                        data: [],
                        borderColor: '#667eea',
                        backgroundColor: 'rgba(102, 126, 234, 0.2)',
                        fill: true,
                        tension: 0.4,
                        pointRadius: 3,
                        pointHoverRadius: 6,
                        pointBackgroundColor: '#667eea',
                        pointBorderColor: 'white',
                        pointBorderWidth: 2
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    plugins: {
                        legend: { 
                            display: true,
                            position: 'top'
                        }
                    },
                    scales: {
                        y: {
                            beginAtZero: false,
                            min: -100,
                            max: -20,
                            grid: {
                                color: 'rgba(0,0,0,0.1)'
                            },
                            ticks: {
                                callback: function(value) {
                                    return value + ' dBm';
                                }
                            }
                        },
                        x: {
                            grid: {
                                color: 'rgba(0,0,0,0.1)'
                            }
                        }
                    }
                }
            });
            
            // NYC Borough-style Heatmap
            heatmapChart = new Chart(heatmapCtx, {
                type: 'bar',
                data: {
                    labels: ['Channel 1-3', 'Channel 4-8', 'Channel 9-13'],
                    datasets: [{
                        label: 'Network Count',
                        data: [0, 0, 0],
                        backgroundColor: [
                            'rgba(255, 87, 34, 0.8)',  // Deep Orange
                            'rgba(255, 152, 0, 0.8)',  // Orange  
                            'rgba(255, 193, 7, 0.8)'   // Amber
                        ],
                        borderColor: [
                            'rgb(255, 87, 34)',
                            'rgb(255, 152, 0)', 
                            'rgb(255, 193, 7)'
                        ],
                        borderWidth: 2,
                        borderRadius: 8
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    plugins: {
                        legend: { display: false }
                    },
                    scales: {
                        y: {
                            beginAtZero: true,
                            grid: {
                                color: 'rgba(0,0,0,0.1)'
                            }
                        },
                        x: {
                            grid: {
                                display: false
                            }
                        }
                    }
                }
            });
        }
        
        function getSignalQuality(rssi) {
            if (rssi > -30) return { quality: 'Amazing', color: '#28a745', percentage: 100 };
            if (rssi > -50) return { quality: 'Excellent', color: '#20c997', percentage: 90 };
            if (rssi > -60) return { quality: 'Good', color: '#ffc107', percentage: 75 };
            if (rssi > -70) return { quality: 'Fair', color: '#fd7e14', percentage: 55 };
            if (rssi > -80) return { quality: 'Weak', color: '#dc3545', percentage: 35 };
            return { quality: 'Very Weak', color: '#6c757d', percentage: 15 };
        }
        
        function getEncryptionText(encType) {
            const types = {
                0: '🔓 Open',
                2: '🔒 WPA',
                3: '🔒 WPA2',
                4: '🔒 WPA/WPA2',
                5: '🔐 WPA2 Enterprise',
                7: '🔐 WPA3'
            };
            return types[encType] || '🔒 Secured';
        }
        
        async function fetchNetworks() {
            try {
                const response = await fetch('/api/networks');
                networks = await response.json();
                updateNetworkList();
                updateHeatmap();
            } catch (error) {
                console.error('Error fetching networks:', error);
            }
        }
        
        function updateNetworkList() {
            const listElement = document.getElementById('networkList');
            
            if (networks.length === 0) {
                listElement.innerHTML = '<div class="loading">No networks found</div>';
                return;
            }
            
            listElement.innerHTML = networks.map((network, index) => {
                const signal = getSignalQuality(network.rssi);
                const isOpen = network.encryption === 0;
                
                return `
                    <div class="network-item ${selectedSSID === network.ssid ? 'selected' : ''} ${isOpen ? 'open-network' : ''}" 
                         onclick="selectNetwork('${network.ssid}', ${index})">
                        <div class="network-name">${network.ssid || 'Hidden Network'}</div>
                        <div class="network-info">
                            ${getEncryptionText(network.encryption)} • 
                            Channel ${network.channel} • 
                            ${network.rssi} dBm • 
                            ${signal.quality}
                        </div>
                        <div class="signal-bar">
                            <div class="signal-fill" 
                                 style="width: ${signal.percentage}%; background: ${signal.color}">
                            </div>
                        </div>
                    </div>
                `;
            }).join('');
        }
        
        function updateHeatmap() {
            const channelCounts = [0, 0, 0];
            
            networks.forEach(network => {
                if (network.channel <= 3) channelCounts[0]++;
                else if (network.channel <= 8) channelCounts[1]++;
                else channelCounts[2]++;
            });
            
            heatmapChart.data.datasets[0].data = channelCounts;
            heatmapChart.update('none');
        }
        
        async function selectNetwork(ssid, index) {
            selectedSSID = ssid;
            updateNetworkList();
            
            try {
                // Fetch detailed network info
                const response = await fetch(`/api/network-details?ssid=${encodeURIComponent(ssid)}`);
                const details = await response.json();
                showNetworkDetails(details);
                
                // Fetch RSSI history
                const historyResponse = await fetch(`/api/rssi-history?ssid=${encodeURIComponent(ssid)}`);
                const history = await historyResponse.json();
                updateSignalChart(history);
                
            } catch (error) {
                console.error('Error fetching network details:', error);
            }
        }
        
        function showNetworkDetails(details) {
            const detailsElement = document.getElementById('networkDetails');
            const contentElement = document.getElementById('detailsContent');
            const signal = getSignalQuality(details.rssi);
            const isOpen = details.encryption === 0;
            
            contentElement.innerHTML = `
                <div class="detail-card">
                    <div class="detail-value">${details.rssi}</div>
                    <div class="detail-label">Signal Strength (dBm)</div>
                </div>
                <div class="detail-card">
                    <div class="detail-value">${signal.quality}</div>
                    <div class="detail-label">Signal Quality</div>
                </div>
                <div class="detail-card">
                    <div class="detail-value">${details.channel}</div>
                    <div class="detail-label">WiFi Channel</div>
                </div>
                <div class="detail-card">
                    <div class="detail-value">${getEncryptionText(details.encryption)}</div>
                    <div class="detail-label">Security Type</div>
                </div>
                <div class="detail-card">
                    <div class="detail-value">${details.bssid.substring(0, 17)}</div>
                    <div class="detail-label">MAC Address</div>
                </div>
                ${isOpen ? `
                <div class="detail-card">
                    <button class="connect-btn" onclick="connectToOpenNetwork('${details.ssid}')">
                        🔓 Connect & Get IP
                    </button>
                    <div class="detail-label">Open Network Available</div>
                </div>
                ` : ''}
            `;
            
            detailsElement.style.display = 'block';
        }
        
        async function connectToOpenNetwork(ssid) {
            const btn = event.target;
            btn.disabled = true;
            btn.innerHTML = '⏳ Connecting...';
            
            try {
                const response = await fetch(`/api/connect-open?ssid=${encodeURIComponent(ssid)}`);
                const result = await response.json();
                
                const ipInfoElement = document.getElementById('ipInfo');
                
                if (result.connected) {
                    ipInfoElement.innerHTML = `
                        <h4>🌐 Network Connection Details</h4>
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 15px;">
                            <div><strong>Local IP:</strong> ${result.localIP}</div>
                            <div><strong>Gateway IP:</strong> ${result.gatewayIP}</div>
                            <div><strong>Subnet Mask:</strong> ${result.subnetMask}</div>
                            <div><strong>DNS Server:</strong> ${result.dnsIP}</div>
                            <div><strong>MAC Address:</strong> ${result.macAddress}</div>
                            <div><strong>Signal Strength:</strong> ${result.rssi} dBm</div>
                        </div>
                    `;
                    ipInfoElement.style.display = 'block';
                    btn.innerHTML = '✅ Connected!';
                    btn.style.background = '#28a745';
                } else {
                    ipInfoElement.innerHTML = `
                        <h4>❌ Connection Failed</h4>
                        <p>${result.error}</p>
                    `;
                    ipInfoElement.style.display = 'block';
                    btn.innerHTML = '❌ Failed';
                    btn.style.background = '#dc3545';
                }
            } catch (error) {
                console.error('Error connecting to open network:', error);
                btn.innerHTML = '❌ Error';
                btn.style.background = '#dc3545';
            }
            
            setTimeout(() => {
                btn.disabled = false;
                btn.innerHTML = '🔓 Connect & Get IP';
                btn.style.background = 'linear-gradient(45deg, #28a745, #20c997)';
            }, 5000);
        }
        
        function updateSignalChart(historyData) {
            if (historyData.data && historyData.labels) {
                signalChart.data.labels = historyData.labels;
                signalChart.data.datasets[0].data = historyData.data;
                signalChart.update('none');
            }
        }
        
        // Initialize everything
        window.onload = function() {
            initCharts();
            fetchNetworks();
            
            // Auto-refresh every 4 seconds
            setInterval(fetchNetworks, 4000);
        };
    </script>
</body>
</html>
)rawliteral";
}
