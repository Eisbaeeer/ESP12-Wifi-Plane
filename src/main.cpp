#include <Arduino.h>

/**********************************************************************
 *  VIMANA2 ESP8266 based flight controller for STEM/STEAM Education
 *  Plane diff thrust 2-MOTOR 2-SERVO (NO IMU)
 *  Designed and developed by Ravi Butani 
 *  Contact : ravi.butani03@gmail.com
 *  Version 2 - April 2023
 *  Note : ESP8266WiFiAp.h changed max connection to 1 in SDK
 *  Note : Need N76E003 Slave MCU for ARM status and PWMs for Motor and SERVO drive
 *	Project Documentation : https://hackaday.io/project/190462-vimana-stem-for-all
 **********************************************************************
 *  20240405
 *  Lars Weimar
 *  Ported Firmware to using new APP with first project (onlny 2 motors)
 *  No N76E003 needed!
 *  Changed Analog Read to input voltage of module
 **********************************************************************/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h> // used for UDP comms.
#include <EEPROM.h>
//#define DEBUGLED  // Uncomment this line to observe loop and rx pkt timing
//#define DEBUGSER  // Uncomment this line to get redable values of angle and rx pkt on serial terminal (note - N76E003 not understand this readings)

#define PID_LOOP_US  6000
#define L_MOTOR 4
#define R_MOTOR 5

unsigned int l_speed = 0;
unsigned int r_speed = 0;

#define ST_LED        2  //D4
#define DEBUG_LED     5  //D1  // Toggle when pkt received
#define DEBUG_LED2    16 //D0  // HIGH during calculation in loop
#define RESP_CNT_MAX  50

// set Server stuff
WiFiUDP Udp;

//***UDP Variables***
unsigned int localUdpPort = 2390;

//*** Soft Ap variables ***
IPAddress APlocal_IP(192, 168, 43, 42);
IPAddress APgateway(192, 168, 43, 42);
IPAddress APsubnet(255, 255, 255, 0);
uint16_t rxcount=0;

unsigned long microsold=0;

int16_t R_ch=128, P_ch=128, T_ch=0, Y_ch=128; 
unsigned char AUX_ch;
 
float R_in=0,R_set=0,R_out=0; 
int16_t mot1_final=0,mot2_final=0,speed=0;
uint32_t loop_count=0;
uint32_t old_pkt_cnt=0, current_pkt_cnt=0;
uint8_t con_flag=0;
uint8_t debug_flag=0;
uint8_t ser_r,ser_p,ser_y;

void setup_hardware_wifi_ap(void);

// ======================== check for RX Packet ============================
unsigned int check_rx_pkt(int16_t& rch,int16_t& pch,int16_t& tch,int16_t& ych,unsigned char& auxch)
{
 int packetSize = Udp.parsePacket();
 char incomingPacket[12];
 char replyPacket[] = "OK1";
 if (packetSize)
 {
   // receive incoming UDP packets ToDO On Receive PID Request Caliberate Gyro and for 3second no packet received than motor off (if possible)
   int len = Udp.read(incomingPacket, 25);
   if (len > 8)
   {
    auxch = incomingPacket[0];
    if (((auxch & 0xB1) != 0x00)||(auxch==0x00)) // Control Packet Received
    {
      rch   = ((((int16_t)incomingPacket[1]<<8)+(int16_t)incomingPacket[2])-296);
      pch   = ((((int16_t)incomingPacket[3]<<8)+(int16_t)incomingPacket[4])-296);
      tch   = ((((int16_t)incomingPacket[5]<<8)+(int16_t)incomingPacket[6]))+60;
      ych   = ((((int16_t)incomingPacket[7]<<8)+(int16_t)incomingPacket[8])-296);
      rch = map(rch,-296,296,0,255);
      pch = map(pch,-296,296,0,255);
      tch = map(tch,0,660,0,500);
      ych = map(ych,-260,260,0,255);
    }

    rxcount++;
    current_pkt_cnt++;
   }
   if(rxcount>=RESP_CNT_MAX)
   {
    rxcount = 0;
    // send back a reply, to the IP address and port we got the packet from
    Udp.beginPacket(Udp.remoteIP(), 2399);
    replyPacket[0] = (unsigned char)(1);
    replyPacket[1] = (unsigned char)(120);//(abs(WiFi.RSSI()));
    replyPacket[2] = (unsigned char)(((float)ESP.getVcc()/22.82)-2.0);
    //replyPacket[2] = (unsigned char)(((float)analogRead(A0)/22.82)-2.0);//(48); // use this if you will read from the A0 pin
    Udp.write(replyPacket);
    Udp.endPacket();
   }
   #ifdef DEBUGLED    
   debug_flag = !debug_flag;
   digitalWrite(DEBUG_LED,debug_flag);
   #endif
   
   return 1; // Sucess
 }
 else
 {
  return 0; // fail
 }
}

void setup() {
  
  Serial.begin(115200);
  setup_hardware_wifi_ap();
  Serial.println("AP Setup done");
  EEPROM.begin(1024);   // Initlize EEPROM with 1K Bytes (Not useful in this code)
  pinMode(L_MOTOR, OUTPUT);
  pinMode(R_MOTOR, OUTPUT);
  analogWrite(L_MOTOR,0);
  analogWrite(R_MOTOR,0);
  delay(500);
}

void loop() {

  if(micros()-microsold >= PID_LOOP_US)
  {
    #ifdef DEBUGLED    
    digitalWrite(DEBUG_LED2,HIGH);
    #endif
    microsold = micros();
    check_rx_pkt(R_ch,P_ch,T_ch,Y_ch,AUX_ch);
    if((AUX_ch & 0x80) == 0x80) //if ARMED 
    {
      mot1_final = T_ch;
    }
    else  //NOT ARMED
    {
      mot1_final = 0;
    }
    if(WiFi.softAPgetStationNum() == 0)
    {
      AUX_ch=0x00;
      mot1_final = 0;
      digitalWrite(ST_LED,HIGH);
    }
    else 
    {
      digitalWrite(ST_LED,LOW);
    }
    #ifdef DEBUGLED    
    digitalWrite(DEBUG_LED2,LOW);
    #endif

    /*
          Hier muss dann die Logic für die Motoren rein:
    */
      // Motor aus "disarmed"
      if (mot1_final == 0 ) {
        analogWrite(L_MOTOR,0);
        analogWrite(R_MOTOR,0);
      }

      // Motor an "armed"
      if (mot1_final > 0) {
        int16_t ruder_r = 0;
        int16_t ruder_l = 0;
        speed = map(mot1_final, 0, 494, 0, 255);    
        
        if (R_ch > 128) {
          ruder_r = R_ch - 128;
          analogWrite(R_MOTOR,speed-ruder_r);
          analogWrite(L_MOTOR,speed);
        } else if (R_ch < 128) {
          ruder_l = 128 - R_ch;
          analogWrite(R_MOTOR,speed);
          analogWrite(L_MOTOR,speed-ruder_l);
        } else {
        analogWrite(L_MOTOR,speed);
        analogWrite(R_MOTOR,speed);
        }
      }

    #ifdef DEBUGSER  // debug message for check what IMU remote and PID doing
    Serial.print(mot1_final);         Serial.print('\t');
    Serial.print(AUX_ch);             Serial.print('\t');
    Serial.print(R_ch);               Serial.print('\t');
    Serial.print(P_ch);               Serial.print('\t');
    Serial.print(T_ch);               Serial.print('\t');
    Serial.print(Y_ch);               Serial.print("\r\n");
    #endif
       
    //disarm if no packet received for 3 seconds app closed accidently
    loop_count++; 
    if(loop_count>=500)
    {
      loop_count = 0;
      if(old_pkt_cnt == current_pkt_cnt)
      {
        AUX_ch = 0x00;
      }
      else
      {
        old_pkt_cnt = current_pkt_cnt;
      }  
    }
  }
}


// ========================= Setup Hardware and WiFi AP ===========================================
void setup_hardware_wifi_ap(void)
{
  // Set NodeMCU Wifi hostname based on chip mac address
  char chip_id[15];
  snprintf(chip_id, 15, "%04X", (uint16_t)(ESP.getChipId()));
  String APssid = "Wifi-Plane - " + String(chip_id);

  pinMode(ST_LED,OUTPUT);
  pinMode(DEBUG_LED,OUTPUT);
  pinMode(DEBUG_LED2,OUTPUT);
  digitalWrite(ST_LED,HIGH);
  digitalWrite(DEBUG_LED,LOW);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APlocal_IP, APgateway, APsubnet);
  //WiFi.softAP(APssid, APpassword);
  WiFi.softAP(APssid);
  Udp.begin(localUdpPort);
}



