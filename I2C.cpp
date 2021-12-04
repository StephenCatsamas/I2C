/*
  I2C.cpp - I2C library
  Copyright (c) 2011-2012 Wayne Truchsess.  All right reserved.
  Rev 5.0 - January 24th, 2012
          - Removed the use of interrupts completely from the library
            so TWI state changes are now polled. 
          - Added calls to lockup() function in most functions 
            to combat arbitration problems 
          - Fixed scan() procedure which left timeouts enabled 
            and set to 80msec after exiting procedure
          - Changed scan() address range back to 0 - 0x7F
          - Removed all Wire legacy functions from library
          - A big thanks to Richard Baldwin for all the testing
            and feedback with debugging bus lockups!
  Rev 4.0 - January 14th, 2012
          - Updated to make compatible with 8MHz clock frequency
  Rev 3.0 - January 9th, 2012
          - Modified library to be compatible with Arduino 1.0
          - Changed argument type from boolean to uint8_t in pullUp(), 
            setSpeed() and _receiveByte() functions for 1.0 compatability
          - Modified return values for timeout feature to report
            back where in the transmission the timeout occured.
          - added function scan() to perform a bus scan to find devices
            attached to the I2C bus.  Similar to work done by Todbot
            and Nick Gammon
  Rev 2.0 - September 19th, 2011
          - Added support for timeout function to prevent 
            and recover from bus lockup (thanks to PaulS
            and CrossRoads on the Arduino forum)
          - Changed return type for _stop() from void to
            uint8_t to handle timeOut function 
  Rev 1.0 - August 8th, 2011
  
  This is a modified version of the Arduino Wire/TWI 
  library.  Functions were rewritten to provide more functionality
  and also the use of Repeated Start.  Some I2C devices will not
  function correctly without the use of a Repeated Start.  The 
  initial version of this library only supports the Master.


  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#if (ARDUINO >= 100)
#include <Arduino.h>
#else
#include <WProgram.h>
#endif

#include <inttypes.h>
#include "I2C.h"

uint8_t I2C::bytesAvailable = 0;
uint8_t I2C::bufferIndex = 0;
uint8_t I2C::totalBytes = 0;
uint16_t I2C::timeOutDelay = 0;

I2C::I2C()
{
}

////////////// Public Methods ////////////////////////////////////////

void I2C::begin()
{
  pullup(1);

  // initialize twi prescaler and bit rate
  cbi(TWSR, TWPS0);
  cbi(TWSR, TWPS1);
  TWBR = ((F_CPU / 100000) - 16) / 2;
  // enable twi module and acks
  TWCR = _BV(TWEN) | _BV(TWEA);
}

void I2C::end()
{
  TWCR = 0;
}

void I2C::timeOut(uint16_t _timeOut)
{
  timeOutDelay = _timeOut;
}

void I2C::setSpeed(uint8_t _fast)
{
  if (!_fast)
  {
    TWBR = ((F_CPU / 100000) - 16) / 2;
  }
  else
  {
    TWBR = ((F_CPU / 400000) - 16) / 2;
  }
}

void I2C::pullup(uint8_t activate)
{
  if (activate)
  {
#if defined(__AVR_ATmega168__) || defined(__AVR_ATmega8__) || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__)
    // activate internal pull-ups for twi
    // as per note from atmega8 manual pg167
    sbi(PORTC, 4);
    sbi(PORTC, 5);
#elif defined(__AVR_ATmega644__) || defined(__AVR_ATmega644P__)
    // activate internal pull-ups for twi
    // as per note from atmega644p manual pg108
    sbi(PORTC, 0);
    sbi(PORTC, 1);
#else
    // activate internal pull-ups for twi
    // as per note from atmega128 manual pg204
    sbi(PORTD, 0);
    sbi(PORTD, 1);
#endif
  }
  else
  {
#if defined(__AVR_ATmega168__) || defined(__AVR_ATmega8__) || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__)
    // deactivate internal pull-ups for twi
    // as per note from atmega8 manual pg167
    cbi(PORTC, 4);
    cbi(PORTC, 5);
#elif defined(__AVR_ATmega644__) || defined(__AVR_ATmega644P__)
    // deactivate internal pull-ups for twi
    // as per note from atmega644p manual pg108
    cbi(PORTC, 0);
    cbi(PORTC, 1);
#else
    // deactivate internal pull-ups for twi
    // as per note from atmega128 manual pg204
    cbi(PORTD, 0);
    cbi(PORTD, 1);
#endif
  }
}

void I2C::scan()
{
  uint16_t tempTime = timeOutDelay;
  timeOut(80);
  uint8_t totalDevicesFound = 0;
  Serial.println(F("Scanning for devices...please wait"));
  Serial.println();
  for (uint8_t s = 0; s <= 0x7F; s++)
  {
    uint8_t returnStatus;
    returnStatus = _start();
    if (!returnStatus)
    {
      returnStatus = _sendAddress(SLA_W(s));
    }
    if (returnStatus)
    {
      if (returnStatus == 1)
      {
        Serial.println(F("There is a problem with the bus, could not complete scan"));
        timeOutDelay = tempTime;
        return;
      }
    }
    else
    {
      Serial.print(F("Found device at address - "));
      Serial.print(F(" 0x"));
      Serial.println(s, HEX);
      totalDevicesFound++;
    }
    _stop();
  }
  if (!totalDevicesFound)
  {
    Serial.println(F("No devices found"));
  }
  timeOutDelay = tempTime;
}

uint8_t I2C::available()
{
  return (bytesAvailable);
}

uint8_t I2C::receive()
{
  bufferIndex = totalBytes - bytesAvailable;
  if (!bytesAvailable)
  {
    bufferIndex = 0;
    return (0);
  }
  bytesAvailable--;
  return (data[bufferIndex]);
}

/*return values for new functions that use the timeOut feature 
  will now return at what point in the transmission the timeout
  occurred. Looking at a full communication sequence between a 
  master and slave (transmit data and then readback data) there
  a total of 7 points in the sequence where a timeout can occur.
  These are listed below and correspond to the returned value:
  1 - Waiting for successful completion of a Start bit
  2 - Waiting for ACK/NACK while addressing slave in transmit mode (MT)
  3 - Waiting for ACK/NACK while sending data to the slave
  4 - Waiting for successful completion of a Repeated Start
  5 - Waiting for ACK/NACK while addressing slave in receiver mode (MR)
  6 - Waiting for ACK/NACK while receiving data from the slave
  7 - Waiting for successful completion of the Stop bit

  All possible return values:
  0           Function executed with no errors
  1 - 7       Timeout occurred, see above list
  8 - 0xFF    See datasheet for exact meaning */

/////////////////////////////////////////////////////

uint8_t I2C::write(uint8_t address, uint8_t registerAddress)
{
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  returnStatus = _sendByte(registerAddress);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}

  return (returnStatus);
}

uint8_t I2C::write(int address, int registerAddress)
{
  return (write((uint8_t)address, (uint8_t)registerAddress));
}


uint8_t I2C::write(uint8_t address, uint8_t registerAddress, uint8_t data)
{
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  returnStatus = _sendByte(registerAddress);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  returnStatus = _sendByte(data);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

uint8_t I2C::write(int address, int registerAddress, int data)
{
  return (write((uint8_t)address, (uint8_t)registerAddress, (uint8_t)data));
}

uint8_t I2C::write(uint8_t address, uint8_t registerAddress, const char *data)
{
  uint8_t bufferLength = strlen(data);
  uint8_t returnStatus;
  returnStatus = write(address, registerAddress, (const uint8_t *)data, bufferLength);
  return (returnStatus);
}

uint8_t I2C::write(uint8_t address, uint8_t registerAddress, uint16_t data)
{
  //Array to hold the 2 bytes that will be written to the register
  uint8_t writeBytes[2];
  uint8_t returnStatus;

  writeBytes[0] = (data >> 8) & 0xFF; //MSB
  writeBytes[1] = data & 0xFF;        //LSB

  returnStatus = write(address, registerAddress, writeBytes, 2);
  return (returnStatus);
}

uint8_t I2C::write(uint8_t address, uint8_t registerAddress, uint32_t data)
{
  //Array to hold the 4 bytes that will be written to the register
  uint8_t writeBytes[4];
  uint8_t returnStatus;

  writeBytes[0] = (data >> 24) & 0xFF; //MSB
  writeBytes[1] = (data >> 16) & 0xFF;
  writeBytes[2] = (data >> 8) & 0xFF;
  writeBytes[3] = data & 0xFF; //LSB

  returnStatus = write(address, registerAddress, writeBytes, 4);
  return (returnStatus);
}

uint8_t I2C::write(uint8_t address, uint8_t registerAddress, uint64_t data)
{
  //Array to hold the 8 bytes that will be written to the register
  uint8_t writeBytes[8];
  uint8_t returnStatus;

  writeBytes[0] = (data >> 56) & 0xFF; //MSB
  writeBytes[1] = (data >> 48) & 0xFF;
  writeBytes[2] = (data >> 40) & 0xFF;
  writeBytes[3] = (data >> 32) & 0xFF;
  writeBytes[4] = (data >> 24) & 0xFF;
  writeBytes[5] = (data >> 16) & 0xFF;
  writeBytes[6] = (data >> 8) & 0xFF;
  writeBytes[7] = data & 0xFF; //LSB

  returnStatus = write(address, registerAddress, writeBytes, 8);
  return (returnStatus);
}

uint8_t I2C::write(uint8_t address, uint8_t registerAddress, const uint8_t *data, uint8_t numberBytes)
{
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  returnStatus = _sendByte(registerAddress);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    returnStatus = _sendByte(data[i]);
    if (returnStatus){return send_error_handler(returnStatus);}
  }
  
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

uint8_t I2C::read(int address, int numberBytes)
{
  return (read((uint8_t)address, (uint8_t)numberBytes));
}

uint8_t I2C::read(uint8_t address, uint8_t numberBytes)
{
  bytesAvailable = 0;
  bufferIndex = 0;
  numberBytes = min(numberBytes, MAX_BUFFER_SIZE);
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint8_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
      
    }
    data[i] = TWDR;
    bytesAvailable = i + 1;
    totalBytes = i + 1;
  }
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

uint8_t I2C::read(int address, int registerAddress, int numberBytes)
{
  return (read((uint8_t)address, (uint8_t)registerAddress, (uint8_t)numberBytes));
}

uint8_t I2C::read(uint8_t address, uint8_t registerAddress, uint8_t numberBytes)
{
  bytesAvailable = 0;
  bufferIndex = 0;
  numberBytes = min(numberBytes, MAX_BUFFER_SIZE);
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint8_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  returnStatus = _sendByte(registerAddress);
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, REPEATED_START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    data[i] = TWDR;
    bytesAvailable = i + 1;
    totalBytes = i + 1;
  }
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

uint8_t I2C::read(uint8_t address, uint8_t numberBytes, uint8_t *dataBuffer)
{
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint8_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    dataBuffer[i] = TWDR;
  }
  
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

uint8_t I2C::read(uint8_t address, uint16_t numberBytes, uint8_t *dataBuffer)
{
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint16_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint16_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    dataBuffer[i] = TWDR;
  }
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

uint8_t I2C::read(uint8_t address, uint8_t registerAddress, uint8_t numberBytes, uint8_t *dataBuffer)
{
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint8_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  returnStatus = _sendByte(registerAddress);
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, REPEATED_START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    dataBuffer[i] = TWDR;
  }
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

uint8_t I2C::read(uint8_t address, uint8_t registerAddress, uint16_t numberBytes, uint8_t *dataBuffer)
{
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint16_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  returnStatus = _sendByte(registerAddress);
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, REPEATED_START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint16_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    dataBuffer[i] = TWDR;
  }
  returnStatus = _stop();
 if (returnStatus){return stop_error_handler(returnStatus);}
 
  return (returnStatus);
}

////////// 16-Bit Methods ///////////

//These functions will be used to write to Slaves that take 16-bit addresses
uint8_t I2C::write16(uint8_t address, uint16_t registerAddress)
{
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  //Send MSB of register address
  returnStatus = _sendByte(registerAddress >> 8);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  //Send LSB of register address
  returnStatus = _sendByte(registerAddress & 0xFF);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}
uint8_t I2C::write16(uint8_t address, uint16_t registerAddress, uint8_t data)
{
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  //Send MSB of register address
  returnStatus = _sendByte(registerAddress >> 8);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  //Send LSB of register address
  returnStatus = _sendByte(registerAddress & 0xFF);
  if (returnStatus){return send_error_handler(returnStatus);}
  
  returnStatus = _sendByte(data);
  if (returnStatus){return send_error_handler(returnStatus);}
   
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}
uint8_t I2C::write16(uint8_t address, uint16_t registerAddress, const char *data)
{
  uint8_t bufferLength = strlen(data);
  uint8_t returnStatus;
  returnStatus = write16(address, registerAddress, (const uint8_t *)data, bufferLength);
  return (returnStatus);
}
uint8_t I2C::write16(uint8_t address, uint16_t registerAddress, const uint8_t *data, uint8_t numberBytes)
{
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
 if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
 
  //Send MSB of register address
  returnStatus = _sendByte(registerAddress >> 8);
   if (returnStatus){return send_error_handler(returnStatus);}
   
  //Send LSB of register address
  returnStatus = _sendByte(registerAddress & 0xFF);
   if (returnStatus){return send_error_handler(returnStatus);}
   
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    returnStatus = _sendByte(data[i]);
    if (returnStatus){return send_error_handler(returnStatus);}
  }
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}
uint8_t I2C::write16(uint8_t address, uint16_t registerAddress, uint16_t data)
{
  //Array to hold the 2 bytes that will be written to the register
  uint8_t writeBytes[2];
  uint8_t returnStatus;

  writeBytes[0] = (data >> 8) & 0xFF; //MSB
  writeBytes[1] = data & 0xFF;        //LSB

  returnStatus = write16(address, registerAddress, writeBytes, 2);
  return (returnStatus);
}

uint8_t I2C::write16(uint8_t address, uint16_t registerAddress, uint32_t data)
{
  //Array to hold the 4 bytes that will be written to the register
  uint8_t writeBytes[4];
  uint8_t returnStatus;

  writeBytes[0] = (data >> 24) & 0xFF; //MSB
  writeBytes[1] = (data >> 16) & 0xFF;
  writeBytes[2] = (data >> 8) & 0xFF;
  writeBytes[3] = data & 0xFF; //LSB

  returnStatus = write16(address, registerAddress, writeBytes, 4);
  return (returnStatus);
}

uint8_t I2C::write16(uint8_t address, uint16_t registerAddress, uint64_t data)
{
  //Array to hold the 8 bytes that will be written to the register
  uint8_t writeBytes[8];
  uint8_t returnStatus;

  writeBytes[0] = (data >> 56) & 0xFF; //MSB
  writeBytes[1] = (data >> 48) & 0xFF;
  writeBytes[2] = (data >> 40) & 0xFF;
  writeBytes[3] = (data >> 32) & 0xFF;
  writeBytes[4] = (data >> 24) & 0xFF;
  writeBytes[5] = (data >> 16) & 0xFF;
  writeBytes[6] = (data >> 8) & 0xFF;
  writeBytes[7] = data & 0xFF; //LSB

  returnStatus = write16(address, registerAddress, writeBytes, 8);
  return (returnStatus);
}
//These functions will be used to read from Slaves that take 16-bit addresses
uint8_t I2C::read16(uint8_t address, uint16_t registerAddress, uint8_t numberBytes)
{
  bytesAvailable = 0;
  bufferIndex = 0;
  numberBytes = min(numberBytes, MAX_BUFFER_SIZE);
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint8_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  
  //Send MSB of register address
  returnStatus = _sendByte(registerAddress >> 8);
   if (returnStatus){return send_error_handler(returnStatus);}
   
  //Send LSB of register address
  returnStatus = _sendByte(registerAddress & 0xFF);
   if (returnStatus){return send_error_handler(returnStatus);}
   
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, REPEATED_START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    data[i] = TWDR;
    bytesAvailable = i + 1;
    totalBytes = i + 1;
  }
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}
uint8_t I2C::read16(uint8_t address, uint16_t registerAddress, uint8_t numberBytes, uint8_t *dataBuffer)
{
  if (numberBytes == 0)
  {
    numberBytes++;
  }
  uint8_t nack = numberBytes - 1;
  uint8_t returnStatus;
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_W(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, TRANSMIT_MODE);}
  //Send MSB of register address
  returnStatus = _sendByte(registerAddress >> 8);
  if (returnStatus){return send_error_handler(returnStatus);}
   
  //Send LSB of register address
  returnStatus = _sendByte(registerAddress & 0xFF);
  if (returnStatus){return send_error_handler(returnStatus);}
   
  returnStatus = _start();
  if (returnStatus){return start_error_handler(returnStatus, START_MODE);}
  
  returnStatus = _sendAddress(SLA_R(address));
  if (returnStatus){return send_addr_error_handler(returnStatus, RECEIVER_MODE);}
  
  for (uint8_t i = 0; i < numberBytes; i++)
  {
    if (i == nack)
    {
      returnStatus = _receiveByte(0);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    else
    {
      returnStatus = _receiveByte(1);
      if(returnStatus){return receive_error_handler(returnStatus);}
    }
    dataBuffer[i] = TWDR;
  }
  returnStatus = _stop();
  if (returnStatus){return stop_error_handler(returnStatus);}
  
  return (returnStatus);
}

//////////// LOW-LEVEL METHODS (No need to use them if the device uses normal register protocal)
uint8_t I2C::_start()
{
  unsigned long startingTime = millis();
  TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
  while (!(TWCR & (1 << TWINT)))
  {
    if (!timeOutDelay)
    {
      continue;
    }
    if ((millis() - startingTime) >= timeOutDelay)
    {
      lockUp();
      return (1);
    }
  }
  if ((TWI_STATUS == START) || (TWI_STATUS == REPEATED_START))
  {
    return (0);
  }
  if (TWI_STATUS == LOST_ARBTRTN)
  {
    uint8_t bufferedStatus = TWI_STATUS;
    lockUp();
    return (bufferedStatus);
  }
  return (TWI_STATUS);
}

uint8_t I2C::_sendAddress(uint8_t i2cAddress)
{
  TWDR = i2cAddress;
  unsigned long startingTime = millis();
  TWCR = (1 << TWINT) | (1 << TWEN);
  while (!(TWCR & (1 << TWINT)))
  {
    if (!timeOutDelay)
    {
      continue;
    }
    if ((millis() - startingTime) >= timeOutDelay)
    {
      lockUp();
      return (1);
    }
  }
  if ((TWI_STATUS == MT_SLA_ACK) || (TWI_STATUS == MR_SLA_ACK))
  {
    return (0);
  }
  uint8_t bufferedStatus = TWI_STATUS;
  if ((TWI_STATUS == MT_SLA_NACK) || (TWI_STATUS == MR_SLA_NACK))
  {
    _stop();
    return (bufferedStatus);
  }
  else
  {
    lockUp();
    return (bufferedStatus);
  }
}

uint8_t I2C::_sendByte(uint8_t i2cData)
{
  TWDR = i2cData;
  unsigned long startingTime = millis();
  TWCR = (1 << TWINT) | (1 << TWEN);
  while (!(TWCR & (1 << TWINT)))
  {
    if (!timeOutDelay)
    {
      continue;
    }
    if ((millis() - startingTime) >= timeOutDelay)
    {
      lockUp();
      return (1);
    }
  }
  if (TWI_STATUS == MT_DATA_ACK)
  {
    return (0);
  }
  uint8_t bufferedStatus = TWI_STATUS;
  if (TWI_STATUS == MT_DATA_NACK)
  {
    _stop();
    return (bufferedStatus);
  }
  else
  {
    lockUp();
    return (bufferedStatus);
  }
}

uint8_t I2C::_receiveByte(uint8_t ack)
{
  unsigned long startingTime = millis();
  if (ack)
  {
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
  }
  else
  {
    TWCR = (1 << TWINT) | (1 << TWEN);
  }
  while (!(TWCR & (1 << TWINT)))
  {
    if (!timeOutDelay)
    {
      continue;
    }
    if ((millis() - startingTime) >= timeOutDelay)
    {
      lockUp();
      return (1);
    }
  }
  if (TWI_STATUS == LOST_ARBTRTN)
  {
    uint8_t bufferedStatus = TWI_STATUS;
    lockUp();
    return (bufferedStatus);
  }
  
  if(ack){
      if(TWI_STATUS == MR_DATA_ACK){return 0;}
  }else{
      if(TWI_STATUS == MR_DATA_NACK){return 0;}
  }
  
  return (TWI_STATUS);
}

uint8_t I2C::_receiveByte(uint8_t ack, uint8_t *target)
{
  uint8_t stat = I2C::_receiveByte(ack);
  if (stat)
  {
    *target = 0x00;
    return receive_error_handler(stat);
  }
  
  *target = TWDR;
  // I suppose that if we get this far we're ok
  return 0;
}

uint8_t I2C::_stop()
{
  unsigned long startingTime = millis();
  TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
  while ((TWCR & (1 << TWSTO)))
  {
    if (!timeOutDelay)
    {
      continue;
    }
    if ((millis() - startingTime) >= timeOutDelay)
    {
      lockUp();
      return (1);
    }
  }
  return (0);
}

/////////////// Private Methods ////////////////////////////////////////

void I2C::lockUp()
{
  TWCR = 0;                     //releases SDA and SCL lines to high impedance
  TWCR = _BV(TWEN) | _BV(TWEA); //reinitialize TWI
}

I2C I2c = I2C();

/*mode variable
  0 -  START_MODE (MT)
  1 -  REPEATED_START_MODE (MR)
*/
uint8_t I2C::start_error_handler(uint8_t returnStatus, uint8_t mode){
    if (returnStatus == 1)
    {
      switch (mode){
      case START_MODE:
        return (1);
      case REPEATED_START_MODE:
        return (4);
      }
    }
    return (returnStatus);
}

uint8_t I2C::stop_error_handler(uint8_t returnStatus){
    if (returnStatus == 1)
    {
      return (7);
    }
    return (returnStatus);
}

uint8_t I2C::receive_error_handler(uint8_t returnStatus){
    if (returnStatus == 1)
    {
      return (6);
    }
    return (returnStatus);
}

uint8_t I2C::send_error_handler(uint8_t returnStatus){
    if (returnStatus == 1)
    {
      return (3);
    }
    return (returnStatus);
}

/*mode variable
  0 -  TRANSMIT_MODE (MT)
  1 -  RECEIVER_MODE (MR)
*/
uint8_t I2C::send_addr_error_handler(uint8_t returnStatus, uint8_t mode){
    if (returnStatus == 1)
    {
      switch (mode){
      case TRANSMIT_MODE:
        return (2);
      case RECEIVER_MODE:
        return (5);
      }
    }
    return (returnStatus);
}


