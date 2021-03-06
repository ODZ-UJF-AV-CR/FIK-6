// FIK 6 balloon flight secondary LoRa beacon firmware
// Derived from basicmac's usage example.
//
// Compilation with -D MOCK disables most of the code and can
// be used for testing outside of Arduino. If the flag is passed,
// the code reads in a sequence of mock NMEA messages and prints
// the produced message payloads, which would otherwise be transmitted
// over LoRa, on the standard output (hex-encoded).

// ATMEL ATMEGA1284P
//
//                       +---\/---+
//           (D 0) PB0  1|        |40  PA0 (AI 0 / D24)
//           (D 1) PB1  2|        |39  PA1 (AI 1 / D25)
//      INT2 (D 2) PB2  3|        |38  PA2 (AI 2 / D26)
//       PWM (D 3) PB3  4|        |37  PA3 (AI 3 / D27)
//    PWM/SS (D 4) PB4  5|        |36  PA4 (AI 4 / D28)
//      MOSI (D 5) PB5  6|        |35  PA5 (AI 5 / D29)
//  PWM/MISO (D 6) PB6  7|        |34  PA6 (AI 6 / D30)
//   PWM/SCK (D 7) PB7  8|        |33  PA7 (AI 7 / D31)
//                 RST  9|        |32  AREF
//                 VCC 10|        |31  GND 
//                 GND 11|        |30  AVCC
//               XTAL2 12|        |29  PC7 (D 23)
//               XTAL1 13|        |28  PC6 (D 22)
//      RX0 (D 8)  PD0 14|        |27  PC5 (D 21) TDI
//      TX0 (D 9)  PD1 15|        |26  PC4 (D 20) TDO
// RX1/INT0 (D 10) PD2 16|        |25  PC3 (D 19) TMS
// TX1/INT1 (D 11) PD3 17|        |24  PC2 (D 18) TCK
//      PWM (D 12) PD4 18|        |23  PC1 (D 17) SDA
//      PWM (D 13) PD5 19|        |22  PC0 (D 16) SCL
//      PWM (D 14) PD6 20|        |21  PD7 (D 15) PWM
//                       +--------+
//

// SX1262_RST, 0, PB0
// SX1262_DIO3, 3, PB3
// SX1262_BUSY, 2, PB2
// CS, 4, PB4
// MOSI, 5, PB5
// MISO, 6, PB6
// SCK, 7, PB7
// RX0, 8, PD0 -- GPS
// TX0, 9, PD1

#include <TinyGPS++.h>
#include <stdio.h>
#include <stdint.h>

TinyGPSPlus gps;
uint32_t last_packet = 0;

#ifndef MOCK

#include <basicmac.h>
#include <hal/hal.h>
#include <SPI.h>

static const PROGMEM u1_t NWKSKEY[16] = { 0xCA, 0x0F, 0xC7, 0x70, 0x63, 0xBE, 0xF3, 0xDD, 0x15, 0x00, 0xD8, 0x16, 0xB3, 0x13, 0xB7, 0x5D };
static const u1_t PROGMEM APPSKEY[16] = { 0x46, 0x4F, 0xB1, 0x8D, 0xF5, 0x8B, 0x04, 0x0C, 0xD8, 0x99, 0x22, 0xB7, 0x62, 0x65, 0xB8, 0xCB };
static const u4_t DEVADDR = 0x26011828;

void os_getJoinEui (u1_t* /* buf */) { }
void os_getDevEui (u1_t* /* buf */) { }
void os_getNwkKey (u1_t* /* buf */) { }

// The region to use, this just uses the first one (can be changed if
// multiple regions are enabled).
u1_t os_getRegion (void) { return LMIC_regionCode(0); }

// Schedule TX every this many milliseconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60000;

// All pin assignments use Arduino pin numbers (e.g. what you would pass
// to digitalWrite), or LMIC_UNUSED_PIN when a pin is not connected.
const lmic_pinmap lmic_pins = {
    // NSS input pin for SPI communication (required)
    .nss = 4,
    .tx = LMIC_UNUSED_PIN,
    .rx = LMIC_UNUSED_PIN,
    // Radio reset output pin (active high for SX1276, active low for
    // others). When omitted, reset is skipped which might cause problems.
    .rst = 1,
    // DIO input pins.
    //   For SX127x, LoRa needs DIO0 and DIO1, FSK needs DIO0, DIO1 and DIO2
    //   For SX126x, Only DIO1 is needed (so leave DIO0 and DIO2 as LMIC_UNUSED_PIN)
    .dio = {/* DIO0 */ LMIC_UNUSED_PIN, /* DIO1 */ 3, /* DIO2 */ LMIC_UNUSED_PIN},
    // Busy input pin (SX126x only). When omitted, a delay is used which might
    // cause problems.
    .busy = 2,
    .tcxo = LMIC_UNUSED_PIN,
};

void onLmicEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.print(F("Received "));
              Serial.print(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            }
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
        case EV_SCAN_FOUND:
            Serial.println(F("EV_SCAN_FOUND"));
            break;
        case EV_TXSTART:
            Serial.println(F("EV_TXSTART"));
            break;
        case EV_TXDONE:
            Serial.println(F("EV_TXDONE"));
            break;
        case EV_DATARATE:
            Serial.println(F("EV_DATARATE"));
            break;
        case EV_START_SCAN:
            Serial.println(F("EV_START_SCAN"));
            break;
        case EV_ADR_BACKOFF:
            Serial.println(F("EV_ADR_BACKOFF"));
            break;
         default:
            Serial.print(F("Unknown event: "));
            Serial.println(ev);
            break;
    }
}

void setup() {
    Serial.begin(9600);

    Serial.println();
    Serial.println(F("Booted up."));

    // Wait up to 5 seconds for serial to be opened, to allow catching
    // startup messages on native USB boards (that do not reset when
    // serial is opened).
    unsigned long start = millis();
    while (millis() - start < 5000 && !Serial);

    Serial.println();
    Serial.println();
    Serial.println(F("Starting"));
    Serial.println();

    // LMIC init
    os_init(nullptr);
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    #ifdef PROGMEM
    // On AVR, these values are stored in flash and only copied to RAM
    // once. Copy them to a temporary buffer here, LMIC_setSession will
    // copy them into a buffer of its own again.
    uint8_t appskey[sizeof(APPSKEY)];
    uint8_t nwkskey[sizeof(NWKSKEY)];
    memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
    LMIC_setSession (0x1, DEVADDR, nwkskey, appskey);
    #else
    // If not running an AVR with PROGMEM, just use the arrays directly
    LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);
    #endif

    #if defined(CFG_eu868)
    // These are defined by the LoRaWAN specification
    enum {
        EU_DR_SF12 = 0,
        EU_DR_SF11 = 1,
        EU_DR_SF10 = 2,
        EU_DR_SF9 = 3,
        EU_DR_SF8 = 4,
        EU_DR_SF7 = 5,
        EU_DR_SF7_BW250 = 6,
        EU_DR_FSK = 7,
    };

    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set.
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7_BW250)); // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(EU_DR_SF12, EU_DR_SF7));      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(EU_DR_FSK,  EU_DR_FSK));      // g2-band

    // TTN uses SF9 at 869.525Mhz for its RX2 window (frequency is
    // default LoRaWAN, SF is different, but set them both to be
    // explicit).
    LMIC.dn2Freq = 869525000;
    LMIC.dn2Dr = EU_DR_SF9;

    // Set data rate for uplink
    LMIC_setDrTxpow(EU_DR_SF7, KEEP_TXPOWADJ);
    #elif defined(CFG_us915)
    // NA-US channels 0-71 are configured automatically
    // but only one group of 8 should (a subband) should be active
    // TTN recommends the second sub band, 1 in a zero based count.
    // https://github.com/TheThingsNetwork/gateway-conf/blob/master/US-global_conf.json
    // TODO: How to configure these channels? LMIC had LMIC_selectSubBand,
    // but it seems BasicMac only has LMIC_disableChannel.
    #endif

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // Enable this to increase the receive window size, to compensate
    // for an inaccurate clock.  // This compensate for +/- 10% clock
    // error, a lower value will likely be more appropriate.
    //LMIC_setClockError(MAX_CLOCK_ERROR * 10 / 100);

    // Queue first packet
    send_packet();
}

void loop() {
    while (Serial.available() > 0)
        gps.encode(Serial.read());

    // Let LMIC handle background tasks
    os_runstep();

    // If TX_INTERVAL passed, *and* our previous packet is not still
    // pending (which can happen due to duty cycle limitations), send
    // the next packet.
    if (millis() - last_packet > TX_INTERVAL && !(LMIC.opmode & (OP_JOINING|OP_TXRXPEND)))
        send_packet();
}

#endif /* MOCK not defined */

uint8_t *pack_latlon(uint8_t *p, double vd) {
    uint32_t v = vd * 4194304.0;
    *p++ = v >> 24;
    *p++ = v >> 16;
    *p++ = v >> 8;
    *p++ = v;
    return p;
}

enum {
    LATLON_OK = (1<<0),
    ALT_OK    = (1<<1),
    COURSE_OK = (1<<2),
    SPEED_OK  = (1<<3)
};

void send_packet()
{
    uint8_t m[17];
    uint8_t *p = m+1;
    m[0] = 0;
    p = pack_latlon(p, gps.location.lat());
    p = pack_latlon(p, gps.location.lng());
    if (gps.location.isValid())
        m[0] |= LATLON_OK;
    uint32_t age_s = gps.location.age() / 1000;
    if (age_s > 0xffff)
        age_s = 0xffff;
    *p++ = age_s >> 8;
    *p++ = age_s;
    int32_t alt_m = gps.altitude.meters();
    if (alt_m < -0x7fff) alt_m = -0x7fff;
    if (alt_m >  0x7fff) alt_m =  0x7fff;
    *p++ = alt_m >> 8;
    *p++ = alt_m;
    if (gps.altitude.isValid())
        m[0] |= ALT_OK;
    int16_t course = gps.course.deg() * 64;
    *p++ = course >> 8;
    *p++ = course;
    if (gps.course.isValid())
        m[0] |= COURSE_OK;
    int16_t speed = gps.speed.mps() * 16;
    *p++ = speed >> 8;
    *p++ = speed;
    if (gps.speed.isValid())
        m[0] |= SPEED_OK;

#ifndef MOCK
    LMIC_setTxData2(1, m, sizeof(m), 0);
    Serial.println(F("Packet queued"));
#else
    for (int i = 0; i < sizeof(m); i++)
        printf("%02x", m[i]);
    printf("\n");
#endif

    last_packet = millis();
}

#ifdef MOCK
const char *gpsStream =
"$GPGGA,203026.311,4902.232,N,01429.466,E,1,12,1.0,0.0,M,0.0,M,,*6E\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203026.311,A,4902.232,N,01429.466,E,038.9,065.2,191120,000.0,W*73\r\n"
"$GPGGA,203027.311,4902.238,N,01429.479,E,1,12,1.0,0.0,M,0.0,M,,*6B\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203027.311,A,4902.238,N,01429.479,E,038.9,065.2,191120,000.0,W*76\r\n"
"$GPGGA,203028.311,4902.244,N,01429.493,E,1,12,1.0,0.0,M,0.0,M,,*6B\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203028.311,A,4902.244,N,01429.493,E,038.9,065.2,191120,000.0,W*76\r\n"
"$GPGGA,203029.311,4902.250,N,01429.506,E,1,12,1.0,0.0,M,0.0,M,,*62\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203029.311,A,4902.250,N,01429.506,E,038.9,065.2,191120,000.0,W*7F\r\n"
"$GPGGA,203030.311,4902.256,N,01429.520,E,1,12,1.0,0.0,M,0.0,M,,*68\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203030.311,A,4902.256,N,01429.520,E,038.9,065.2,191120,000.0,W*75\r\n"
"$GPGGA,203031.311,4902.263,N,01429.533,E,1,12,1.0,0.0,M,0.0,M,,*6D\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203031.311,A,4902.263,N,01429.533,E,038.9,065.2,191120,000.0,W*70\r\n"
"$GPGGA,203032.311,4902.269,N,01429.547,E,1,12,1.0,0.0,M,0.0,M,,*67\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203032.311,A,4902.269,N,01429.547,E,038.9,065.2,191120,000.0,W*7A\r\n"
"$GPGGA,203033.311,4902.275,N,01429.560,E,1,12,1.0,0.0,M,0.0,M,,*6E\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203033.311,A,4902.275,N,01429.560,E,038.9,065.2,191120,000.0,W*73\r\n"
"$GPGGA,203034.311,4902.281,N,01429.574,E,1,12,1.0,0.0,M,0.0,M,,*67\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203034.311,A,4902.281,N,01429.574,E,038.9,065.2,191120,000.0,W*7A\r\n"
"$GPGGA,203035.311,4902.287,N,01429.587,E,1,12,1.0,0.0,M,0.0,M,,*6C\r\n"
"$GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30\r\n"
"$GPRMC,203035.311,A,4902.287,N,01429.587,E,038.9,065.2,191120,000.0,W*71\r\n";
int main(int argc, char const *argv[])
{
    while (*gpsStream)
        if (gps.encode(*gpsStream++))
            send_packet();
    return 0;
}
#endif /* MOCK defined */
