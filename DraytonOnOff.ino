/// @dir DraytonOnOff
// Adapted from kaku_onoff example
// Listens for RFM messages indicating that the boiler should be turned on / off,
// then switches to OOK mode to send control message using drayton OOK protocol

// Mark Tully
// 3/1/17

#include <JeeLib.h>
#include <util/parity.h>

#define LED_PIN 9
#define CMD_REPEAT_DELAY 5000   // ms
#define DRAYTON_REBROADCAST_TIME (1000L * 60L)  // ms

static bool boilerState = false;   // whether the boiler is on
static unsigned long rebroadcastTime = 0;

// Turn transmitter on or off, but also apply asymmetric correction and account
// for 25 us SPI overhead to end up with the proper on-the-air pulse widths.
// With thanks to JGJ Veken for his help in getting these values right.
static void ookPulse(int on, int off) {
    rf12_onOff(1);
    delayMicroseconds(on + 150);
    rf12_onOff(0);
    delayMicroseconds(off - 200);
}

static void activityLed (byte on) {
#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, !on);
#endif
}

// send buffer holds the bit pattern to send, up to SEND_BUFFER_SIZE bytes
#define SEND_BUFFER_SIZE 10
byte sendBuffer[SEND_BUFFER_SIZE];
byte sendBufferUsed = 0;
byte sendBufferBitPos = 7;

// clears the send buffer
static void clearSendBuffer()
{
  sendBufferUsed = 0;
  sendBufferBitPos = 7;
}

// pushes a single bit into the send buffer
static void pushBit(bool v)
{
  if (sendBufferUsed < SEND_BUFFER_SIZE)
  {
    if (v)
    {
      bitSet(sendBuffer[sendBufferUsed],sendBufferBitPos);
    }
    else
    {
      bitClear(sendBuffer[sendBufferUsed],sendBufferBitPos);
    }
    if (sendBufferBitPos == 0)
    {
      sendBufferBitPos = 7;
      sendBufferUsed += 1;
    }
    else
    {
      sendBufferBitPos -= 1;
    }
  }
  else
  {
    Serial.println("SEND BUFFER FULL");
  }
}

// push manchester encoding 'A' symbol
static void pushA()
{
  pushBit(1);
  pushBit(0);
}

// push manchester encoding 'B' symbol
static void pushB()
{
  pushBit(0);
  pushBit(1);  
}

// push manchester encoding 'C' and 'D' symbols
static void pushCD()
{
  pushBit(1);
  pushBit(1);
  pushBit(0);
  pushBit(0);  
}

// drayton leadin is the single the drayton looks out for to lock onto to read the message
static void pushDraytonLeadIn()
{
  pushA();
  pushA();
  pushA();
  pushA();
  pushA();
  pushA();
}

// drayton lead out is the signal the drayton expects at the end
static void pushDraytonLeadOut()
{
  pushB();
  pushA();
  pushA();
  pushA();
}

// boiler ID is the ID of the thermostat that the boiler is be paired with
// this ID was obtained by capturing the radio signal from an existing paired thermostat
static void pushBoilerID()
{
  // A  B  B  A  B  A  B  A  A  B  A  B  B  B
  pushA();
  pushB();
  pushB();
  pushA();
  pushB();
  pushA();
  pushB();
  pushA();
  pushA();
  pushB();
  pushA();
  pushB();
  pushB();
  pushB();
}

// push the boiler on command
static void pushBoilerOnCmd()
{
  pushB();
  pushB();
  pushA();
  pushB();
  pushB();

  pushB();
  pushB();
  pushB();
  pushA();
  pushA();
}

// push the boiler off cmd
static void pushBoilerOffCmd()
{
  pushB();
  pushB();
  pushB();
  pushB();
  pushB();
  
  pushB();
  pushB();
  pushA();
  pushB();
  pushA();
}

// clears the send buffer and writes a full payload to turn the boiler off
static void pushDraytonBoilerOff()
{
  clearSendBuffer();
  pushDraytonLeadIn();
  pushCD();
  pushBoilerID();  
  pushBoilerOffCmd();
  pushDraytonLeadOut();
}

// clears the send buffer and writes a full payload to turn the boiler on
static void pushDraytonBoilerOn()
{
  clearSendBuffer();
  pushDraytonLeadIn();
  pushCD();
  pushBoilerID();
  pushBoilerOnCmd();
  pushDraytonLeadOut();
}

// to send the buffered command, we first convert it into run length encoded 1s and 0s, then we can turn
// the radio on / off to broadcast the bit pattern
#define MAX_RLE_BUFFER (SEND_BUFFER_SIZE * 8)
static byte rleBuffer[MAX_RLE_BUFFER];
static byte rleBufferUsed = 0;

// sends the buffered command at the baud rate specified
static void sendCmd(int baudRate)
{
  int bitPause = 1000000 / baudRate;    // microseconds to pause for for each bit
  cmdToRle();
  //Serial.print("Sending RLE len ");
  //Serial.println(rleBufferUsed);
  if (rleBufferUsed & 1)
  {
    Serial.println("\nRLE LENGTH ERROR - SHOULD BE EVEN\n");
  }
  
  for (int i = 0; i < rleBufferUsed; i+=2)
  {
    int onTime = rleBuffer[i] * bitPause;
    int offTime = rleBuffer[i+1] * bitPause;
    /*
    Serial.print("pulse ");
    Serial.print(onTime);
    Serial.print(" ");
    Serial.print(offTime);
    Serial.print("\n");*/
    ookPulse(onTime,offTime);
  }
}

// helper to convert bits to an rle sequence
static void storeRleCmd(int curCmd)
{
  // rle run broken
  if (rleBufferUsed < MAX_RLE_BUFFER)
  {
    rleBuffer[rleBufferUsed] = curCmd;
    rleBufferUsed += 1;
  }
  else
  {
    Serial.println("ERROR: RLE BUFFER FULL");
  }
}

// converts a buffered sequence of bits to an rle sequence
static void cmdToRle()
{
  byte bufByteIdx = 0;
  int curRun = -1;     // -1 uninitialised, 1 true bit, 0 false bit
  int curCount = 0;
  
  rleBufferUsed = 0;

  while (bufByteIdx <= sendBufferUsed)
  {
    byte numBits = (bufByteIdx == sendBufferUsed) ? (7 - sendBufferBitPos) : 8;
    byte bitIdx = 7;

    while (numBits > 0)
    {
      int b = bitRead(sendBuffer[bufByteIdx], bitIdx);
      bitIdx -= 1;
      numBits -= 1;

      if (curRun == -1)
      {
        curRun = b;
        if (b != 1)
        {
          Serial.println("ERROR: Cmd should start with 1 bit");
        }
      }
      else if (curRun != b)
      {
        storeRleCmd(curCount);
        curRun = b;
        curCount = 0;
      }

      curCount += 1;

      /*Serial.print("Send ");
      Serial.print(b);
      Serial.print("\n");*/
    }

    bufByteIdx += 1;
  }

  if (curRun != -1)
  {
    if (curRun != 0)
    {
      Serial.println("ERROR: Cmd should end with 0 bit");
    }
    storeRleCmd(curCount);
  }
}


/*    
    rf12_initialize(0, RF12_433MHZ);

    pushDraytonBoilerOn();
    Serial.println("Sending boiler ON");
    sendCmd(2000);

    Serial.println("Pause for 30 sec");
    delay(30*1000);

    Serial.println("Sending boiler OFF");
    pushDraytonBoilerOff();
    sendCmd(2000);
*/

void sendBoilerState() {

  // switch to OOK mode
  rf12_initialize(0, RF12_433MHZ);
  
  if (boilerState) {
    pushDraytonBoilerOn();
  } else {
    pushDraytonBoilerOff();
  }

  sendCmd(2000);

  activityLed(boilerState);

  // switch back to standard RFM mode
  rf12_config(0); 
}

void setup() {
    Serial.begin(57600);
    Serial.println("\n[drayton controller]");

    // boot the RFM into standard RFM protocol mode
    // it will use the group and node ID configured in the EEPROM, use the RFM12 demo sketch to change it

    // when it is time to send a boiler OOK message, the RFM module will be reconfigured into OOK mode, during this time
    // it won't be able to receive any protocol messages. it will switch back once the message is sent.
    if (!rf12_config(1)) {
      Serial.println("No node ID / group ID settings configured, use RFM12 demo sketch to set them");
    }
}

void loop() {
  if (rf12_recvDone()) {
    byte n = rf12_len;
    if (rf12_crc == 0) {
      // received a packet for this node
      Serial.print("Received message ");
      Serial.print(n);
      Serial.print(" bytes\n");

      // process message
      if (n == 1) {
        switch (rf12_data[0]) {
          case 0:
          case 1: {
            bool newBoilerState = rf12_data[0];
            Serial.print("Received boiler cmd ");
            if (newBoilerState) {
              Serial.println("ON");
            } else {
              Serial.println("OFF");
            }
            boilerState = newBoilerState;
            sendBoilerState();
            rebroadcastTime = millis() + CMD_REPEAT_DELAY;  // send again in a short while to make sure it got through
            Serial.println("Cmd has been sent to boiler");
          }
          break;

          default:
            Serial.print("Unknown cmd :");
            Serial.println(rf12_data[0]);
            break;
        }
      } else {
        Serial.print("Unexpected cmd length :");
        Serial.println(n);
      }
    }
  }
  if ((rebroadcastTime != 0) && (millis() > rebroadcastTime)) {
    Serial.println("Resending boiler state");
    sendBoilerState();
    if (boilerState == true) {
      rebroadcastTime = millis() + DRAYTON_REBROADCAST_TIME;    // rebroadcast periodically, otherwise the boiler will auto switch off
    } else {
      rebroadcastTime = 0;    // no further rebroadcasting needed to maintain off state
    }    
  }
}
