#include <WiFi.h>

const char* ssid     = "TWANG_AP";
const char* passphrase = "esp32rocks";

WiFiServer server(80);

void ap_setup() {
    bool ret;
    
    

  /*
   * Set up an access point
   * @param ssid          Pointer to the SSID (max 63 char).
   * @param passphrase    (for WPA2 min 8 char, for open use NULL)
   * @param channel       WiFi channel number, 1 - 13.
   * @param ssid_hidden   Network cloaking (0 = broadcast SSID, 1 = hide SSID)
   */
    ret = WiFi.softAP(ssid, passphrase, 2, 0);
    
    Serial.println("\r\nWiFi AP online ...");    
    server.begin();
	

}

void sendStatsPage(WiFiClient client) {
  Serial.println("printUploadForm");
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.println("<html>");
  client.println("<body>");
  client.println("<h1>TWANG32 Play Stats</h1>");
  client.println("<ul>");
  
  client.print("<li>Games played: "); client.print(user_settings.games_played); client.println("</li>");
  if (user_settings.games_played > 0) {  // prevent divide by 0
		client.print("<li>Average score: "); client.print(user_settings.total_points / user_settings.games_played); client.println("</li>");
  }
  client.print("<li>High score: "); client.print(user_settings.high_score); client.println("</li>");
  client.print("<li>Boss kills: "); client.print(user_settings.boss_kills); client.println("</li>");
  
  client.println("</ul>");
  client.println("</body>");
  client.println("</html>");  
  client.println();
  
}

void ap_client_check(){
  int cnt;
  bool newconn=false;
  int stat;
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
     sendStatsPage(client);
     Serial.println("printUploadForm");
  }
  
}
