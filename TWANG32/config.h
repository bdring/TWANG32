/*
	TWANG32 - An ESP32 port of TWANG
	(c) B. Dring 3/2018
	License: Creative Commons 4.0 Attribution - Share Alike	
	
	TWANG was originally created by Critters
	https://github.com/Critters/TWANG
	
	Basic hardware definitions
	
	Light Strip Notes:
	
	Noepixel / WS2812
		- Low Cost
		- You might already have a strip.
		- No Clock Line - This means a fixed and relatively slow data rate and only shorter strips can be used
		- Poor Dynamic Range - The low end of brightness is basically not visible
	Dotstar (Highly recommended)
	  - Higher Cost
		- Higher speed - Longer strips can be used.
		- Great dynamic range, so lower levels and more colors can be used.
	

*/

#ifndef CONFIG_H
	#define CONFIG_H

#define DATA_PIN        16
#define CLOCK_PIN       17

/* Game is rendered to this and scaled down to your strip.
 This allows level definitions to work on all strip lengths  */
#define VIRTUAL_LED_COUNT 1000  

// what type of LED Strip....uncomment to define only one of these
#define USE_APA102

//#define USE_NEOPIXEL

// Check to make sure LED choice was done right
#if !defined(USE_NEOPIXEL) && !defined(USE_APA102)
	#error "You must have USE_APA102 or USE_NEOPIXEL defined in config.h"
#endif

#if defined(USE_NEOPIXEL) && defined(USE_APA102)
	#error "Both USE_APA102 and USE_NEOPIXEL are defined in config.h. Only one can be used"
#endif

#ifdef USE_APA102
  #define LED_TYPE        			APA102
  #define LED_COLOR_ORDER      	BGR // typically this will be the order, but switch it if not
  #define CONVEYOR_BRIGHTNESS 	8
  #define LAVA_OFF_BRIGHTNESS 	4
	#define MAX_LEDS   						VIRTUAL_LED_COUNT // these LEDS can handle the max
	#define MIN_REDRAW_INTERVAL  	1000.0 / 60.0    // divide by frames per second..if you tweak, adjust player speed
#endif

#ifdef USE_NEOPIXEL
  #define LED_TYPE        		 	NEOPIXEL
	#define CONVEYOR_BRIGHTNESS 	40  // low neopixel values are nearly off, Neopixels need a higher value
	#define LAVA_OFF_BRIGHTNESS  	15   // low neopixel values are nearly off, Neopixels need a higher value
	#define MAX_LEDS 						 	288  // Neopixels cannot handle the framerate
	#define MIN_REDRAW_INTERVAL  	1000.0 / 60.0    // divide by frames per second..if you tweak adjust player speed
#endif

// Comment or remove the next #define to disable the /metrics endpoint on the HTTP server.
// This endpoint provides the Twang32 stats for ingestion via Prometheus.
#define ENABLE_PROMETHEUS_METRICS_ENDPOINT

#endif
