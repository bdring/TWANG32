# TWANG32
An ESP32 based, 1D, LED strip, dungeon crawler. inspired by Line Wobbler by Robin B

This version compiles with ESP32-Arduino 3.x.

This was ported from the [TWANG fork](https://github.com/bdring/TWANG) by bdring of [Buildlog.net Blog](http://www.buildlog.net/blog?s=twang)

[![Youtube Video](http://www.buildlog.net/blog/wp-content/uploads/2018/05/vid_thumb.png)](https://www.youtube.com/watch?v=RXpfa-ZvUMA)

![TWANG LED Game](http://www.buildlog.net/blog/wp-content/uploads/2018/01/20180111_130909-1.jpg?s=200)

## Why ESP32?
- Lower Cost than Arduino Mega
- Faster Processor
- More Memory
- Smaller (allows a smaller enclosure that is easier to print and more portable)
- DAC pins for better sound capabilities.
- WiFi and Bluetooth.

**Current State**

- All of the Arduino version game features are functional.
- The game now has a WiFi access port to get game stats. Connect a smartphone or computer to see them.
  - **SSID:** TWANG_AP
  - **Password:** 12345666
  - **URL:** 192.168.4.1
- You can update these settings over WiFi
  - LED Count
  - LED Brightness
  - Audio Volume
  - Joystick Deadzone (removes drift)
  - Attack Threshold (twang sensitivity)
  - Lives Per Level

![](http://www.buildlog.net/blog/wp-content/uploads/2018/03/20180328_122254.jpg)

## TO DO List:

- Wireless features~~
  - 2 Player features by linking controllers. TBD
=======
-  Settings:
  - Change strip type.
-  Digitized Audio
  - Currently the port uses the same square wave tones of the the Arduino version.
  - I want to convert to digitized high quality sound effects.
  - Possibly mix multiple sounds so things like lava and movement sound good at the same time.
- Better looking mobile web interface (looks more like a web app)

**BTW:** Since you have red this far... If you want to contribute, contact me and I might be able to get you some free or discounted hardware.

## Required libraries:
* [FastLED](http://fastled.io/)
* [RunningMedian](http://playground.arduino.cc/Main/RunningMedian)

## Hardware used:
* ESP32, I typically use the NodeMCU-32S module
* LED light strip. The more the better, maximum of 1000. Tested with 1x & 2x 144/meter, 12x 60/meter and 5m x 114/meter strips. This has been tested with APA102C and NeoPixel type strips. Anything compatible with the FastLED library should work.
* MPU6050 accelerometer
* Spring doorstop, I used [these](http://smile.amazon.com/gp/product/B00J4Y5BU2)
* Speaker and amplifier. I use a PAM8403 module. (ESP32 cannot drive a speaker as loudly as an Arduino)

See [Buildlog.net Blog](http://www.buildlog.net/blog?s=twang) for more details.

Super easy to use kits and ready to play units [are available on Tindie](https://www.tindie.com/products/33366583/twang32-led-strip-game/).

![TWANG 32 Controller](http://www.buildlog.net/blog/wp-content/uploads/2018/03/20180319_080636.jpg)

## Enclosure
[The STL files are here](http://www.buildlog.net/blog/wp-content/uploads/2018/04/twang32_stl.zip).

![TWANG32](http://www.buildlog.net/blog/wp-content/uploads/2018/03/twang32_enclosure.jpg)

## Overview
The following is a quick overview of the code to help you understand and tweak the game to your needs.

The game is played on a 1000 unit line, the position of enemies, the player, lava etc range from 0 to 1000 and the LEDs that represent them are derived using the `getLED()` function. You don't need to worry about this but it's good to know for things like the width of the attack and player max move speed. Regardless of the number of LEDs, everything takes place in this 1000 unit wide line.

**LED SETUP** Defines the quantity of LEDs as well as the data and clock pins used. I've tested several APA102-C strips and the color order sometimes changes from BGR to GBR, if the player is not blue, the exit green and the enemies red, this is the bit you want to change. Brightness should range from 50 to 255, use a lower number if playing at night or wanting to use a smaller power supply. `DIRECTION` can be set to 0 or 1 to flip the game orientation. In `setup()` there is a `FastLED.addLeds()` line, in there you could change it to another brand of LED strip like the cheaper WS2812.

The game also has 3 regular LEDs for life indicators (the player gets 3 lives which reset each time they level up). The pins for these LEDs are stored in `lifeLEDs[]` and are updated in the `updateLives()` function.

**JOYSTICK SETUP** All parameters are commented in the code, you can set it to work in both forward/backward as well as side-to-side mode by changing `JOYSTICK_ORIENTATION`. Adjust the `ATTACK_THRESHOLD` if the "Twanging" is overly sensitive and the `JOYSTICK_DEADZONE` if the player slowly drifts when there is no input (because it's hard to get the MPU6050 dead level).

**WOBBLE ATTACK** Sets the width, duration (ms) of the attack.

**POOLS** These are the object pools for enemies, particles, lava, conveyors etc. You can modify the quantity of any of them if your levels use more or if you want to save some memory, just remember to update the respective counts to avoid errors.

**USE_GRAVITY** 0/1 to set if particles created by the player getting killed should fall towards the start point, the `BEND_POINT` variable can be set to mark the point at which the strip of LEDs goes from being horizontal to vertical. The game is 1000 units wide (regardless of number of LED's) so 500 would be the mid point. If this is confusing just set `USE_GRAVITY` to 0.

## Modifying / Creating levels
Find the `loadLevel()` function, in there you can see a switch statement with the 10 levels I created.
They all call different functions and variables to setup the level. Each one is described below:

**playerPosition;** Where the player starts on the 0 to 1000 line. If not set it defaults to 0. I set it to 200 in the first level so the player can see movement even if the first action they take is to push the joystick left

**spawnEnemy(position, direction, speed, wobble);** (10 enemies max)
* position: 0 to 1000
* direction: 0/1, initial direction of travel
* speed: >=0, speed of the enemy, remember the game is 1000 wide and runs at 60fps. I recommend between 1 and 4
* wobble: 0=regular moving enemy, 1=sine wave enemy, in this case speed sets the width of the wave

**spawnPool[poolNumber].Spawn(position, rate, speed, direction);** (2 spawners max)
* A spawn pool is a point which spawns enemies forever
* position: 0 to 1000
* rate: milliseconds between spawns, 1000 = 1 second
* speed: speed of the enemis it spawns
* direction: 0=towards start, 1=away from start

**spawnLava(startPoint, endPoint, ontime, offtime, offset);** (4 lava pools max)
* startPoint: 0 to 1000
* endPoint: 0 to 1000, combined with startPoint this sets the location and size of the lava
* ontime: How long (ms) the lava is ON for
* offtime: How long the lava is ON for
* offset: How long (ms) after the level starts before the lava turns on, use this to create patterns with multiple lavas
* grow: This specifies the rate of growth. Use 0 for no growth. Reasonable growth is 0.1 to 0.5
* flow: This specifies the rate/direction of flow. Reasonable numbers are 0.2 to 0.8 

**spawnConveyor(startPoint, endPoint, speed);** (2 conveyors max)
* startPoint, endPoint: Same as lava
* speed: The direction and speed of the travel. Negative moves to base and positive moves towards exit. Must be less than +/- max player speed.

**spawnBoss();** (only one, don't edit boss level)
* There are no parameters for a boss, they always spawn in the same place and have 3 lives. Tweak the values of Boss.h to modify
