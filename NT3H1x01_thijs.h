/*

The IC has 2 means of communicating, RF/NFC and I2C.
both the RF interface (the actual NFC functionality) and the I2C interface are all about memory access.
There are 2 memory maps, with some significant overlapping areas, which can change depending on the mode (normal, pass-through, mirror)
for the comprehensive (and suprisingly legible) memory map tables, please look at page 12/13 (RF) and 14/15 (I2C) of the datasheet.
the datasheet talks about blocks (16 bytes) and pages (4 bytes). The I2C interface uses in blocks, the RF interface uses sectors and pages.

The IC has 2 types of memory; EEPROM and SRAM (see page 16 of the datasheet)
by default, the RF interface only has access to the EEPROM, but with Memory-Mirror mode or Pass-Through modes, it can access SRAM as well.
The EEPROM has a limited number of write(/read) cycles, so to keep that in mind
The SRAM has "unlimited" write/read cycles, but is only 64 bytes

The IC has a number of features/contents (which i explain in more detail further below):
- I2C address changing (by NFC host or I2C master)
- Serial number
- Static & Dynamic locking: turns memory to read-only (can only be set/reset by I2C host)
- Capability Container (CC): set (factory-programmed) according to the 'NFC Forum Type 2 Tag specification', used to indicate memory version, memory size and R/W access
- Configuration and Session registers: communication settings. These values are loaded into the session registers on Power-On-Reset (Conf. is stored in EEPROM, Sess. is read-only for RF)
- Pass-through mode: lets the NFC host and I2C master talk (almost) directly (through a 64byte buffer)
- Memory-Mirror mode: lets you map the (64byte) SRAM overtop a part of the EEPROM, to save write(/read) cycles
- Field Detection pin: can be set to react to certain events with a rising and/or falling edge. Pretty nice if you want to save I2C bus time (at the cost of 1 GPIO pin)
- Watchdog timer: in case the I2C master forgets to indicate that it's done using the memory, the WDT will clear the I2C_LOCKED flag (letting the RF access the memory)

The I2C interface of the NT3H is somewhat complex
The I2C address can be changed (only by I2C master) but by default it's:
0b1010101x = 0xAA / 0xAB  including the R/W bit,
0b01010101 = 0x55  in the unshifted (conventional) way.
you can modify the I2C address, but only through the I2C address (so you can't really have 2 devices on 1 bus).
the I2C clock freq can be 100kHz or 400kHz (not sure about arbetrary values inbetween or below)

Both read and write operations are done in 16 byte (not bit!) blocks, with a MEMory Address (MEMA) byte prefix
meaning that there are 256 blocks of 16 bytes, for a total of 4096 bytes of addressable space.
for (normal) operations, the read/write structure must look like:
 for reads: you send the memory block address, then a stop and start conditions, then read 16 bytes
 for writes:  you send the memory block address, then 16 bytes of data
ONLY the registers block is accessed in the following 1-byte fashion:
 for reads: you send the 0xFE block, then 1 byte (which of the register bytes (0~7) you want), then a stop and start conditions, then read 1 byte
 for writes:  you send the 0xFE block, then 1 byte (which of the register bytes (0~7) you want), then a bitmask for which bits to affect, then the new register byte.
  (the mask byte is nice, cuz then you don't have to read the byte first, you can just exclusively target the settings you want without affecting others)
The IC includes an optional soft-reset feature, if you perform a repeated-start. The default is 0 (presumably that means disabled), so repeated starts don't reset the IC

Communication and arbitration between RF and I²C interface:
both the RF interface (the actual NFC functionality) and the I2C interface are all about memory access.
 However, ONLY 1 interface can have access at-a-time.
So, the I2C code should check whether the RF interface is using the memory, by checking RF_LOCKED bit in the NS_REG register



feature descriptions:

Serial Number (UID):
(the Serial Number bytes are found at (I2C)block:0x00,bytes:0~6  == (RF)sector:0,page:0x00,byte:0~page:0x01,byte:6 )
the serial number is a 7byte value at memory address 0x00~0x07, the first byte of which is always the NXP manufacturer ID, 0x04.
 This first constant byte SHARES a memory address with the I2C address byte, so reads and writes of that byte are entirely unrelated

ATQA and SAK:
(the SAK byte is found at (I2C)block:0x00,byte:7  == (RF)sector:0,page:0x01,byte:3 )
(the ATQA bytes are found at (I2C)block:0x00,bytes:8~9  == (RF)sector:0,page:0x02,bytes:0~1 )
These are some value to help identify the card (in this tag's case only(?) the Serial Number size)
Bits 6 and 7 (starting at 0, not 1) indicate the size of the Serial Number (7 bytes in this tag).
See page 40 of the datasheet for slightly more info (but not a lot... it's not exactly well-documented)

Static Locking (and block-locking):
(the static locking bytes are found at (I2C)block:0x00,bytes:10~11  == (RF)sector:0,page:0x02,bytes:2~3 )
memory space (I2C)block:0x01  == (RF)sector:0,page:0x03~0x0F can be locked as read-only memory using the Static locking bytes. (NOTE: total of 52 bytes)
(the rest of the memory can be locked using Dynamic Locking)
using bits[7:3] of the first static-lock-byte and bits[7:0] of the second byte, memory pages can be locked.
However, the static locking bytes themselves can also be locked ('block-locking'), by bits [2:0] of the first static-lock-byte.
this secondary locking seems a little excessive to me, personally, but whatever.
The RF interface cannot set locking bits back to 0, so Static Locking is a way for the I2C interface to limit the NFC host
For details on which bits block which memory pages, please refer to the helpful infograpic on page 17 of the datasheet

Dynamic Locking (and block-locking):
(the Dynamic locking bytes are found at (I2C)block:0x38/0x78,bytes:0~2  == (RF)sector:1,page:0xE2/0xE0,bytes:0~2)
Dynamic locking is the same as Static locking, just on a larger scale. It also has block-locking, for which the whole 3rd byte is used.
memory space to be locked as read-only memory using the Dynamic locking bytes. (NOTE: total of 840/1856) (datasheet has typo):
  on 1k variant: (I2C)block:0x02~block:0x56,bytes:0~7  == (RF)sector:0,page:0x10~0xE1                   == 210 pages == 52 blocks
  on 2k variant: (I2C)block:0x02~0x77                  == (RF)sector:0,page:0x10~sector:1,page:0xDF     == 464 pages == 116 blocks
(the first few bytes of the memory can be locked using Static Locking)
Usage is also very similar to Static Locking,
 EXCEPT that certain bits "marked with RFUI" must be set to 0 at all times.
Lastly, reading the 3rd byte will always return 0. No reason why, it's just true
For details on which bits block which memory pages, please refer to the helpful infograpic on page 18/19 of the datasheet

Capability Container (CC):  (also mentioned as 'NDEF management' in official NFC tag specification documents)
these are some bytes, defined by the NFC forum to help indicate what this particular tag can/can't do.
byte:0 is an absolute constant 0xE1  (to identify it as an official NFC forum complient card)
byte:1 indicates the version of the NFC specification the tag is built for.
  This particular IC uses version 0x10 (MSB nibble = major version, LSB nibble = minor version, V1.0 == 0b 0001 0000 )
byte:2 indicates the memory size of the tag (roughly?), multiply this byte with 8 to get size in bytes
byte:3 indicates read/write access.
  The MSB nibble is read access: 0x0 is default (unprotected), 0x1~7 are 'reserved for future use', 0x8~E are 'proprietary', 0xF i don't know
  The LSB nibble is write access: 0x_0 is default (unprotected), 0x_1~7 are 'reserved for future use', 0x_8~E are 'proprietary', 0x_F means NO WRITE ACCESS at all
the RF host can set certain bits, but it can't clear them (i think?). The I2C interface has more control (i think.)
There is no real need to mess with these bytes, except for indicating a lack of write-access (by writing 0x0F to the last byte)

Configuration and Session registers:
(the Configuration registers are found at (I2C)block:0x3A/0x7A,bytes:0~7  == (RF)sector:0/1,page:0xE8~0xE9)
(the Session registers are found at (I2C)block:0xFE,bytes:0~7  == (RF)sector:3,page:0xF8~0xF9) AND can only be accessed using a special read/write format (see pages 37~38 of datasheet)
Configuration registers are stored in EEPROM, and loaded into the Session registers at Power-On-Reset
Here is a list of the registers and their functions: {text in curvy{} brackets means ONLY for Session regs.}, [text in square[] brackets means only for Configuration regs.]
- NC_REG: I2C soft reset, {Pass-Through mode}, FD pin output, {Memory-Mirror mode}, data transfer direction (in Pass-Through mode) or RF write access (when not in Pass-Through mode)
- LAST_NDEF_BLOCK: address of last block (== 16 bytes == 4 pages) of user-memory that holds actual data. You set this to limit size that the RF host reads. (it basically indicates memory fullness)
- SRAM_MIRROR_BLOCK: address of first block of user-memory to be replaced (mapped over) by SRAM when Mirror-Mode is enabled
- WDT_LS and WDT_MS: watchdog time control register (NOTE: write _LS first??, becuase _MS activates something, i think)
- I2C_CLOCK_STR: I2C clock stretching enable/disable
- [REG_LOCK: disallow RF and/or I2C to write to the configuration bytes, NOTE: these bits BURN IN PERMANENTLY]
- {NS_REG: status flags for: RF done reading, memory arbitration (I2C vs RF), Pass-Through mode buffers filled, EEPROM write error or busy, RF field present}

Pass-Through mode explenation:
(Memory-Mirror must be OFF and requires external VCC)
when pass-through mode is enabled, the RF interface (NFC host) has access to the SRAM instead of(?) the EEPROM
the I2C interface always has access to the SRAM, so (by carefully coordinating reads and writes,) full custom communication between the RF host and I2C master is possible,
 using this IC's SRAM as a buffer inbetween. The SRAM buffer is 16 pages of 4 bytes, a.k.a. 64 bytes, which encompasses the entire SRAM block
using the SRAM requires external VCC (not only power harvesting), and must be re-enabled after every Power-On-Reset
pages 55~57 of the datasheet have some nice visuals for the state of the arbitration bits during pass-through mode.
...

Memory-Mirror mode explenation:
(pass-through must be OFF and requires external VCC)
While this mode is enabled, the SRAM is mapped overtop of a chunk of user-memory.
Bascially, if you want to vary one 64 byte chunk of the EEPROM very often, uses this mode.
The RF interface (and the I2C interface, i think) will interact with the SRAM instead of the EEPROM for a 64 byte chunk, thereby saving you precious EEPROM write(/read) cycles.
you can 'place' this chunk of anywhere in user-memory that you want, using SRAM_MIRROR_BLOCK in the Conf./Sess. registers

Field Detection pin: (they consider it 'active' LOW)
the FD pin can be activated (fall) by:
- RF field present
- start of communication (presumably triggers before anticollision (multiple tags)????)
- this tag being selected (not sure what the difference between this and communication start are...)
- (in Pass-Through mode) data in buffer: ready-to-be-read-by-I2C OR has-been-read-by-RF
and it can be deactivated (rise) by:
- RF field gone
- (RF field gone or) tag is set to HALT state
- (RF field gone or) last block of data has been read (defined by LAST_NDEF_BLOCK)
- (in Pass-Through mode and FD_ON set to indicate data buffer ready) (RF field gone or) data in buffer: has-been-read-by-I2C OR ready-to-be-read-by-RF

Watchdog timer:
(requires external VCC))
When the I2C master is using the memory, the I2C_LOCKED flag is TRUE
HOWEVER, the I2C master should also clear this flag once it's done all it wanted to.
So, in case the I2C master forgets to clear the flag, the WDT will do it for you.
The resolution is 9.43us, and the default value = 0x0848 == 2120*9.43us =~ 20ms
the maximum WDT value is 0xFFFF == 65535*9.43 =~ 618ms
if the WDT is triggered (timer is done), but the I2C interface is still actively communicating, it will clear the flag immediately afterwards

Energy harvesting:
The datasheet claims this IC can typically provide 5mA at 2V on the VOUT pin (when the NFC host is a phone).


the 2kb EEPROM can come from several places
file system (R/W) capable MCUs:
- ESP32
- STM32?
SD card (R/W) capable MCUs:
- all
big ass array (write only) capable MCUs:
- all (except MSP430 has very little ROM)
I'd like to make functions that properly encompass all of these, including:
suspending the WDT while writing such big files,
setting the LAST_NDEF_BLOCK to match the size of the loaded contents,



TO check out:
- responds to I2C general call?
- does checking the SRAM_xxx_READY flags clear them?
- FD pin start of comm vs tag selection
- soft reset function

TODO (general):
- connection check and I2C address change(?!?)
- continue writing missing functions (see list below)
- HW testing (STM32)
- HW testing (ESP32)
- HW testing (MSP430)
- HW testing (328p)
- block writing/reading to/from file/flash/EEPROM/idk   (2kB of data coming from somewhere, going to somewhere!)
- check Wire.h function return values for I2C errors
- test if 'static' vars in the ESP32 functions actually are static (connect 2 sensors?)

TO write (specific functions):
- set/get Session registers
- set/get configuration registers AND copy session registers data to configuration registers (store it in EEPROM)
- set/get watchdog timer setting (easy)
- set/get FD pin function
- (file writing funtion (mostly setting LAST_NDEF_BLOCK))
- static lock functions (_setStaticLockBits, staticLockPage, _staticBlockLockPageToChunk, staticBlockLockChunks)
- dynamic lock functions (_setDynamicLockBits, _dynamicLockPageToChunk, dynamicLockChunks, _dynamicBlockLockPageToChunk, dynamicBlockLockChunks)
- generalized lock function: lockArea(startBlock,endBlock,roundUp=true)
- Memory-Mirror mode functions
- Pass-Through mode functions
- check ATQA bytes to verify UID size

*/

// put datasheet link here!

#ifndef NT3H1x01_thijs_h
#define NT3H1x01_thijs_h

#include "Arduino.h"


//#define NT3H1x01_unlock_burning   // enable the permanent chip-burning features of the NT3H1x01 (e.g. REG_LOCK)

//#define NT3H1x01debugPrint(x)  Serial.println(x)
//#define NT3H1x01debugPrint(x)  log_d(x)   //using the ESP32 debug printing

#ifndef NT3H1x01debugPrint
  #define NT3H1x01debugPrint(x)  ;
#endif


//// NT3H1x01 constants:
// memory block address ranges (MEMory Address values), (16 Byte blocks of memory)
// #define NT3H1x01_EEPROM_START 0x00  // EEPROM memory start MEMA
// #define NT3H1101_EEPROM_END   0x3A  // EEPROM memory end MEMA for the 1k variant
// #define NT3H1201_EEPROM_END   0x7A  // EEPROM memory end MEMA for the 2k variant
// #define NT3H1x01_SRAM_START   0xF8  // SRAM memory start MEMA
// #define NT3H1x01_SRAM_END     0xFB  // SRAM memory end MEMA

#define NT3H1x01_BLOCK_SIZE 16 // the I2C interface only deals in blocks of 16 bytes (except for Session registers)

#define NT3H1x01_I2C_ADDR_CHANGE_MEMA 0x00      // I2C address change memory block
#define NT3H1x01_I2C_ADDR_CHANGE_MEMA_BYTE  0   // I2C address change covers byte 0 only (write only)

#define NT3H1x01_SERIAL_NR_MEMA 0x00            // Serial Number memory block
#define NT3H1x01_SERIAL_NR_MEMA_BYTES_START 0   // Serial Number covers bytes 0~6 (read only)
#define NT3H1x01_SERIAL_NR_NXP_MF_ID 0x04       // Serial Number manufacturer number (of NXP). Should be the first byte of UID on all NXP tags

#define NT3H1x01_SAK_MEMA 0x00                  // SAK memory block
#define NT3H1x01_SAK_MEMA_BYTE         7        // SAK covers byte 7 (read only)
#define NT3H1x01_ATQA_MEMA 0x00                 // ATQA memory block
#define NT3H1x01_ATQA_MEMA_BYTES_START 8        // ATQA covers bytes 8~9 (read only)

#define NT3H1x01_CAPA_CONT_MEMA 0x00            // Capability Container memory block
#define NT3H1x01_CAPA_CONT_MEMA_BYTES_START 12  // Capability Container covers bytes 12~15
static const uint32_t NT3H1x01_CAPA_CONT_DEFAULT_uint32_t[2] = {0xE1106D00, 0xE110EA00};  // Capability Container factory default value (as uint32_t) for the {1k, 2k} variant
static const uint8_t  NT3H1x01_CAPA_CONT_DEFAULT[2][4] = {{0xE1,0x10,0x6D,0x00}, {0xE1,0x10,0xEA,0x00}}; // Capability Container factory default value (as individual bytes) for the {1k, 2k} variant

#define NT3H1x01_STAT_LOCK_MEMA 0x00            // Static Locking bytes memory block
#define NT3H1x01_STAT_LOCK_MEMA_BYTES_START 10  // Capability Container covers bytes 10~11

#define NT3H1101_DYNA_LOCK_MEMA 0x38            // Dynamic Locking bytes memory block for the 1k variant
#define NT3H1201_DYNA_LOCK_MEMA 0x78            // Dynamic Locking bytes memory block for the 2k variant
#define NT3H1101_DYNA_LOCK_RFUI_bits 0xFF0F3F00   // Dynamic Locking RFUI mask for the 1k variant (RFU = Reserved for Future Use)
#define NT3H1201_DYNA_LOCK_RFUI_bits 0xFF7FFF00   // Dynamic Locking RFUI mask for the 2k variant

#define NT3H1101_CONF_REGS_MEMA 0x3A            // Configuration registers memory block for the 1k variant
#define NT3H1201_CONF_REGS_MEMA 0x7A            // Configuration registers memory block for the 2k variant
#define NT3H1x01_SESS_REGS_MEMA 0xFE            // Session registers memory block (NOTE: can only be accessed using special read/write operation, see pages 37~38 of datasheet)

enum NT3H1x01_CONF_SESS_REGS : uint8_t { // you could also do this with just defines, but it's slightly fancier this way
//// configuration/session registers common:
  NT3H1x01_COMN_REGS_NC_REG_BYTE = 0,  // NC_REG register location in the conf/sess. registers
  NT3H1x01_COMN_REGS_LAST_NDEF_BLOCK_BYTE = 1,  // LAST_NDEF_BLOCK register location in the conf/sess. registers
  NT3H1x01_COMN_REGS_SRAM_MIRROR_BLOCK_BYTE = 2,  // SRAM_MIRROR_BLOCK register location in the conf/sess. registers
  NT3H1x01_COMN_REGS_WDT_LS_BYTE = 3,  // WDT_LS register location in the conf/sess. registers
  NT3H1x01_COMN_REGS_WDT_MS_BYTE = 4,  // WDT_MS register location in the conf/sess. registers
  NT3H1x01_COMN_REGS_I2C_CLOCK_STR_BYTE = 5,  // I2C_CLOCK_STR register location in the conf/sess. registers
//// configuration registers only:
  NT3H1x01_CONF_REGS_REG_LOCK_BYTE = 6,  // REG_LOCK register location in the conf/sess. registers
//// session registers only:
  NT3H1x01_SESS_REGS_NS_REG_BYTE = 6  // NS_REG register location in the conf/sess. registers
};

//// NC_REG common:
#define NT3H1x01_NC_REG_I2C_RST_bits  0b10000000 // I2C_RST_ON_OFF enables soft-reset through repeated starts in I2C communication (very cool, slighly niche)
#define NT3H1x01_NC_REG_FD_OFF_bits   0b00110000 // FD_OFF determines the behaviour of the FD pin (falling)
#define NT3H1x01_NC_REG_FD_ON_bits    0b00001100 // FD_ON determines the behaviour of the FD pin (rising)
#define NT3H1x01_NC_REG_DIR_bits      0b00000001 // TRANSFER_DIR/PTHRU_DIR determines the direction of data in Pass-Through mode, or can disable RF write-access otherwise
//// NC_REG Session only:
#define NT3H1x01_NC_REG_PTHRU_bits    0b01000000 // PTHRU_ON_OFF enables Pass-Through mode
#define NT3H1x01_NC_REG_MIRROR_bits   0b00000010 // SRAM_MIRROR_ON_OFF enables Memory-Mirror mode
//// REG_LOCK (Configuration only): (there are one-time-program bits, a.k.a. burn bits, and can be set to 1 ONLY ONCE, disabling the changing of the Configuration bytes forever)
#define NT3H1x01_NC_REG_LOCK_I2C_bits 0b00000010 // PTHRU_ON_OFF enables Pass-Through mode
#define NT3H1x01_NC_REG_LOCK_RF_bits  0b00000001 // SRAM_MIRROR_ON_OFF enables Memory-Mirror mode
//// NS_REG (Session only):
#define NT3H1x01_NS_REG_NDEF_READ_bits  0b10000000 // NDEF_DATA_READ flag is 1 once the RF interface has read the data at address LAST_NDEF_BLOCK (if set). Reading clears flag
#define NT3H1x01_NS_REG_I2C_LOCKED_bits 0b01000000 // I2C_LOCKED is 1 if I2C has control of memory (arbitration). Should be cleared once the I2C interaction is completely done, may be cleared by WDT
#define NT3H1x01_NS_REG_RF_LOCKED_bits  0b00100000 // RF_LOCKED is 1 if RF has control of memory (arbitration)
#define NT3H1x01_NS_REG_PTHRU_IN_bits   0b00010000 // SRAM_I2C_READY is 1 if data is ready in SRAM buffer to be READ by I2C (i'm not sure if checking this flag changes it)
#define NT3H1x01_NS_REG_PTHRU_OUT_bits  0b00001000 // SRAM_RF_READY is 1 if data is ready in SRAM buffer to be READ by RF (the I2C should not need to check this flag, and i'm not sure if checking it clears it)
#define NT3H1x01_NS_REG_EPR_WR_ERR_bits 0b00000100 // EEPROM_WR_ERR is 1 if there was a (High Voltage?) error during EEPROM write. Flag needs to be manually cleared
#define NT3H1x01_NS_REG_EPR_WR_BSY_bits 0b00000010 // EEPROM_WR_BUSY is 1 if EEPROM writing is in progress (access is disabled while writing)
#define NT3H1x01_NS_REG_RF_FIELD_bits   0b00000001 // RF_FIELD_PRESENT is 1 if an RF field is detected


#include "_NT3H1x01_thijs_base.h" // this file holds all the nitty-gritty low-level stuff (I2C implementations (platform optimizations))
/**
 * An I2C interfacing library for the NT3H1x01 NFC IC
 * 
 */
class NT3H1x01_thijs : public _NT3H1x01_thijs_base
{
  //private:
  uint8_t _oneBlockBuff[NT3H1x01_BLOCK_SIZE]; // used for user-friendly functions. Initialized here just to (slightly) reduce overhead of repeatedly initializing and cleaning up read buffers
  public:
  using _NT3H1x01_thijs_base::_NT3H1x01_thijs_base;
  /*
  This class only contains the higher level functions.
   for the base functions, please refer to _NT3H1x01_thijs_base.h
  here is a brief list of all the lower-level functions:
  - init()
  - requestMemBlock()
  - requestSessRegByte()
  - _onlyReadBytes()
  - writeMemBlock()
  - writeSessRegByte()
  */
  //// the following functions are abstract enough that they'll work for either architecture
  
  /**
   * (just a macro) check whether an NT3H1x01_ERR_RETURN_TYPE (which may be one of several different types) is fine or not 
   * @param err (bool or esp_err_t or i2c_status_e, see on defines at top)
   * @return whether the error is fine
   */
  bool _errGood(NT3H1x01_ERR_RETURN_TYPE err) {
    #if defined(NT3H1x01_return_esp_err_t)
      return(err == ESP_OK);
    #elif defined(NT3H1x01_return_i2c_status_e)
      return(err == I2C_OK);
    #else
      return(err);
    #endif
  }
  
  // I'd love to template these _get and _set functions with some kind of pre-processor directive that makes the debugPrint message include the name of the individual function (efficiently)
  // but alas, i have yet to determine how one would do such a thing (in an even remotely legible way)
  /**
   * (private) retrieve an arbetrary set of bytes (into a provided buffer) from a block
   * @param blockAddress MEMory Address (MEMA) of the block
   * @param bytesInBlockStart where in the block the relevant data starts (see defines up top)
   * @param bytesToRead how many bytes are of interest (size of readBuff)
   * @param readBuff buffer of size (bytesToRead) to put the results in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE _getBytesFromBlock(uint8_t blockAddress, uint8_t bytesInBlockStart, uint8_t bytesToRead, uint8_t readBuff[]) {
    if((bytesInBlockStart+bytesToRead) > NT3H1x01_BLOCK_SIZE) { NT3H1x01debugPrint("_getBytesFromBlock() MISUSE!, you're trying to read bytes outside of the buffer"); /* return(err) */ // TODO: find appropriate error
        bytesInBlockStart=(bytesInBlockStart%NT3H1x01_BLOCK_SIZE); bytesToRead=min((uint8_t)(NT3H1x01_BLOCK_SIZE-bytesInBlockStart), bytesToRead); } // safety measures instead of return()
    NT3H1x01_ERR_RETURN_TYPE err = requestMemBlock(blockAddress, _oneBlockBuff); // fetch the whole block
    if(!_errGood(err)) { NT3H1x01debugPrint("_getBytesFromBlock() read/write error!"); return(err); }
    for(uint8_t i=0; i<bytesToRead; i++) { readBuff[i] = _oneBlockBuff[i+bytesInBlockStart]; }
    return(err); // err should always be OK, if it makes it to this point
  }
  /**
   * (private) retrieve an arbetrary set of bytes from a block and force into a (little-endian) value of class T
   * @param blockAddress MEMory Address (MEMA) of the block
   * @param bytesInBlockStart where in the block the relevant data starts (see defines up top)
   * @param littleEndian true for almost all interactions, but ATQA needs false, so there's the option
   * @return value (or garbage, depending on the unkowable success of the write/read)
   */
  template<typename T> 
  T _getValFromBlock(uint8_t blockAddress, uint8_t bytesInBlockStart, bool littleEndian=true) {
    T returnVal;
    if((bytesInBlockStart+sizeof(T)) > NT3H1x01_BLOCK_SIZE) { NT3H1x01debugPrint("_getValFromBlock() MISUSE!, you're trying to read bytes outside of the buffer"); /* return(err) */ } // TODO: find appropriate error
    NT3H1x01_ERR_RETURN_TYPE err = requestMemBlock(blockAddress, _oneBlockBuff); // fetch the whole block
    if(!_errGood(err)) { NT3H1x01debugPrint("_getValFromBlock<>() read/write error!"); return(returnVal); } // note: returns random value, as returnVal was not zero-initialized
    uint8_t* bytePtrToReturnVal = (uint8_t*) &returnVal;
    for(uint8_t i=0; i<sizeof(T); i++) { bytePtrToReturnVal[littleEndian ? (sizeof(T)-1-i) : i] = _oneBlockBuff[i+bytesInBlockStart]; } // (little-endian)
    return(returnVal);
  }

  /**
   * (private) overwrite an arbetrary set of bytes in a block (with bytes from a provided buffer)
   * @param blockAddress MEMory Address (MEMA) of the block
   * @param bytesInBlockStart where in the block the relevant data starts (see defines up top)
   * @param bytesToWrite how many bytes are of interest (size of writeBuff)
   * @param writeBuff buffer of size (bytesToRead) to write into block
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE _setBytesInBlock(uint8_t blockAddress, uint8_t bytesInBlockStart, uint8_t bytesToWrite, uint8_t writeBuff[]) {
    NT3H1x01_ERR_RETURN_TYPE err = requestMemBlock(blockAddress, _oneBlockBuff); // fetch the whole block
    if(blockAddress == NT3H1x01_I2C_ADDR_CHANGE_MEMA) { _oneBlockBuff[0] = (slaveAddress<<1); } // the I2C address byte is write-only, reading it will return the first byte of the UID
    if(!_errGood(err)) { NT3H1x01debugPrint("_setBytesInBlock() read/write error!"); return(err); }
    for(uint8_t i=0; i<bytesToWrite; i++) { _oneBlockBuff[i+bytesInBlockStart] = writeBuff[i]; } // overwrite only the desired bytes
    err = writeMemBlock(blockAddress, _oneBlockBuff);
    if(!_errGood(err)) { NT3H1x01debugPrint("_setBytesInBlock() write error!"); }
    return(err); // err should always be OK, if it makes it to this point
  }
  /**
   * (private) overwrite an arbetrary set of bytes in a block (with a (little-endian) value)
   * @param blockAddress MEMory Address (MEMA) of the block
   * @param bytesInBlockStart where in the block the relevant data starts (see defines up top)
   * @param newVal value (of type T) to write into the block
   * @param littleEndian true for almost all interactions, but ATQA needs false, so there's the option
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  template<typename T> 
  NT3H1x01_ERR_RETURN_TYPE _setValInBlock(uint8_t blockAddress, uint8_t bytesInBlockStart, T newVal, bool littleEndian=true) {
    NT3H1x01_ERR_RETURN_TYPE err = requestMemBlock(blockAddress, _oneBlockBuff); // fetch the whole block
    if(!_errGood(err)) { NT3H1x01debugPrint("_setValInBlock() read/write error!"); return(err); }
    uint8_t* bytePtrToNewVal = (uint8_t*) &newVal;
    for(uint8_t i=0; i<sizeof(T); i++) { _oneBlockBuff[i+bytesInBlockStart] = bytePtrToNewVal[littleEndian ? (sizeof(T)-1-i) : i]; } // (little-endian)
    return(err);
  }

/////////////////////////////////////////////////////////////////////////////////////// set functions: //////////////////////////////////////////////////////////

  /**
   * overwrite the Capability Container (also mentioned as NDEF thingy) with bytes
   * @param writeBuff 4 byte buffer to write to the CC bytes (i stronly recommend NT3H1x01_CAPA_CONT_DEFAULT[is2kVariant])
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE setCC(uint8_t writeBuff[]) { return(_setBytesInBlock(NT3H1x01_CAPA_CONT_MEMA, NT3H1x01_CAPA_CONT_MEMA_BYTES_START, 4, writeBuff)); }
  /**
   * overwrite the Capability Container (also mentioned as NDEF thingy) with a 4byte value
   * @param newVal 4 byte value (little-endian) to write to the CC bytes (i stronly recommend NT3H1x01_CAPA_CONT_DEFAULT_uint32_t[is2kVariant])
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE setCC(uint32_t newVal) { return(_setValInBlock<uint32_t>(NT3H1x01_CAPA_CONT_MEMA, NT3H1x01_CAPA_CONT_MEMA_BYTES_START, newVal)); }



  #ifdef NT3H1x01_unlock_burning // functions that can have a permanent consequence)
    /**
     * set the I2C address (may brick device(?))
     * @param newAddress is the new 7bit address (so before shifting and adding R/W bit, just 7 bits)
     * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
     */
    NT3H1x01_ERR_RETURN_TYPE setI2Caddress(uint8_t newAddress) { // does not seem to work (yet)
      #warning("setI2Caddress() does not appear to work (yet!)")
      NT3H1x01_ERR_RETURN_TYPE err = _setValInBlock<uint8_t>(NT3H1x01_I2C_ADDR_CHANGE_MEMA, NT3H1x01_I2C_ADDR_CHANGE_MEMA_BYTE, newAddress<<1);
      if(_errGood(err)) { slaveAddress = newAddress; } // update this object's address byte ONLY IF the transfer seemed to go as intended
      else { NT3H1x01debugPrint("setI2Caddress() failed!"); }
      return(err);
    }
    #warning("burnRegLockI2C() and burnRegLockRF() are untested!")
    /**
     * disables writing to the Configuration register bytes from I2C PERMANENTLY
     * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
     */
    NT3H1x01_ERR_RETURN_TYPE burnRegLockI2C() { return(_setValInBlock<uint8_t>(is2kVariant ? NT3H1201_CONF_REGS_MEMA : NT3H1101_CONF_REGS_MEMA, NT3H1x01_CONF_REGS_REG_LOCK_BYTE, NT3H1x01_NC_REG_LOCK_I2C_bits)); }
    /**
     * disables writing to the Configuration register bytes from RF PERMANENTLY
     * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
     */
    NT3H1x01_ERR_RETURN_TYPE burnRegLockRF() { return(_setValInBlock<uint8_t>(is2kVariant ? NT3H1201_CONF_REGS_MEMA : NT3H1101_CONF_REGS_MEMA, NT3H1x01_CONF_REGS_REG_LOCK_BYTE, NT3H1x01_NC_REG_LOCK_RF_bits)); }
  #endif // NT3H1x01_unlock_burning

/////////////////////////////////////////////////////////////////////////////////////// get functions: //////////////////////////////////////////////////////////

  /**
   * retrieve the Serial Number, a.k.a. UID
   * @param readBuff 7 byte buffer to put the results in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE getUID(uint8_t readBuff[]) { return(_getBytesFromBlock(NT3H1x01_SERIAL_NR_MEMA, NT3H1x01_SERIAL_NR_MEMA_BYTES_START, 7, readBuff)); }
  /**
   * retrieve the Capability Container (also mentioned as NDEF thingy) (this version of the function lets you check for I2C errors)
   * @param readBuff 4 byte buffer to put the results in (results should match NT3H1x01_CAPA_CONT_DEFAULT)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE getCC(uint8_t readBuff[]) { return(_getBytesFromBlock(NT3H1x01_CAPA_CONT_MEMA, NT3H1x01_CAPA_CONT_MEMA_BYTES_START, 4, readBuff)); }
  /**
   * retrieve the Capability Container (also mentioned as NDEF thingy) (this version DOES NOT let you check for I2C errors)
   * @return the Capability Container as a uint32_t. result should match NT3H1x01_CAPA_CONT_DEFAULT_uint32_t
   */
  uint32_t getCC() { return(_getValFromBlock<uint32_t>(NT3H1x01_CAPA_CONT_MEMA, NT3H1x01_CAPA_CONT_MEMA_BYTES_START)); }
  /**
   * retrieve the ATQA bytes NOTE: datasheet mentions theses bytes are stored LSB first
   * @param readBuff 2 byte buffer to put the results in(result should match 0x44,0x00)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE getATQA(uint8_t readBuff[]) { return(_getBytesFromBlock(NT3H1x01_ATQA_MEMA, NT3H1x01_ATQA_MEMA_BYTES_START, 2, readBuff)); }
  /**
   * retrieve the ATQA bytes as uint16_t
   * @param littleEndian allows you to specify whether it should be interpreted LSB-first or MSB first
   * @return ATQA bytes as a uint16_t.
   */
  uint16_t getATQA(bool littleEndian=true) { return(_getValFromBlock<uint16_t>(NT3H1x01_ATQA_MEMA, NT3H1x01_ATQA_MEMA_BYTES_START, littleEndian)); }
  /**
   * retrieve the SAK byte
   * @param readBuff byte pointer to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  NT3H1x01_ERR_RETURN_TYPE getSAK(uint8_t readBuff[]) { return(_getBytesFromBlock(NT3H1x01_SAK_MEMA, NT3H1x01_SAK_MEMA_BYTE, 1, readBuff)); }
  /**
   * retrieve the SAK byte
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  uint8_t getSAK() { return(_getValFromBlock<uint8_t>(NT3H1x01_SAK_MEMA, NT3H1x01_SAK_MEMA_BYTE)); }

/////////////////////////////////////////////////////////////////////////////////////// debug functions: //////////////////////////////////////////////////////////

  /**
   * checks if retrieving the UID works without errors, AND if the first byte matches the manufacturer ID
   * @return true if reading was successful and ...
   */
  bool connectionCheck() {
    uint8_t UID[7];
    NT3H1x01_ERR_RETURN_TYPE err = getUID(UID); // fetch the Serial Number a.k.a. UID, the first byte of which should always be the manufacturer ID
    if(!_errGood(err)) { return(false); } // at this point it will have already printed several debug messages, no need to print another
    return(UID[0] == NT3H1x01_SERIAL_NR_NXP_MF_ID);
  }
  /**
   * checks whether the reported memory size matches the expected memory size byte
   * @return true if reading was successful and ...
   */
  bool variantCheck() {
    uint8_t readBuff[4];
    NT3H1x01_ERR_RETURN_TYPE err = getCC(readBuff); // fetch the Serial Number a.k.a. UID, the 3rd byte of which indicates the size of the tag
    if(!_errGood(err)) { NT3H1x01debugPrint("variantCheck() read/write error!"); return(false); }
    bool returnBool = (readBuff[2] == NT3H1x01_CAPA_CONT_DEFAULT[is2kVariant][2]);
    if(!returnBool) { NT3H1x01debugPrint("variantCheck(), CC/NDEF memory size byte did NOT match expectation!"); }
    return(returnBool);
  }

  // uint8_t UIDsizeCheck() { uint8_t tempArr[2]; getATQA(tempArr); return((tempArr[0] >= 0x40) ? 7 : 4); } // getATQA and interpret the 2 bits that indicate the size of the UID


  // /**
  //  * print out all the configuration values of the sensor (just for debugging)
  //  * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
  //  */
  // TMP112_ERR_RETURN_TYPE printConfig() { // prints contents of CONF register (somewhat efficiently)
  //   uint8_t readBuff[2]; // instead of calling all the functions above (which may actually take hundreds of millis at the lowest I2C freq this things supports), just get the CONF register once
  //   TMP112_ERR_RETURN_TYPE readSuccess = requestReadBytes(TMP112_CONF, readBuff, 2);
  //   if(_errGood(readSuccess))
  //    {
  //     //Serial.println("printing config: ");
  //     Serial.print("ShutDown mode: "); Serial.println(readBuff[0] & TMP112_SD_bits);
  //     Serial.print("Thermostat Mode: "); Serial.println((readBuff[0] & TMP112_TM_bits)>>1);
  //     Serial.print("POLatiry: "); Serial.println((readBuff[0] & TMP112_POL_bits)>>2);
  //     static const uint8_t FQbitsToThreshold[4] = {1,2,4,6};
  //     uint8_t FQbits = (readBuff[0] & TMP112_FQ_bits)>>3 ; // note: should also prevent array index overflow, by its very nature
  //     Serial.print("Fault Queue: "); Serial.print(FQbits); Serial.print(" = "); Serial.println(FQbitsToThreshold[FQbits]);
  //     Serial.print("RESolution (should==3): "); Serial.println((readBuff[0] & TMP112_RES_bits)>>5);
  //     Serial.print("One-Shot status: "); Serial.println((readBuff[0] & TMP112_OS_bits)>>1);
  //     bool is13bit = readBuff[1] & TMP112_EM_bits;
  //     Serial.print("Thermostat Mode: "); Serial.println(is13bit);
  //     Serial.print("Alert (active==!POL): "); Serial.println((readBuff[1] & TMP112_AL_bits)>>5);
  //     static const float CRbitsToThreshold[4] = {0.25,1,4,8};
  //     uint8_t CRbits = (readBuff[1] & TMP112_CR_bits)>>6; // note: should also prevent array index overflow, by its very nature
  //     Serial.print("Convertsion Rate: "); Serial.print(CRbits); Serial.print(" = "); Serial.println(CRbitsToThreshold[CRbits]);
  //     // also print Thigh and Tlow (assuming 'is13bit' is working correctly):
  //     int16_t ThighInt = getThighInt(is13bit); // assume that, since the first reading went successfully, this one will as well
  //     Serial.print("Thigh threshold: "); Serial.print(ThighInt); Serial.print(" = "); Serial.println((float)ThighInt * 0.0625);
  //     int16_t TlowInt = getTlowInt(is13bit); // assume that, since the first reading went successfully, this one will as well
  //     Serial.print("Tlow threshold: "); Serial.print(TlowInt); Serial.print(" = "); Serial.println((float)TlowInt * 0.0625);
  //   } else {
  //     Serial.println("TMP112 failed to read CONF register!");
  //   }
  //   return(readSuccess);
  // }

  // /**
  //  * write the defualt value (according to the datasheet) to the CONF register.
  //  * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
  //  */
  // NT3H1x01_ERR_RETURN_TYPE resetConfig() { static uint8_t CONF_default[2]={0x60,0xA0}; return(writeBytes(NT3H1x01_CONF, CONF_default, 2)); } // write the default value (according to datasheet) to CONF register
  /**
   * write the defualt value (according to the datasheet) to the CC bytes, indicating the size of the card (make sure to initialize class object appropriately (is2kVariant))
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  NT3H1x01_ERR_RETURN_TYPE resetCC() { return(setCC(NT3H1x01_CAPA_CONT_DEFAULT_uint32_t[is2kVariant])); } // write the default value (according to datasheet) to CONF register
  // NT3H1x01_ERR_RETURN_TYPE resetCC() { uint8_t tempArr[4]; for(uint8_t i=0;i<4;i++){tempArr[i]=NT3H1x01_CAPA_CONT_DEFAULT[is2kVariant][i];} return(setCC(tempArr)); } // write the default value (according to datasheet) to CONF register
};

#endif  // NT3H1x01_thijs_h


/*
configuration registers read through RF (hex): 10.00.F8.48.08.01.00.00
i was not able to switch sectors from the NFC app i used, so the session registers remain a mystery (from RF)

*/