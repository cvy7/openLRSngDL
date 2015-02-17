/****************************************************
 * OpenLRSng transmitter code
 ****************************************************/
uint8_t RF_channel = 0;

uint32_t lastSent = 0;
uint32_t lastReceived = 0;

uint8_t RSSI_rx = 0;
uint8_t RSSI_tx = 0;
uint32_t sampleRSSI = 0;

uint16_t linkQuality = 0;
uint8_t linkQualityPeer = 0;


#ifndef BZ_FREQ
#define BZ_FREQ 2000
#endif

void bindMode(void)
{
  uint32_t prevsend = millis();
  uint8_t  tx_buf[sizeof(bind_data) + 1];
  bool  sendBinds = 1;

  init_rfm(1);

  while (Serial.available()) {
    Serial.read();    // flush serial
  }

  Red_LED_OFF;

  while (1) {
    if (sendBinds & (millis() - prevsend > 200)) {
      prevsend = millis();
      Green_LED_ON;
      buzzerOn(BZ_FREQ);
      tx_buf[0] = 'b';
      memcpy(tx_buf + 1, &bind_data, sizeof(bind_data));
      tx_packet(tx_buf, sizeof(bind_data) + 1);
      Green_LED_OFF;
      buzzerOff();
      RF_Mode = Receive;
      rx_reset();
      delay(50);
      if (RF_Mode == Received) {
        RF_Mode = Receive;
        spiSendAddress(0x7f);   // Send the package read command
        if ('B' == spiReadData()) {
          sendBinds = 0;
        }
      }
    }

    if (!digitalRead(BTN)) {
      sendBinds = 1;
    }

    while (Serial.available()) {
      Red_LED_ON;
      Green_LED_ON;
      switch (Serial.read()) {
      case '\n':
      case '\r':
        Serial.println(F("Enter menu..."));
        handleCLI();
        init_rfm(1);
        break;
      case '#':
        scannerMode();
        break;
      case 'B':
        //binaryMode();
        break;
      default:
        break;
      }
      Red_LED_OFF;
      Green_LED_OFF;
    }
  }
}

void bindRX(bool timeout)
{
  uint32_t start = millis();
  Serial.println("waiting bind...");
  init_rfm(1);
  RF_Mode = Receive;
  to_rx_mode();
  while(!timeout || ((millis() - start) < 500)) {
    if (RF_Mode == Received) {
      Serial.println("Got pkt\n");
      spiSendAddress(0x7f);   // Send the package read command
      uint8_t rxb = spiReadData();
      if (rxb == 'b') {
        for (uint8_t i = 0; i < sizeof(bind_data); i++) {
          *(((uint8_t*) &bind_data) + i) = spiReadData();
        }
        if (bind_data.version == BINDING_VERSION) {
          Serial.println("data good\n");
          rxb = 'B';
          tx_packet(&rxb, 1); // ACK that we got bound
          bindWriteEeprom();
          Red_LED_ON; //signal we got bound on LED:s
          Green_LED_ON; //signal we got bound on LED:s
        }
      }
    }

  }
}

static inline void checkBND(void)
{
  if ((Serial.available() > 3) &&
      (Serial.read() == 'B') && (Serial.read() == 'N') &&
      (Serial.read() == 'D') && (Serial.read() == '!')) {
    buzzerOff();
    bindMode();
  }
}

#define MODE_MASTER 0
#define MODE_SLAVE  1
bool slaveMode = false;

static inline void checkOperatingMode()
{
  if (digitalRead(SLAVE_SELECT)) {
    slaveMode = false;
  } else {
    slaveMode = true;
  }
}

uint8_t tx_buf[33];
uint8_t rx_buf[33];

#define MASTER_SEQ 0x80
#define SLAVE_SEQ  0x40

/* Frame format:

MASTER->SLAVE:
  byte0 : control (bitmasked)
  x------- seq_M_S master changes this when it is putting new data
  -x------ seq_S_M last bit seen by master is relayed back
  --1xxxxx data packet xxxxx + 1 bytes of data attached
    byte1... data (upto 32 if packetsize permits)
    if possible rssi and quality are appended
  --000000 idle
  --0xxxxx reserved

SLAVE->MASTER
  byte0 : control (bitmasked)
  x------- seq_M_S - not needed
  -x------ seq_S_M -
  --1xxxxx data packet xxxxx + 1 bytes of data attached
    byte1... data (upto 32 if packetsize permits)
    if possible rssi and quality are appended
    byteN+1 RSSI
    byteN+2 quality
  ..000000 idle
    byte1 RSSI
    byte2 quality
*/

// Simple FIFO implementation
#define FIFOSIZE 128
#include"fifo.h"

struct fifo txFifo;

void setup(void)
{
  uint32_t start;

  watchdogConfig(WATCHDOG_OFF);

  setupSPI();
#ifdef SDN_pin
  pinMode(SDN_pin, OUTPUT); //SDN
  digitalWrite(SDN_pin, 0);
#endif
  //LED and other interfaces
  pinMode(Red_LED, OUTPUT); //RED LED
  pinMode(Green_LED, OUTPUT); //GREEN LED
#ifdef Red_LED2
  pinMode(Red_LED2, OUTPUT); //RED LED
  pinMode(Green_LED2, OUTPUT); //GREEN LED
#endif
  // pinMode(BTN, INPUT); //Button
  pinMode(SLAVE_SELECT, INPUT);
  digitalWrite(SLAVE_SELECT, HIGH); // enable pullup for TX:s with open collector output
  buzzerInit();

#ifdef __AVR_ATmega32U4__
  Serial.begin(0); // Suppress warning on overflow on Leonardo
#else
  Serial.begin(115200);
#endif

  checkOperatingMode();

  Serial.print("OpenLRSng DL starting ");
  printVersion(version);
  Serial.print(" on HW ");
  Serial.print(BOARD_TYPE);
  Serial.print(" (");
  Serial.print(RFMTYPE);
  Serial.print("MHz) MDOE=");

  setupRfmInterrupt();
  buzzerOn(BZ_FREQ);
  digitalWrite(BTN, HIGH);
  Red_LED_ON ;
  sei();

  delay(50);
  if (!slaveMode) {
    Serial.println("MASTER");
    if (!bindReadEeprom()) {
      Serial.println("eeprom bogus reinit....");
      bindInitDefaults();
      bindWriteEeprom();
    }
    checkBND();
    if (!digitalRead(BTN)) {
      bindMode();
    }
  } else {
    Serial.println("SLAVE");
    if (!digitalRead(BTN) || !bindReadEeprom()) {
      bindRX(false);
    } else {
      bindRX(true);
    }
  }

  delay(50);
  Serial.println("Entering normal mode");

  start = millis();
  while ((millis() - start) < 2000);


  while (Serial.available()) {
    Serial.read();
  }


  TelemetrySerial.begin(bind_data.serial_baudrate);

  Red_LED_OFF;
  buzzerOff();

  init_rfm(0);
  rfmSetChannel(RF_channel);
  rx_reset();

  fifoInit(&txFifo);
  watchdogConfig(WATCHDOG_2S);
  lastReceived=micros();
}


uint8_t state=0;
uint8_t lostpkts=10; //slow hop at start

void slaveLoop()
{
  watchdogReset();
  uint32_t now = micros();
  bool     needHop=false;
  switch (state) {
  case 0: // waiting for packet
    if (RF_Mode == Received) {
      Green_LED_ON;
      Red_LED_OFF;
      // got packet
      lastReceived = now;
      lostpkts=0;
      linkQuality |= 1;

      RF_Mode = Receive;

      spiSendAddress(0x7f); // Send the package read command
      for (int16_t i = 0; i < bind_data.packetSize; i++) {
        rx_buf[i] = spiReadData();
      }
      // Check if this is a new packet from master and not a resent one
      if ((rx_buf[0] ^ tx_buf[0]) & MASTER_SEQ) {
        tx_buf[0] ^= MASTER_SEQ;
        if (rx_buf[0] & 0x20) {
          // DATA FRAME
          for (uint8_t i=0; i <= (rx_buf[0] & 0x1f); i++) {
            Serial.write(rx_buf[1 + i]);
          }
        }
      }

      // construct TX packet
      tx_buf[0] &= MASTER_SEQ | SLAVE_SEQ;

      if (!((rx_buf[0] ^ tx_buf[0]) & SLAVE_SEQ) && fifoAvail(&txFifo)) {
        uint8_t i;
        for (i=0; fifoAvail(&txFifo) && (i < (bind_data.packetSize-1)); i++) {
          tx_buf[i + 1] = fifoRead(&txFifo);
        }
        tx_buf[0] |= 0x20 + (i-1);
        tx_buf[0] ^= SLAVE_SEQ;
      }

      state = 1;
      tx_packet_async(tx_buf, bind_data.packetSize);
    } else {
      if ((now - lastReceived) > (getInterval(&bind_data) + 500)) {
        Red_LED_ON;

        if (lostpkts++ < 10) {
          needHop=1;
        } else {
          if (lostpkts > 25) {
            needHop=1;
            lostpkts=10;
          }
        }
        // missed a packet
        lastReceived += getInterval(&bind_data);
      }
    }
    break;
  case 1: // waiting TX completion
    switch (tx_done()) {
    case 2: // tx timeout
    // rfm init ??
    case 1: // ok
      Green_LED_OFF;
      state = 0;
      needHop=1;
      rfmSetChannel(RF_channel);
      RF_Mode = Receive;
      rx_reset();
      break;
    }
    break;
  }
  if (needHop) {
    RF_channel++;
    if ((RF_channel == MAXHOPS) || (bind_data.hopchannel[RF_channel] == 0)) {
      RF_channel = 0;
    }
    rfmSetChannel(RF_channel);
  }
}

void masterLoop()
{
  if (RF_Mode == Received) {
    // got packet
    lastReceived = (micros() | 1);

    linkQuality |= 1;

    RF_Mode = Receive;

    spiSendAddress(0x7f); // Send the package read command
    for (int16_t i = 0; i < bind_data.packetSize; i++) {
      rx_buf[i] = spiReadData();
    }
    if ((rx_buf[0] ^ tx_buf[0]) & SLAVE_SEQ) {
      tx_buf[0] ^= SLAVE_SEQ;
      if (rx_buf[0] & 0x20) {
        // DATA FRAME
        for (uint8_t i=0; i <= (rx_buf[0] & 0x1f); i++) {
          Serial.write(rx_buf[1 + i]);
        }
      }
    }

  }

  uint32_t time = micros();

  if ((sampleRSSI) && ((time - sampleRSSI) >= 3000)) {
    RSSI_tx = rfmGetRSSI();
    sampleRSSI = 0;
  }

  if ((time - lastSent) >= getInterval(&bind_data)) {
    lastSent = time;

    watchdogReset();

    if (lastReceived) {
      if ((time - lastReceived) > getInterval(&bind_data)) {
        // telemetry lost
        if (!(bind_data.flags & MUTE_TX)) {
          buzzerOn(BZ_FREQ);
        }
        lastReceived = 0;
      } else {
        // telemetry link re-established
        buzzerOff();
      }
    }

    // Construct packet to be sent
    Green_LED_ON;
    tx_buf[0] &= MASTER_SEQ | SLAVE_SEQ;
    if (!((rx_buf[0] ^ tx_buf[0]) & MASTER_SEQ) && fifoAvail(&txFifo)) {
      uint8_t i;
      for (i=0; fifoAvail(&txFifo) && (i < (bind_data.packetSize-1)); i++) {
        tx_buf[i + 1] = fifoRead(&txFifo);
      }
      tx_buf[0] |= 0x20 + (i-1);
      tx_buf[0] ^= MASTER_SEQ;
    }

    // Send the data over RF on the next frequency
    RF_channel++;
    if ((RF_channel == MAXHOPS) || (bind_data.hopchannel[RF_channel] == 0)) {
      RF_channel = 0;
    }
    rfmSetChannel(RF_channel);
    tx_packet_async(tx_buf, bind_data.packetSize);

  }

  if (tx_done() == 1) {
    linkQuality <<= 1;
    RF_Mode = Receive;
    rx_reset();
    // tell loop to sample downlink RSSI
    sampleRSSI = micros();
    if (sampleRSSI == 0) {
      sampleRSSI = 1;
    }
  }
  Green_LED_OFF;
}

void loop(void)
{
  if (spiReadRegister(0x0C) == 0) {     // detect the locked module and reboot
    Serial.println("module locked?");
    Red_LED_ON;
    init_rfm(0);
    rx_reset();
    Red_LED_OFF;
  }

  while (Serial.available()) {
    if (!fifoWrite(&txFifo,Serial.read())) {
      Serial.println("TX DROP!");
    }
  }

  if (slaveMode) {
    slaveLoop();
  } else {
    masterLoop();
  }
}