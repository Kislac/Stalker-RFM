//pio device monitor -p COM4 -b 115200
// Sample RFM69 sender/node sketch, with ACK and optional encryption, and Automatic Transmission Control
// Sends periodic messages of increasing length to gateway (id=1)
// It also looks for an onboard FLASH chip, if present
// **********************************************************************************
// Copyright Felix Rusu 2018, http://www.LowPowerLab.com/contact
// **********************************************************************************
// License
// **********************************************************************************
// This program is free software; you can redistribute it 
// and/or modify it under the terms of the GNU General    
// Public License as published by the Free Software       
// Foundation; either version 3 of the License, or        
// (at your option) any later version.                    
//                                                        
// This program is distributed in the hope that it will   
// be useful, but WITHOUT ANY WARRANTY; without even the  
// implied warranty of MERCHANTABILITY or FITNESS FOR A   
// PARTICULAR PURPOSE. See the GNU General Public        
// License for more details.                              
//                                                        
// Licence can be viewed at                               
// http://www.gnu.org/licenses/gpl-3.0.txt
//
// Please maintain this license information along with authorship
// and copyright notices in any redistribution of this code
// **********************************************************************************
#include <RFM69.h>         //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>     //get it here: https://www.github.com/lowpowerlab/rfm69
//#include <SPIFlash.h>      //get it here: https://www.github.com/lowpowerlab/spiflash
#include <RFM69registers.h> // Include- block, needed for recognition of REG_BITRATEMSB / REG_BITRATELSB
#include <Arduino.h>
#include <U8g2lib.h>
#include <U8x8lib.h>
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  


#include <Geiger.h>
#include <esp_task_wdt.h>
//3 seconds WDT
#define WDT_TIMEOUT 3
//*********************************************************************************************
//************ IMPORTANT SETTINGS - YOU MUST CHANGE/CONFIGURE TO FIT YOUR HARDWARE ************
//*********************************************************************************************
// Address IDs are 10bit, meaning usable ID range is 1..1023
// Address 0 is special (broadcast), messages to address 0 are received by all *listening* nodes (ie. active RX mode)
// Gateway ID should be kept at ID=1 for simplicity, although this is not a hard constraint
//*********************************************************************************************
#define NODEID        2    // keep UNIQUE for each node on same network
#define NETWORKID     100  // keep IDENTICAL on all nodes that talk to each other
#define GATEWAYID     1    // "central" node

//*********************************************************************************************
// Frequency should be set to match the radio module hardware tuned frequency,
// otherwise if say a "433mhz" module is set to work at 915, it will work but very badly.
// Moteinos and RF modules from LowPowerLab are marked with a colored dot to help identify their tuned frequency band,
// see this link for details: https://lowpowerlab.com/guide/moteino/transceivers/
// The below examples are predefined "center" frequencies for the radio's tuned "ISM frequency band".
// You can always set the frequency anywhere in the "frequency band", ex. the 915mhz ISM band is 902..928mhz.
//*********************************************************************************************
#define FREQUENCY   RF69_433MHZ
//#define FREQUENCY   RF69_868MHZ
//#define FREQUENCY     RF69_915MHZ
//#define FREQUENCY_EXACT 916000000 // you may define an exact frequency/channel in Hz
#define ENCRYPTKEY    "sampleEncryptKey" //exactly the same 16 characters/bytes on all nodes!
#define IS_RFM69HW_HCW  //uncomment only for RFM69HW/HCW! Leave out if you have RFM69W/CW!
//*********************************************************************************************
//Auto Transmission Control - dials down transmit power to save battery
//Usually you do not need to always transmit at max output power
//By reducing TX power even a little you save a significant amount of battery power
//This setting enables this gateway to work with remote nodes that have ATC enabled to
//dial their power down to only the required level (ATC_RSSI)
//#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
#define ATC_RSSI      -80
//*********************************************************************************************
#define SERIAL_BAUD   115200
#define RESET_PIN 4
#define Button_Blue 25
#define Button_Yellow 26
#define Button_Green 27
#define Button_Red 14


#define buzzerPin 32
#define LEDpin 33

int TRANSMITPERIOD = 500; //transmit a packet to gateway so often (in ms)
char payload[] = "123456789";
char buff[20];
byte sendSize=1;
boolean requestACK = false;
//SPIFlash flash(SS_FLASHMEM, 0xEF30); //EF30 for 4mbit  Windbond chip (W25X40CL)

#ifdef ENABLE_ATC
  RFM69_ATC radio(5,16,true);
#else
  RFM69 radio(5,16,true);
  //RFM69 radio;
#endif

bool spy = false; //set to 'true' to sniff all packets on the same network

void ResetRadio()
{
  digitalWrite(RESET_PIN, HIGH);
  delay(50);
  digitalWrite(RESET_PIN, LOW);
  Serial.println("Radio reseted!");
}

void Blink(byte PIN, int DELAY_MS)
{
  pinMode(PIN, OUTPUT);
  digitalWrite(PIN,HIGH);
  delay(DELAY_MS);
  digitalWrite(PIN,LOW);
}

void DisplayUpdater();
void AliveAnimation();
void ProcessSerialInput();
void RFM_Recive_msg();
void RFM_Send_msg();
void ButtonHandler();
void WatchDogFeeder();


unsigned long previousMillis_DisplayUpdater = 0;
unsigned long previousMillis_LoadingIcon = 0;
uint8_t IconState;
uint32_t sentMSGCounter_Prev;
uint32_t sentMSGCounter;
uint32_t packetCount_Prev;
uint32_t packetCount = 0;
byte ackCount=0;
int CurrentRSSI = 0;

unsigned long previousMillis_WatchDogTimer = 0;
bool WatchDogEnable = true;

int8_t PowerLevel = 23;

char IncomingMsg_char[230];
int IncomingMsg_int[230];
// IncomingMSG_Lenght;
char SentMsg[64];
char IncomingMsg[64];




bool Button_Blue_state = 1;
bool Button_Yellow_state = 1;
bool Button_Green_state = 1;
bool Button_Red_state = 1;
bool Button_Blue_state_Prev = 1;
bool Button_Yellow_state_Prev = 1;
bool Button_Green_state_Prev = 1;
bool Button_Red_state_Prev = 1;

bool SendMsgFlag = true;
bool ReciveMsgFlag = false;


const uint8_t GraphResPixel = 5;
const uint8_t RSSI_LogSize = 25;//126/GraphResPixel;//20
int RSSI_Log[RSSI_LogSize];
uint8_t First_X;
uint8_t Second_X;
int8_t First_RSSI ;
int8_t Second_RSSI;
int8_t First_Y;
int8_t Second_Y;


int8_t BuzzerIntensity= 0;
uint8_t BuzzerVolume = 100;
uint8_t BuzzerPWM = 255;
bool enableBuzzer= false;
unsigned long previousMillis_Buzzer = 0;
int delta = 2000;

int8_t BITRATE_Counter = 6;
uint8_t BITRATE[22][2] ={
{	RF_BITRATELSB_1200	,	RF_BITRATEMSB_1200	}	,
{	RF_BITRATELSB_2400	,	RF_BITRATEMSB_2400	}	,
{	RF_BITRATELSB_4800	,	RF_BITRATEMSB_4800	}	,
{	RF_BITRATELSB_9600	,	RF_BITRATEMSB_9600	}	,
{	RF_BITRATELSB_12500	,	RF_BITRATEMSB_12500	}	,
{	RF_BITRATELSB_19200	,	RF_BITRATEMSB_19200	}	,
{	RF_BITRATELSB_25000	,	RF_BITRATEMSB_25000	}	,
{	RF_BITRATELSB_32768	,	RF_BITRATEMSB_32768	}	,
{	RF_BITRATELSB_34482	,	RF_BITRATEMSB_34482	}	,
{	RF_BITRATELSB_38323	,	RF_BITRATEMSB_38323	}	,
{	RF_BITRATELSB_38400	,	RF_BITRATEMSB_38400	}	,
{	RF_BITRATELSB_50000	,	RF_BITRATEMSB_50000	}	,
{	RF_BITRATELSB_55555	,	RF_BITRATEMSB_55555	}	,
{	RF_BITRATELSB_57600	,	RF_BITRATEMSB_57600	}	,
{	RF_BITRATELSB_76800	,	RF_BITRATEMSB_76800	}	,
{	RF_BITRATELSB_100000	,	RF_BITRATEMSB_100000	}	,
{	RF_BITRATELSB_115200	,	RF_BITRATEMSB_115200	}	,
{	RF_BITRATELSB_150000	,	RF_BITRATEMSB_150000	}	,
{	RF_BITRATELSB_153600	,	RF_BITRATEMSB_153600	}	,
{	RF_BITRATELSB_200000	,	RF_BITRATEMSB_200000	}	,
{	RF_BITRATELSB_250000	,	RF_BITRATEMSB_250000	}	,
{	RF_BITRATELSB_300000	,	RF_BITRATEMSB_300000	}	
};

int BITRATE_Meaning[22] =
  { 
  1200	,
  2400	,
  4800	,
  9600	,
  12500	,
  19200	,
  25000	,
  32768	,
  34482	,
  38323	,
  38400	,
  50000	,
  55555	,
  57600	,
  76800	,
  100000	,
  115200	,
  150000	,
  153600	,
  200000	,
  250000	,
  300000
  };

//void AliveAnimation();
//void DisplayUpdater();
//void ProcessSerialInput();
//void RFM_Recive_msg();
//void RFM_Send_msg();

void setup() {
    for(int i = 0; i < RSSI_LogSize;  i++ )//clear IncomingMsg[] array
      RSSI_Log[i] = -100;

      pinMode(Button_Blue, INPUT_PULLUP);
      pinMode(Button_Yellow, INPUT_PULLUP);
      pinMode(Button_Green, INPUT_PULLUP);
      pinMode(Button_Red, INPUT_PULLUP);

      pinMode(LEDpin, OUTPUT);
      pinMode(buzzerPin, OUTPUT);



  Serial.begin(SERIAL_BAUD);
  ResetRadio();
  radio.initialize(FREQUENCY,NODEID,NETWORKID);

radio.writeReg(REG_BITRATEMSB, RF_BITRATEMSB_25000); // setup- function, after radio.initialize(...)
radio.writeReg(REG_BITRATELSB, RF_BITRATELSB_25000);   // setup- function, after radio.initialize(...)


#ifdef IS_RFM69HW_HCW
  radio.setHighPower(); //must include this only for RFM69HW/HCW!
#endif

#ifdef ENCRYPTKEY
  //radio.encrypt(ENCRYPTKEY);
  radio.encrypt(NULL);
#endif

#ifdef FREQUENCY_EXACT
  radio.setFrequency(FREQUENCY_EXACT); //set frequency to some custom frequency
#endif
  
//Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
//For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
//For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
//Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
#ifdef ENABLE_ATC
  radio.enableAutoPower(ATC_RSSI);
#endif

  char buff[50];
  sprintf(buff, "\nTransmitting at %d Mhz...", FREQUENCY==RF69_433MHZ ? 433 : FREQUENCY==RF69_868MHZ ? 868 : 915);
  Serial.println(buff);


#ifdef ENABLE_ATC
  Serial.println("RFM69_ATC Enabled (Auto Transmission Control)\n");
#endif

u8g2.begin();  
u8g2.enableUTF8Print();
//u8g2.setDisplayRotation(U8G2_R2);

esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
esp_task_wdt_add(NULL); //add current thread to WDT watch

}



long lastPeriod = 0;
void loop() {
  DisplayUpdater();
  ProcessSerialInput();
  if(ReciveMsgFlag == true){RFM_Recive_msg();}
  if(SendMsgFlag == true){RFM_Send_msg();}
  ButtonHandler();
  //delay(1);

  //Geiger(buzzerPin,map(CurrentRSSI,-100,-40,0,100));
  //Geiger(buzzerPin,BuzzerIntensity, BuzzerVolume);
  WatchDogFeeder();

  if(enableBuzzer == true)
  {
    delta = millis() - previousMillis_Buzzer;
        if (delta >= 0 && delta <150){
          digitalWrite(buzzerPin,1);//delay(150);
        }
        else if (delta >= 150 && delta <330){
          digitalWrite(buzzerPin,0);//delay(180);
        }
        else if(delta >= 330 && delta <390){
          digitalWrite(buzzerPin,1);//delay(60);
        }
        else if (delta >= 390 && delta <990){
          digitalWrite(buzzerPin,0);//delay(600);
        }
        else if(delta>1000){
          previousMillis_Buzzer = millis();
        }
  }
        
        //digitalWrite(buzzerPin,0);
        //delay(180);
        //digitalWrite(buzzerPin,1);
        //delay(60);
        //digitalWrite(buzzerPin,0);
        //delay(600);



}


void ProcessSerialInput(){

//process any serial input
//Used diagnostic characters:
//1-9 Change Sending Period
//R - read all register
//E   Enyript ON
//e   Enyript Off
//i   Information 
//t   read temp
//y   toggle receiving frame
//x   toggle sending frame
//c   powerlevel--
//v   powerlevel+
//b   bitrate--
//n   bitrate++
//p   reset radio
//k   BuzzerIntensity++
//l   BuzzerIntensity--
//h   BuzzerVolume++
//j   BuzzerVolume--
//g   Delay 5 sec (to test WDT)
//z   BuzzerOn
//u   BuzzerOff
//.   BuzzerPWM--
//-   BuzzerPWM++
  if (Serial.available() > 0)
  {
    char input = Serial.read();
    if (input >= 48 && input <= 57) //[0,9]
    {
      TRANSMITPERIOD = 100 * (input-48);
      if (TRANSMITPERIOD == 0) TRANSMITPERIOD = 1000;
      Serial.print("\nChanging delay to ");
      Serial.print(TRANSMITPERIOD);
      Serial.println("ms\n");
    }

    if (input == 'r') //d=dump register values
      radio.readAllRegs();

    if (input == 'E') //E=enable encryption
      radio.encrypt(ENCRYPTKEY);
    if (input == 'e') //e=disable encryption
      radio.encrypt(null);
if (input == 'i') // print all available setup infos
    {
      Serial.println();
      Serial.println("***************************************************************************");
      //return name of file and compile date/time
      Serial.println(__FILE__ " " __DATE__ " " __TIME__);
      
      //Serial.print("Slave Select Pin: ");
      //Serial.println(radio.getSlaveSelectPin());
      //Serial.print("Interrupt Pin: ");
      //Serial.println(radio.getInterruptPin());
      //Serial.print("Interrupt Number: ");
      //Serial.println(radio.getInterruptNumber());
      //Serial.print("Is RFM69HW: ");
      //Serial.println(radio.getHighPower());
      Serial.print("Power Level: ");
      Serial.println(radio.getPowerLevel());
      Serial.print("Frequency: ");
      Serial.println(radio.getFrequency());
      //Serial.print("NODEID: ");
      //Serial.println(radio.getAdress());
      //Serial.print("NETWORKID: ");
      //Serial.println(radio.getNetwork());

      //Serial.print("Test Pin (LED on STM32F103C Bluepill): ");
      //Serial.println(radio.getTestPin());
      Serial.println("***************************************************************************");
      Serial.println();
    }
    if (input == 't')
    {
      byte temperature =  radio.readTemperature(-1); // -1 = user cal factor, adjust for correct ambient
      byte fTemp = 1.8 * temperature + 32; // 9/5=1.8
      Serial.print( "Radio Temp is ");
      Serial.print(temperature);
      Serial.print("C, ");
      Serial.print(fTemp); //converting to F loses some resolution, obvious when C is on edge between 2 values (ie 26C=78F, 27C=80F)
      Serial.println('F');
    } 
    if (input == 'y')
    {
      ReciveMsgFlag = !ReciveMsgFlag;
      Serial.printf("ReciveMsgFlag: %d\n",ReciveMsgFlag);
    }
    if (input == 'x')
    {
      SendMsgFlag = !SendMsgFlag;
      Serial.printf("SendMsgFlag: %d\n",SendMsgFlag);
    }  
  
  if (input == 'c')
    {
      PowerLevel++;
      if (PowerLevel>23) PowerLevel = 23;
      radio.setPowerLevel(PowerLevel);
      Serial.printf("PL: %d BR: %d\n",radio.getPowerLevel(), BITRATE_Meaning[BITRATE_Counter]);
    }
    if (input == 'v')
    {
      PowerLevel--;
      if (PowerLevel<0) PowerLevel = 0;
      radio.setPowerLevel(PowerLevel);
      Serial.printf("PL: %d BR: %d\n",radio.getPowerLevel(), BITRATE_Meaning[BITRATE_Counter]);
    } 
    if (input == 'b')
    {
      BITRATE_Counter--;
      if (BITRATE_Counter<0) {BITRATE_Counter = 0;}
      radio.writeReg(REG_BITRATEMSB, BITRATE[BITRATE_Counter][1]); // setup- function, after radio.initialize(...)
      radio.writeReg(REG_BITRATELSB, BITRATE[BITRATE_Counter][0]);   // setup- function, after radio.initialize(...)
      Serial.printf("PL: %d BR: %d\n",radio.getPowerLevel(), BITRATE_Meaning[BITRATE_Counter]);
    }
    if (input == 'n')
    {
      BITRATE_Counter++;
      if (BITRATE_Counter>22) {BITRATE_Counter = 22;}
      radio.writeReg(REG_BITRATEMSB, BITRATE[BITRATE_Counter][1]); // setup- function, after radio.initialize(...)
      radio.writeReg(REG_BITRATELSB, BITRATE[BITRATE_Counter][0]);   // setup- function, after radio.initialize(...)
      Serial.printf("PL: %d BR: %d\n",radio.getPowerLevel(), BITRATE_Meaning[BITRATE_Counter]);
    }
    if (input == 'p')
    {
      ResetRadio(); 
    }
    if (input == 'k')
    {

      BuzzerIntensity++; 
      if (BuzzerIntensity>99) {BuzzerIntensity = 99;}
      Serial.printf("BuzzerIntensity: %d\n",BuzzerIntensity);
    }
    if (input == 'l')
    {
      BuzzerIntensity--;
      if (BuzzerIntensity<0) {BuzzerIntensity = 0;}
      Serial.printf("BuzzerIntensity: %d\n",BuzzerIntensity);
    }
    //BuzzerVolume
    if (input == 'h')
    {
      BuzzerVolume++; 
      //if (BuzzerIntensity>99) {BuzzerIntensity = 99;}
      Serial.printf("BuzzerVolume: %d\n",BuzzerVolume);
    }
    if (input == 'j')
    {
      BuzzerVolume--;
      //if (BuzzerIntensity<0) {BuzzerIntensity = 0;}
      Serial.printf("BuzzerVolume: %d\n",BuzzerVolume);
    }

    if (input == 'z')
    {
      enableBuzzer = true;
      //BuzzerPWM = 255;
      //analogWrite(buzzerPin,BuzzerPWM);
      //Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
    }
    if (input == 'u')
    {
      enableBuzzer = false;
      //BuzzerPWM = 0;
      //analogWrite(buzzerPin,BuzzerPWM);
      //Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
    }
    if (input == '.')
    {
      BuzzerPWM--;
      analogWrite(buzzerPin,BuzzerPWM);
      Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
    }
    if (input == '-')
    {
      BuzzerPWM++;
      analogWrite(buzzerPin,BuzzerPWM);
      Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
    }
    if (input == 'q')
    {
      //BuzzerPWM++;
        for(int i=0;i<6;i++){
        //Serial.printf("Start Meloday");
        //Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
        //analogWrite(buzzerPin,140);
        digitalWrite(buzzerPin,1);
        delay(150);
        
        //Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
        //analogWrite(buzzerPin,0);
        digitalWrite(buzzerPin,0);
        delay(180);

        //Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
        digitalWrite(buzzerPin,1);
        //analogWrite(buzzerPin,200);
        delay(60);
        
        //Serial.printf("BuzzerPWM: %d\n",BuzzerPWM);
        digitalWrite(buzzerPin,0);
        //analogWrite(buzzerPin,0);
        delay(600);
        
      }
    }

  }

}

void RFM_Recive_msg(){
if (radio.receiveDone())
  {
    Serial.print("#[");
    Serial.print(++packetCount);
    Serial.print(']');
    Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
    if (spy) Serial.print("to [");Serial.print(radio.TARGETID, DEC);Serial.print("] ");

    for(int b = 0; b < sizeof(IncomingMsg_char);  ++b )//clear IncomingMsg[] array
        IncomingMsg_char[b] = (char)0;

    for (byte i = 0; i < radio.DATALEN; i++){
      Serial.print((char)radio.DATA[i]);
      IncomingMsg_char[i] = (char)radio.DATA[i];
      IncomingMsg_int[i] = radio.DATA[i];
    }
    CurrentRSSI = radio.readRSSI();
    Serial.print("   [RX_RSSI:");Serial.print(CurrentRSSI);Serial.print("]");
    packetCount_Prev = packetCount; 



for(int i=RSSI_LogSize-1; i>0;i--){
  RSSI_Log[i]=RSSI_Log[i-1];
}
RSSI_Log[0] = CurrentRSSI;
BuzzerIntensity = map(CurrentRSSI,-100,-40,0,100);
if (BuzzerIntensity>99) {BuzzerIntensity = 100;}
if (BuzzerIntensity<=0) {BuzzerIntensity = 0;}
//Geiger(buzzerPin,map(CurrentRSSI,-100,-40,0,100));

//SerialPrint Diagram
    //for(int i=0; i<RSSI_LogSize;i++)
    //{Serial.printf("[%d]",RSSI_Log[i]);}
    //uint8_t VisualiseSection = 100/GraphResPixel; //-->20
    //Serial.printf("\n GraphResPixel: %d , VisualiseSection: %d\n",GraphResPixel, VisualiseSection);
    //for (int i = 0;i<VisualiseSection; i++){
//
    //  First_X = 25+i*GraphResPixel;
    //  Second_X = 25+5+i*GraphResPixel;
//
    //  First_RSSI = RSSI_Log[VisualiseSection-i];
    //  Second_RSSI = RSSI_Log[VisualiseSection-1-i];
    //  
    //  First_Y = map(First_RSSI,-100,-10,62,34);
    //  Second_Y = map(Second_RSSI,-100,-10,62,34);
    //  Serial.printf("|i:%d F_X:%d F_Y:%d/%d S_X:%d S_Y:%d/%d| \n", i, First_X,First_RSSI,First_Y,Second_X,Second_RSSI,Second_Y);
    //}

//-----------ACK------------------
//    if (radio.ACKRequested())
//    {
//      byte theNodeID = radio.SENDERID;
//      radio.sendACK();
//      Serial.print(" - ACK sent.");
//
//      // When a node requests an ACK, respond to the ACK
//      // and also send a packet requesting an ACK (every 3rd one only)
//      // This way both TX/RX NODE functions are tested on 1 end at the GATEWAY
//      if (ackCount++%3==0)
//      {
//        Serial.print(" Pinging node ");
//        Serial.print(theNodeID);
//        Serial.print(" - ACK...");
//        delay(3); //need this when sending right after reception .. ?
//        if (radio.sendWithRetry(theNodeID, "ACK TEST", 8, 0))  // 0 = only 1 attempt, no retries
//          Serial.print("ok!");
//        else Serial.print("nothing");
//      }
//    }
//


    Serial.println();
    //Blink(LED_BUILTIN,3);
    Blink(LEDpin,1);
  }
}

void RFM_Send_msg(){
//check for any received packets
  /*if (radio.receiveDone())
  {
    Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
    for (byte i = 0; i < radio.DATALEN; i++)
      Serial.print((char)radio.DATA[i]);
    Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");

    if (radio.ACKRequested())
    {
      radio.sendACK();
      Serial.print(" - ACK sent");
    }
    //Blink(LED_BUILTIN,3);
    Blink(LED_BUILTIN,1);
    Serial.println(33);
  }*/

  int currPeriod = millis()/TRANSMITPERIOD;
  if (currPeriod != lastPeriod)
  {
    lastPeriod=currPeriod;

    for(int b = 0; b < sizeof(SentMsg);  ++b )//clear SentMsg[]array
        SentMsg[b] = (char)0;

    //send FLASH id
    if(sendSize==0)
    {

      //byte buffLen=strlen(buff);
      //if (radio.sendWithRetry(GATEWAYID, buff, buffLen))
      //  Serial.print(" ok!");
      //else Serial.print(" nothing...");
      //sendSize = (sendSize + 1) % 31;
    }
    else
    {
      Serial.print("Sending[");
      Serial.print(sendSize);
      Serial.print("]: ");
      for(byte i = 0; i < sendSize; i++){
        Serial.print((char)payload[i]);
        SentMsg[i] = (char)payload[i];
      }
        
      sentMSGCounter++;
      sendSize++;
      if(sendSize>5){
        sendSize=1;
      }
      radio.send(GATEWAYID, payload, sendSize);
      //if (radio.sendWithRetry(GATEWAYID, payload, sendSize))
      // Serial.print(" ok!");
      //else Serial.print(" nothing...");
    }
    //sendSize = (sendSize + 1) % 31;
    //sendSize = (sendSize + 1) % 4;
    Serial.println();
    Blink(LED_BUILTIN,1);
    //Blink(33,3);
  }
}

void DisplayUpdater(){
  if (millis() - previousMillis_DisplayUpdater >= 100) {
      previousMillis_DisplayUpdater = millis();
    
    //

    u8g2.clearBuffer(); // clear the internal memory
      AliveAnimation();
    //if(sentMSGCounter_Prev != sentMSGCounter || packetCount_Prev != packetCount){
//   //if (RFM_Recive_msg_DispFlag == true){
      u8g2.setFont(u8g2_font_5x8_tf); //Width - 4; Height - 6
      u8g2.setCursor(0,8);
      u8g2.printf("ID:%d  T:%d RSSI:%d",NODEID, GATEWAYID, CurrentRSSI);//u8g2.print(SentMsg);

      u8g2.setCursor(0,16); 
      u8g2.printf("Got[%d]%s",packetCount, IncomingMsg_char);
      //u8g2.print("Got:[");u8g2.print(packetCount);u8g2.print(']');u8g2.print(IncomingMsg_char);
      
  //  }
  //if (RFM_Send_msg_DispFlag == true){
      u8g2.setFont(u8g2_font_5x8_tf); //Width - 4; Height - 6
      u8g2.setCursor(0,24); 
      u8g2.printf("Sent[%d]%s",sentMSGCounter,SentMsg);//u8g2.print(SentMsg); 
      sentMSGCounter_Prev = sentMSGCounter;

 //    //}
    //}
    /*
    u8g2.setCursor(0,30);
    u8g2.setFont(u8g2_font_5x8_tf); 
    u8g2.printf("B:%d ",Button_Blue_state);
    u8g2.printf("Y:%d ",Button_Yellow_state);
    u8g2.printf("G:%d ",Button_Green_state);
    u8g2.printf("R:%d ",Button_Red_state);
    */
    //virtual int8_t setPowerDBm(int8_t dBm); // reduce/increase transmit power level, in dBm
//
    //virtual uint8_t getPowerLevel(); // get powerLevel

    u8g2.setCursor(0,32); 
    u8g2.printf("PL: %d BR: %d",radio.getPowerLevel(), BITRATE_Meaning[BITRATE_Counter]);
    //u8g2.setCursor(0,40); 
    //u8g2.printf("BITRATE: %d",BITRATE_Meaning[BITRATE_Counter]);

    u8g2.drawFrame(0,33,128,31);
    u8g2.setFont(u8g2_font_u8glib_4_tf);


    u8g2.setCursor(0,39); 
    u8g2.printf("-30dbi");
    u8g2.setCursor(0,62); 
    u8g2.printf("-100dbi");
    u8g2.drawLine(25, 33, 25, 63);


//RSSI_LogSize
//const uint8_t GraphResPixel = 5;
//const uint8_t RSSI_LogSize = 200;//126/GraphResPixel;//20
//int RSSI_Log[RSSI_LogSize];

//uint8_t VisualiseSection = 100/GraphResPixel; //-->20
//Serial.printf("GraphResPixel: %d , VisualiseSection: %d\n",GraphResPixel, VisualiseSection);
//
//
//for (int i = 0;i<VisualiseSection; i++){
//  uint8_t First_X = 25+i*GraphResPixel;
//  uint8_t Second_X = 25+5+i*GraphResPixel;
//
//  int8_t First_RSSI = RSSI_Log[VisualiseSection-i];
//  int8_t Second_RSSI = RSSI_Log[VisualiseSection-1-i];
//
//  int8_t First_Y = map(RSSI_Log[VisualiseSection-i],-100,-10,62,34);
//  int8_t Second_Y = map(RSSI_Log[VisualiseSection],-100,-10,62,34);
//
//  Serial.printf("|i:%d F_X:%d F_Y:%d/%d S_X:%d S_Y:%d/%d| \n", i, First_X,First_RSSI,First_Y,Second_X,Second_RSSI,Second_Y);
//  u8g2.drawLine(First_X, First_Y, Second_X, Second_Y);
//
//}


uint8_t VisualiseSection = 100/GraphResPixel; //-->20
//Serial.printf("\n GraphResPixel: %d , VisualiseSection: %d\n",GraphResPixel, VisualiseSection);


for (int i = 0;i<VisualiseSection; i++){
  First_X = 25+i*GraphResPixel;
  Second_X = 25+5+i*GraphResPixel;

  First_RSSI = RSSI_Log[VisualiseSection-i];
  Second_RSSI = RSSI_Log[VisualiseSection-1-i];

  First_Y = map(First_RSSI,-100,-30,62,34);
  Second_Y = map(Second_RSSI,-100,-30,62,34);

//  Serial.printf("|i:%d F_X:%d F_Y:%d/%d S_X:%d S_Y:%d/%d| \n", i, First_X,First_RSSI,First_Y,Second_X,Second_RSSI,Second_Y);
  u8g2.drawLine(First_X, First_Y, Second_X, Second_Y);

}



      u8g2.sendBuffer();          // transfer internal memory to the display
  

  }
}




void AliveAnimation(){


  if (millis() - previousMillis_LoadingIcon >= 100) {
    // save the last time you blinked the LED
    previousMillis_LoadingIcon = millis();
    IconState++;
    if(IconState == 8){
      IconState = 0;
    }
  }

  u8g2.setDrawColor(1);

  switch (IconState){
    case 0:
      u8g2.setDrawColor(1);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      u8g2.setDrawColor(0);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      break;

    case 1:
      u8g2.setDrawColor(1);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.setDrawColor(0);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      break;
    case 2:
      u8g2.setDrawColor(1);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.setDrawColor(0);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      break;
    case 3:
      u8g2.setDrawColor(1);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      u8g2.setDrawColor(0);
      break;



    case 4:
      u8g2.setDrawColor(0);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      u8g2.setDrawColor(1);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      break;
    case 5:
      u8g2.setDrawColor(0);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.setDrawColor(1);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      break;
    case 6:
      u8g2.setDrawColor(0);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.setDrawColor(1);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      break;
    case 7:
      u8g2.setDrawColor(0);
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_RIGHT );
      
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_UPPER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_LEFT );
      u8g2.drawCircle(123, 4, 4,U8G2_DRAW_LOWER_RIGHT );
      u8g2.setDrawColor(1);
      break;
        
    }
    
  u8g2.setDrawColor(1);
}

void ButtonHandler(){

  Button_Blue_state = digitalRead(Button_Blue);
  Button_Yellow_state= digitalRead(Button_Yellow);
  Button_Green_state = digitalRead(Button_Green);
  Button_Red_state= digitalRead(Button_Red);




  if(Button_Blue_state_Prev!=Button_Blue_state){
    Serial.printf("Button-B: %d\n", Button_Blue_state);
    if(Button_Blue_state==0){
      //code
      PowerLevel++;
      if (PowerLevel>23) PowerLevel = 23;
      radio.setPowerLevel(PowerLevel);
    }
  }
  if(Button_Yellow_state_Prev!=Button_Yellow_state){
    Serial.printf("Button-Y: %d\n", Button_Yellow_state);
    if(Button_Yellow_state==0){
      //code
      PowerLevel--;
      if (PowerLevel>23) PowerLevel = 23;
      radio.setPowerLevel(PowerLevel);
    }
  }
  if(Button_Green_state_Prev!=Button_Green_state){
    Serial.printf("Button-G: %d\n", Button_Green_state);
    if(Button_Green_state==0){
      //code
      BITRATE_Counter++;
      if (BITRATE_Counter>22) {BITRATE_Counter = 22;}
      radio.writeReg(REG_BITRATEMSB, BITRATE[BITRATE_Counter][1]); // setup- function, after radio.initialize(...)
      radio.writeReg(REG_BITRATELSB, BITRATE[BITRATE_Counter][0]);   // setup- function, after radio.initialize(...)
    }
  }
  if(Button_Red_state_Prev!=Button_Red_state){
    Serial.printf("Button_R: %d\n", Button_Red_state);
    if(Button_Red_state==0){
      //code
      BITRATE_Counter--;
      if (BITRATE_Counter>22) {BITRATE_Counter = 22;}
      radio.writeReg(REG_BITRATEMSB, BITRATE[BITRATE_Counter][1]); // setup- function, after radio.initialize(...)
      radio.writeReg(REG_BITRATELSB, BITRATE[BITRATE_Counter][0]);   // setup- function, after radio.initialize(...)
    }
  }

  Button_Blue_state_Prev = Button_Blue_state;
  Button_Yellow_state_Prev = Button_Yellow_state;
  Button_Green_state_Prev = Button_Green_state;
  Button_Red_state_Prev = Button_Red_state;
}

void WatchDogFeeder(){
  if (millis() - previousMillis_WatchDogTimer >= 2000) {
    previousMillis_WatchDogTimer = millis();
    esp_task_wdt_reset();
  }
}