/*
 * MIT License
 * Copyright (c) 2019, 2018 - present OMRON Corporation
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* includes */
#include <Wire.h>

/* defines */
#define D6T_ADDR 0x0A  // for I2C 7bit address
#define D6T_CMD 0x4C  // for D6T-44L-06/06H, D6T-8L-09/09H, for D6T-1A-01/02

#define N_ROW 1
#define N_PIXEL 1

#define N_READ ((N_PIXEL + 1) * 2 + 1)
/***** Setting Parameter *****/
#define comparingNumInc 8 // x200 ms   (example) 8 -> 1.6 sec
#define comparingNumDec 8  // x200 ms   (example) 8 -> 1.6 sec
#define threshHoldInc 10 //  /10 degC   (example) 10 -> 1.0 degC
#define threshHoldDec 10 //  /10 degC   (example) 10 -> 1.0 degC
//bool  enablePix[8] = {true, true, true, true, true, true, true, true};
/****************************/

uint8_t rbuf[N_READ];
int16_t pix_data = 0;
int16_t seqData[40] = {0};
bool  occuPix = 0;
bool  occuPixFlag = false;
uint8_t  resultOccupancy = 0;
uint16_t  totalCount = 0;

/** JUDGE_occupancy: judge occupancy*/
bool judge_seatOccupancy(void) { 
  int j = 0; 
  for (j = 0; j < 39; j++){
    seqData[39 - j] = seqData[38 - j];
  }
  seqData[0] = pix_data;            
  if (totalCount <= comparingNumInc){
    totalCount++;
  }
  if (totalCount > comparingNumInc){    
    if (occuPix == false){
      if ((int16_t)(seqData[0] - seqData[comparingNumInc]) > (int16_t)threshHoldInc){
        occuPix = true;
      }
    }
    else{   //resultOccupancy == true
      if ((int16_t)(seqData[comparingNumDec] - seqData[0]) > (int16_t)threshHoldDec){
        occuPix = false;
      }
    }
    if (resultOccupancy == 0) {                
        if(occuPix == true){
          resultOccupancy = 1;
        }
    }
    else{
      occuPixFlag = false;
      if (occuPix == true){
        occuPixFlag = true;
      }
      if (occuPixFlag == false){
        resultOccupancy = 0;
      }
    }
  }
  return true;
}

uint8_t calc_crc(uint8_t data) {
    int index;
    uint8_t temp;
    for (index = 0; index < 8; index++) {
        temp = data;
        data <<= 1;
        if (temp & 0x80) {data ^= 0x07;}
    }
    return data;
}

/** <!-- D6T_checkPEC {{{ 1--> D6T PEC(Packet Error Check) calculation.
 * calculate the data sequence,
 * from an I2C Read client address (8bit) to thermal data end.
 */
bool D6T_checkPEC(uint8_t buf[], int n) {
    int i;
    uint8_t crc = calc_crc((D6T_ADDR << 1) | 1);  // I2C Read address (8bit)
    for (i = 0; i < n; i++) {
        crc = calc_crc(buf[i] ^ crc);
    }
    bool ret = crc != buf[n];
    if (ret) {
        Serial.print("PEC check failed:");
        Serial.print(crc, HEX);
        Serial.print("(cal) vs ");
        Serial.print(buf[n], HEX);
        Serial.println("(get)");
    }
    return ret;
}


/** <!-- conv8us_s16_le {{{1 --> convert a 16bit data from the byte stream.
 */
int16_t conv8us_s16_le(uint8_t* buf, int n) {
    int ret;
    ret = buf[n];
    ret += buf[n + 1] << 8;
    return (int16_t)ret;   // and convert negative.
}


/** <!-- setup {{{1 -->
 * 1. initialize a Serial port for output.
 * 2. initialize an I2C peripheral.
 */
void setup() {
    Serial.begin(115200);  // Serial baudrate = 115200bps
    Wire.begin();  // i2c master
}


/** <!-- loop - Thermal sensor {{{1 -->
 * 1. read sensor.
 * 2. output results, format is: [degC]
 */
void loop() {
    int i, j;

    memset(rbuf, 0, N_READ);
    // Wire buffers are enough to read D6T-16L data (33bytes) with
    // MKR-WiFi1010 and Feather ESP32,
    // these have 256 and 128 buffers in their libraries.
    Wire.beginTransmission(D6T_ADDR);  // I2C client address
    Wire.write(D6T_CMD);               // D6T register
    Wire.endTransmission();            // I2C repeated start for read
    Wire.requestFrom(D6T_ADDR, N_READ);
    i = 0;
    while (Wire.available()) {
        rbuf[i++] = Wire.read();
    }

    if (D6T_checkPEC(rbuf, N_READ - 1)) {
        return;
    }

    // 1st data is PTAT measurement (: Proportional To Absolute Temperature)
    int16_t itemp = conv8us_s16_le(rbuf, 0);
    Serial.print("PTAT:");
    Serial.print(itemp / 10.0, 1);
    Serial.print(", Temperature:");

    // loop temperature pixels of each thrmopiles measurements
    for (i = 0, j = 2; i < N_PIXEL; i++, j += 2) {
        itemp = conv8us_s16_le(rbuf, j);
        pix_data = itemp;
        Serial.print(itemp / 10.0, 1);  // print PTAT & Temperature
        if ((i % N_ROW) == N_ROW - 1) {
            Serial.print(" [degC]");  // wrap text at ROW end.
        } else {
            Serial.print(", ");   // print delimiter
        }
    }
    judge_seatOccupancy(); //add
    Serial.print(", Occupancy:");
    Serial.println(resultOccupancy, 1);
    delay(200);
}
// vi: ft=arduino:fdm=marker:et:sw=4:tw=80
