/*

for the TODO list of remaining features/functions, see NT3H1x01_thijs.h (bottom of comment at the top)

This is a test of the NFC IC: NT3H1201
This sketch should serve to show some of the functions,
 HOWEVER, to truly understand this thing, please just read the preface of NT3H1x01_thijs.h (comment at the top).
In there, i've summarized the datasheet, because the actual datasheet is a scatter-brained mess: https://www.mouser.com/datasheet/2/302/NT3H1101_1201-1127167.pdf
Every function has a C++ documentation comment above it, so if your IDE is sufficiently modern, finding the right function shouldn't be that hard

*/

#include <Arduino.h>


// #define NT3H1x01_useWireLib  // force the use of Wire.h (instead of platform-optimized code (if available))

#define NT3H1x01debugPrint(x)  Serial.println(x)    //you can undefine these printing functions no problem, they are only for printing I2C errors
//#define NT3H1x01debugPrint(x)  log_d(x)  // ESP32 style logging

// #define NT3H1x01_unlock_burning

#include <NT3H1x01_thijs.h>

NT3H1x01_thijs NFCtag(  false  ); // initialize 1k variant


#ifdef ARDUINO_ARCH_ESP32  // on the ESP32, almost any pin can become an I2C pin
  const uint8_t NT3H1x01_SDApin = 26; // 'defualt' is 21 (but this is just some random value Arduino decided.)
  const uint8_t NT3H1x01_SCLpin = 27; // 'defualt' is 22 
#endif
#ifdef ARDUINO_ARCH_STM32   // on the STM32, each I2C peripheral has several pin options
  const uint8_t NT3H1x01_SDApin = SDA; // default pin, on the STM32WB55 (nucleo_wb55rg_p) that's pin PB9
  const uint8_t NT3H1x01_SCLpin = SCL; // default pin, on the STM32WB55 (nucleo_wb55rg_p) that's pin PB8
  /* Here is a handy little table of I2C pins on the STM32WB55 (nucleo_wb55rg_p):
      I2C1: SDA: PA10, PB7, PB9
            SCL: PA9, PB6, PB8
      I2C3: SDA: PB4, PB11, PB14, PC1
            SCL: PA7, PB10, PB13, PC0      */
#endif

#ifdef NT3H1x01_useWireLib // (currently) only implemented with Wire.h
  bool checkI2Caddress(uint8_t address) {
    Wire.beginTransmission(address);
    return(Wire.endTransmission() == 0);
  }

  void I2CdebugScan() {
    Serial.println("I2C debug scan...");
    for(uint8_t address = 1; address<127; address++) {
      if(checkI2Caddress(address)) {
        Serial.print("got ACK at address: 0x"); Serial.println(address, HEX);
      }
      delay(1);
    }
    Serial.println("scanning done");
  }
#endif

void setup() 
{
  // delay(2000);

  Serial.begin(115200);  delay(50); Serial.println();
  #ifdef NT3H1x01_useWireLib // the slow (but pretty universally compatible) way
    NFCtag.init(100000); // NOTE: it's up to the user to find a frequency that works well.
  #elif defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__) // TODO: test 328p processor defines! (also, this code may be functional on other AVR hw as well?)
    pinMode(SDA, INPUT_PULLUP); //A4    NOT SURE IF THIS INITIALIZATION IS BEST (external pullups are strongly recommended anyways)
    pinMode(SCL, INPUT_PULLUP); //A5
    NFCtag.init(100000); // your average atmega328p should do 800kHz
  #elif defined(ARDUINO_ARCH_ESP32)
//    pinMode(NT3H1x01_SDApin, INPUT_PULLUP); //not needed, as twoWireSetup() does the pullup stuff for you
//    pinMode(NT3H1x01_SCLpin, INPUT_PULLUP);
    esp_err_t initErr = NFCtag.init(100000, NT3H1x01_SDApin, NT3H1x01_SCLpin, 0); //on the ESP32 (almost) any pins can be I2C pins
    if(initErr != ESP_OK) { Serial.print("I2C init fail. error:"); Serial.println(esp_err_to_name(initErr)); Serial.println("while(1){}..."); while(1) {} }
    //note: on the ESP32 the actual I2C frequency is lower than the set frequency (by like 20~40% depending on pullup resistors, 1.5kOhm gets you about 800kHz)
  #elif defined(__MSP430FR2355__) //TBD: determine other MSP430 compatibility: || defined(ENERGIA_ARCH_MSP430) || defined(__MSP430__)
    // not sure if MSP430 needs pinMode setting for I2C, but it seems to work without just fine.
    NFCtag.init(100000); // TODO: test what the limit of this poor microcontroller are ;)
    delay(50);
  #elif defined(ARDUINO_ARCH_STM32)
    // not sure if STM32 needs pinMode setting for I2C
    NFCtag.init(100000, SDA, SCL, false); // TODO: test what the limits of this poor microcontroller are ;)
    /* NOTE: for initializing multiple devices, the code should look roughly like this:
      i2c_t* sharedBus = NFCtag.init(100000, SDA, SCL, false); // returns NFCtag._i2c (which is (currently) also just a public member, btw)
      secondNFCtag.init(sharedBus); // pass the i2c_t object (pointer) to the second device, to avoid re-initialization of the i2c peripheral
      //// repeated initialization of the same i2c peripheral will result in unexplained errors or silent crashes (during the first read/write action)!
    */
  #else
    #error("should never happen, NT3H1x01_useWireLib should have automatically been selected if your platform isn't one of the optimized ones")
  #endif

  #ifdef NT3H1x01_useWireLib
    I2CdebugScan();
    Serial.println(); // seperator
  #endif

  //// first, some basic checks. NOTE: will halt entire sketch if something's wrong
  if(!NFCtag.connectionCheck()) { Serial.println("NT3H1x01 connection check failed!");    while(1);    } else { Serial.println("connection good"); }
  Serial.print("UID: "); uint8_t UID[7]; NFCtag.getUID(UID); for(uint8_t i=0; i<7; i++) { Serial.print(UID[i], HEX); Serial.print(' '); } Serial.println();
  //if(!NFCtag.variantCheck()) { Serial.println("resetting CC to factory default..."); NFCtag.resetCC(); } // you can only change the CC bytes from I2C (not RF)
  Serial.print("CC as bytes: "); uint8_t CC[4]; NFCtag.getCC(CC); for(uint8_t i=0; i<4; i++) { Serial.print(CC[i], HEX); Serial.print(' '); }
  Serial.print("  should be: "); Serial.println(NT3H1x01_CAPA_CONT_DEFAULT_uint32_t[NFCtag.is2kVariant], HEX);
  if(!NFCtag.variantCheck()) { Serial.println("NT3H1x01 variant check failed!");    while(1);     } else  { Serial.println("variant good"); }
  Serial.println(); // seperator

  // //// address change test:
  // #ifdef NT3H1x01_useWireLib
  //   I2CdebugScan();
  //   uint8_t newAddress = NT3H1x01_DEFAULT_I2C_ADDRESS;
  //   Serial.print("\t attempting to change I2C address to: 0x"); Serial.println(newAddress, HEX);
  //   NFCtag.setI2Caddress(newAddress); delay(20);
  //   I2CdebugScan();
  //   if(!NFCtag.connectionCheck()) { Serial.println("post-address-change NT3H1x01 connection check failed!"); while(1);} else { Serial.println("post-address-change connection good"); }
  // #endif

  // //// ATQA & SAK tests:
  // Serial.print("getATQA: 0x"); Serial.println(NFCtag.getATQA(), HEX);
  // Serial.print("getSAK: 0x"); Serial.println(NFCtag.getSAK(), HEX);
  // Serial.println(); // seperator

  // //// Session register tests:
  // Serial.print("getSess_NC_REG: "); Serial.print(NFCtag.getSess_NC_REG(), BIN); Serial.print("  default: "); Serial.println(NT3H1x01_SESS_REGS_DEFAULT[0], BIN);
  // Serial.print("getSess_NC_I2C_RST: "); Serial.println(NFCtag.getSess_NC_I2C_RST());
  // Serial.print("getSess_NC_FD_OFF: "); Serial.println(NFCtag.getSess_NC_FD_OFF());
  // Serial.print("getSess_NC_FD_ON: "); Serial.println(NFCtag.getSess_NC_FD_ON());
  // Serial.print("getSess_NC_DIR: "); Serial.println(NFCtag.getSess_NC_DIR());
  // Serial.print("getSess_NC_PTHRU: "); Serial.println(NFCtag.getSess_NC_PTHRU());
  // Serial.print("getSess_NC_MIRROR: "); Serial.println(NFCtag.getSess_NC_MIRROR());
  // Serial.println(); // seperator
  // Serial.print("getSess_LAST_NDEF_BLOCK: 0x"); Serial.print(NFCtag.getSess_LAST_NDEF_BLOCK(), HEX); Serial.print("  default: 0x"); Serial.println(NT3H1x01_SESS_REGS_DEFAULT[1], HEX);
  // Serial.print("getSess_SRAM_MIRROR_BLOCK: 0x"); Serial.print(NFCtag.getSess_SRAM_MIRROR_BLOCK(), HEX); Serial.print("  default: 0x"); Serial.println(NT3H1x01_SESS_REGS_DEFAULT[2], HEX);
  // // Serial.print("getSess_WDTraw: 0x"); Serial.println(NFCtag.getSess_WDTraw(), HEX);
  // Serial.print("getSess_WDT: "); Serial.print(NFCtag.getSess_WDT()); Serial.println("us  default is ~20ms");
  // Serial.print("getSess_I2C_CLOCK_STR: "); Serial.print(NFCtag.getSess_I2C_CLOCK_STR()); Serial.print("  default: "); Serial.println(NT3H1x01_SESS_REGS_DEFAULT[5]);
  // Serial.println(); // seperator
  // Serial.print("getNS_REG: "); Serial.print(NFCtag.getNS_REG(), BIN); Serial.print("  default: "); Serial.println(NT3H1x01_SESS_REGS_DEFAULT[6], BIN);
  // Serial.print("getNS_NDEF_DATA_READ: "); Serial.println(NFCtag.getNS_NDEF_DATA_READ());
  // Serial.print("getNS_I2C_LOCKED: "); Serial.println(NFCtag.getNS_I2C_LOCKED());
  // Serial.print("getNS_RF_LOCKED: "); Serial.println(NFCtag.getNS_RF_LOCKED());
  // Serial.print("getNS_SRAM_I2C_READY: "); Serial.println(NFCtag.getNS_SRAM_I2C_READY());
  // Serial.print("getNS_SRAM_RF_READY: "); Serial.println(NFCtag.getNS_SRAM_RF_READY());
  // Serial.print("getNS_EEPROM_WR_ERR: "); Serial.println(NFCtag.getNS_EEPROM_WR_ERR());
  // Serial.print("getNS_EEPROM_WR_BUSY: "); Serial.println(NFCtag.getNS_EEPROM_WR_BUSY());
  // Serial.print("getNS_RF_FIELD_PRESENT: "); Serial.println(NFCtag.getNS_RF_FIELD_PRESENT());
  // Serial.println(); // seperator

  // //// Configuration register tests:
  // Serial.print("getConf_NC_REG: "); Serial.print(NFCtag.getConf_NC_REG(), BIN); Serial.print("  default: "); Serial.println(NT3H1x01_CONF_REGS_DEFAULT[0], BIN);
  // Serial.print("getConf_NC_I2C_RST: "); Serial.println(NFCtag.getConf_NC_I2C_RST());
  // Serial.print("getConf_NC_FD_OFF: "); Serial.println(NFCtag.getConf_NC_FD_OFF());
  // Serial.print("getConf_NC_FD_ON: "); Serial.println(NFCtag.getConf_NC_FD_ON());
  // Serial.print("getConf_NC_DIR: "); Serial.println(NFCtag.getConf_NC_DIR());
  // Serial.println(); // seperator
  // Serial.print("getConf_LAST_NDEF_BLOCK: 0x"); Serial.print(NFCtag.getConf_LAST_NDEF_BLOCK(), HEX); Serial.print("  default: 0x"); Serial.println(NT3H1x01_CONF_REGS_DEFAULT[1], HEX);
  // Serial.print("getConf_SRAM_MIRROR_BLOCK: 0x"); Serial.print(NFCtag.getConf_SRAM_MIRROR_BLOCK(), HEX); Serial.print("  default: 0x"); Serial.println(NT3H1x01_CONF_REGS_DEFAULT[2], HEX);
  // // Serial.print("getConf_WDTraw: 0x"); Serial.println(NFCtag.getConf_WDTraw(), HEX);
  // Serial.print("getConf_WDT: "); Serial.print(NFCtag.getConf_WDT()); Serial.println("us  default is ~20ms");
  // Serial.print("getConf_I2C_CLOCK_STR: "); Serial.print(NFCtag.getConf_I2C_CLOCK_STR()); Serial.print("  default: "); Serial.println(NT3H1x01_CONF_REGS_DEFAULT[5]);
  // Serial.println(); // seperator
  // Serial.print("getREG_LOCK: "); Serial.print(NFCtag.getREG_LOCK(), BIN); Serial.print("  default: "); Serial.println(NT3H1x01_CONF_REGS_DEFAULT[6], BIN);
  
  //// Reading/Writing to the bulk memory (a.k.a. user-memory):
  //// NOTE: some more user-friendly functions for this are on the way!
  //// The User-memory stretches from blocks 0x01 to (the first half of) 0x38 on the 1k version, and to 0x77 on the 2k version.
  //// the functions (not yet user-friendly) to access this memory should be something like:
  //requestMemBlock(blockAddress, yourBuffHere);     which is the same as    _getBytesFromBlock(blockAddress, 0, NT3H1x01_BLOCK_SIZE, yourBuffHere);    for reading blocks
  //writeMemBlock(blockAddress, yourBuffHere);       which is the same as      _setBytesInBlock(blockAddress, 0, NT3H1x01_BLOCK_SIZE, yourBuffHere);    for writing blocks
  //// to read/write WHOLE blocks of memory efficiently, just make sure your readBuff/writeBuff is the size of NT3H1x01_BLOCK_SIZE. The _getBytes...() functions will recognize this and skip the _oneBlockBuff
  //// to indicate the size of the contents of the user-memory, the LAST_NDEF_BLOCK is used.
  //// user-friendly LAST_NDEF_BLOCK function(s) are still TBD, but setSess_LAST_NDEF_BLOCK, getSess_LAST_NDEF_BLOCK, setConf_LAST_NDEF_BLOCK and getConf_LAST_NDEF_BLOCK should be working.
  //// also:
  //// Filesystem / SD-card streaming are still TBD (, but i would very much like to be able to transfer large files more easily, and that filesystems (ESP32,STM32?) or SD-cards seem like a handy way to do that)
  
  //// many of these user-friendly functions are somewhat inefficient.
  //// Especially, those that change one small part of a block of memory, which require the entire block to be fetched first (to avoid unintentionally changing other data in the same block).
  //// For this reason, i've added 'useCache' to a lot of functions,
  ////  which lets you potentially skip a read interaction, by using the cached memory block instead. NOTE: this does not apply for session registers, which use masked-single-byte interactions by definition.
  //// HOWEVER, this is very susceptible to (user) error, and should only be used if you are sure of what you're doing.
  //// The _oneBlockBuff (class member) is the last block of memory the class interacted with, and _oneBlockBuffAddress indicates what memory block (address) the cache (supposedly) holds.
  //// There are NO checks for how old the cache is, and it is entirely possible that the RF interface changes stuff in memory, making the cache invalid (this is NOT checked (checking is not possible (efficiently)))
  //// here is an example of a reasonable usage of the useCache functionality:
  // NFCtag.getCC(CC); // first command should (almost) never useCache (because we don't know how long it's)
  // NFCtag.getUID(UID, true); // commands immidietly following another, where the contents share a memory block (UID and CC are both found in block 0x00), can use the cache effectively
  //// the code above is especially permissable, because the UID is Read-only, and therefore pretty unlikely to become desynchronized (by the RF side, for example)

  //// memory access arbitration and the WatchDog Timer (IMPORTANT):
  //// the RF and I2C interfaces cannot access the EEPROM of the tag at the same time, so there are _LOCKED flags in place (basically semaphore flags).
  //// after your I2C interactions are completed, the polite thing is to clear the I2C_LOCKED flag (in the NS_REG), using setNS_I2C_LOCKED(false);
  //// HOWEVER, the designers of this IC accounted for lazy people forgetting to clear the flag, and implemented a WatchDog Timer as well.
  //// The WDT will clear the flag for you, after a certain period of I2C inactivity (or after the first I2C START condition, it's not clearly specified in the datasheet)
  //// To set the WDT time, use setSess_WDT(float) or setSess_WDTraw(uint16_t).

  //// Configuration saving:
  //// If you are content with your settings, but don't like specifying them on every Power-On-Reset,
  ////  you can save the active settings (Session registers) to EEPROM (Configuration regisers) with the function saveSessionToConfiguration()
  //// if you are REALLY sure of yourself, and absolutely distrusting of people-with-access-to-the-RF-interface, you can (permanently!!!) disable writing Conf. registers from RF, using burnRegLockRF()
  ////  furthermore, if you are even distrustfull of people-with-access-to-the-I2C-interface, you can (permanently!!!) disable writing Conf. registers from I2C, using burnRegLockI2C()
  //// for obvious reasons, these One-Time-Program functions are not available in this library UNLESS, you #define NT3H1x01_unlock_burning

  //// preventing RF write-access:
  //// Unfortunately, the functions for Static Locking and Dynamic Locking are currently unfinished, but I will hint that that is the way to do it ;)

  //// Pass-Through mode and Memory-Mirror mode functions are still TBD.
  //// (if you really want it enough, the low-level functions are in place, you would just need to read the datasheet thurroughly. If you do so before I do, please tell me on Github :) @ https://github.com/thijses )

  //// if functionality is still missing, or if you just have some ideas for improvements you'd like to see, feel free to contact me: https://github.com/thijses  or  tttthijses@gmail.com
}

void loop()
{
  if(NFCtag.getNS_RF_FIELD_PRESENT()) { // loop demonstration is still TBD, but this will at least show whether there IS a card present or not (much like the FD pin)
    Serial.println("field detected!");
  }
  delay(25);
}