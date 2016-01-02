# Sous Vide Wifi
An ESP8266 powered Sous Vide cooking controller.

# Parts List

1. Adafruit ESP8266 Huzzah - https://www.adafruit.com/products/2471
  * Adafruit also recently launched a new version of their ESP8266 Huzzah in their new "Feather" form factor.
  If you use this model instead you can skip the FTDI cable, power supply, and 2.1mm jack. You can also hook up a battery.
  https://www.adafruit.com/product/2821
1. Waterproof DS18B20 digital temperature sensor - https://www.adafruit.com/products/642
1. Powerswitch Tail - https://www.adafruit.com/products/268
1. Food-grade heat shrink (optional) - https://www.adafruit.com/products/1020

You will also need, if you don't already have:
* An FTDI cable. I like the Adafruit TFDI friend: https://www.adafruit.com/products/284
* A slow cooker. Read the section about choosing one, or pick up a [Hamilton Beach 33969A](http://amzn.to/1LGNcmh).
* Power supply
* 2.1mm jack

# Choosing a slow cooker

We'll be controlling temperature by turning the slow cooker on and off.
It's important that your slow cooker be able to maintain its state through power cycles.
For that purpose, a slow cooker with analog controls may be best.
However, some slow cookers, like the [Hamilton Beach 33969A](http://amzn.to/1LGNcmh), can maintain their state through a power cycle of up to 30 seconds.

If you're not sure about your own digital slow cooker, try this: Turn it on for a minute at its high power state, then unplug the power for five seconds and plug it back in.
If it comes back on in the same state, then you're good!
You can keep trying this with longer periods of time to see the maximum length it can power cycle.

# Acknowledgement

This project was inspired by Adafruit's [Arduino Sous Vide](https://learn.adafruit.com/sous-vide-powered-by-arduino-the-sous-viduino).
Many thanks to the team at Adafruit for their great [guides and projects](http://learn.adafruit.com/).
