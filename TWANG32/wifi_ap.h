#include <WiFi.h>
#include "settings.h"

const char* ssid     = "TWANG_AP";
const char* passphrase = "12345666";

WiFiServer server(80);

char linebuf[80];
int charcount=0;

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
    server.begin();
	
    Serial.print("\r\nWiFi SSID: ");
	Serial.println(ssid);
	Serial.print("WiFi Password: ");
	Serial.println(passphrase);
	Serial.println("Web Server Address: http://192.168.4.1");
	
	
}

void sendStatsPage(WiFiClient client) {
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
	
	client.print("<button onClick=\"location.href = 'http://192.168.4.1'\">Refresh</button>");
  
	client.print("<h2>Adjustable Settings </h2>");
	
	client.print("<table>");
	
	
	client.print("<tr><td>");	
	client.print("LED Count (60-");
  client.print(MAX_LEDS); 
  client.print(")</td><td><form><input type='number' name='C' value='"); 	
	client.print(user_settings.led_count);
	client.print ("' min='60' max='");
	client.print (MAX_LEDS);
	client.print ("'><input type='submit'></form></td></tr>");
	
	client.print("<tr><td>Brightness (10-255)</td><td><form><input type='number' name='B' value='"); 
	client.print(user_settings.led_brightness);
	client.print ("' min='10' max='255'><input type='submit'></form></td></tr>");
		
	client.print("<tr><td>Sound Volume (0-255)</td><td><form><input type='number' name='S' value='");
	client.print(user_settings.audio_volume);
	client.print("' min='0' max='255'><input type='submit'></form></td></tr>");
	
	client.print("<tr><td>Joystick Deadzone (3-12)</td><td><form><input type='number' name='D' value='");
	client.print(user_settings.joystick_deadzone);
	client.print("' min='3' max='12'><input type='submit'></form></td></tr>");
	
	client.print("<tr><td>Attack Sensitivity (20000-35000)</td><td><form><input type='number' name='A' value='");
	client.print(user_settings.attack_threshold);
	client.print("' min='2000' max='35000'><input type='submit'></form></td></tr>");
	
	client.print("<tr><td>Lives Per Level (3-9)</td><td><form><input type='number' name='L' value='");
	client.print(user_settings.lives_per_level);
	client.print("' min='3' max='9'><input type='submit'></form></td></tr>");
	
  client.print("</table>");
	
  client.println("</body>");
  client.println("</html>");  
  client.println();
  
}

void ap_client_check(){
  int cnt;
  bool newconn=false;
  int stat;
  WiFiClient client = server.available();   // listen for incoming clients

  //if (client) {                             // if you get a client,
  //   sendStatsPage(client);
  //   Serial.println("printUploadForm");
  //}
	bool currentLineIsBlank = true;
	
	while (client.connected()) {
      if (client.available()) {
        char c = client.read();
				//Serial.write(c);
				linebuf[charcount]=c;
				if (charcount<sizeof(linebuf)-1) 
					charcount++;
				
				if (c == '\n' && currentLineIsBlank) {
					sendStatsPage(client);
					break;					
				}
				if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
					
					
					if (strstr(linebuf,"GET /?") > 0)
					{						
					  String line = String(linebuf);		

						int start = line.indexOf('=', 0);

						char paramCode = line.charAt(start - 1);
						
						int finish = line.indexOf('H', start+1)-1;
						String val = line.substring(start+1, finish);		
						// if it is not numeric, it will convert to 0. 
						// The the change_setting function will make sure the range is OK
						
						change_setting(paramCode, val.toInt());
						
						
					}
					
					// you're starting a new line
					currentLineIsBlank = true;
					memset(linebuf,0,sizeof(linebuf));
					charcount=0;
				} else if (c != '\r') {
					// you've gotten a character on the current line
					currentLineIsBlank = false;
				}
				
			}
	}
  
}
