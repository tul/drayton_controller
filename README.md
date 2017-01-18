# Drayton SCR Controller

This is the code that I use to control my Drayton Digistat SCR from a JeeNode via a RFM12B module.

The boiler was originally controlled from a Drayton Digistat RF2 Wireless thermostat. Each thermostat has a unique ID which the SCR unit (the unit that is attached to the boiler) is trained to. I used an [cheap OOK receiver][1] to record the 433Mhz band to phono jack which I plugged into my Mac's line in. I then used Audacity to manually inspect the waveform and laboriously transcode it into a stream of 1s and 0s. I got the idea from [here][2] but there is more than one article on the web about capturing radio signal to sound.

The Drayton message format is described a little bit on [this news thread][3]. It explains how Manchester encoding is used, and also how the message format is broken down into a preamble, the boiler ID, then the control codes to switch it on and off. I found the control codes were slightly different to the ones described in this article however, but it was useful information nonetheless.

Once I had the code, I adapted one of the JeeNode OOK examples to send the signal, and it worked perfectly. When the boiler is on, it expects a re-send every few minutes otherwise the SCR will automatically shut the boiler off again; so this program has re-send logic built in on a timer for when the boiler state is on.

To control the boiler, the node listens on the standard JeeNode RFM protocol waiting for a message directed at it to turn it on / off. When it receives one, it re-initialised the RFM into OOK mode, and sends the command. Once sent, it re-initialises back into the usual FM mode to listen for further control messages.

To get the timing right for sending the message to the boiler, I use a simple run length encoder to count how many ticks the OOK signal should be held high or low in a row. The code is flexible and can send any OOK signal, and I plan to use it to control other OOK devices in the future.

The final system receives its control signals from an emon-pi system, which receives messages over MQTT and forwards them onto its RFM. The boiler scheduler can then listen to temperature sensors which are posting to MQTT, and apply its heating logic to decide when to push a boiler on / off cmd to MQTT.

[1]: http://www.ebay.co.uk/itm/RF-Wireless-Transmitter-and-Receiver-Link-Kit-Module-433Mhz-For-Remote-Control-/350628314207?pt=UK_Gadgets&hash=item51a3137c5f

[2]: http://www.homeautomationhub.com/content/boiler-control-using-urf

[3]: https://groups.google.com/forum/m/#!topic/homecamp/kEiAfm4yhdI
