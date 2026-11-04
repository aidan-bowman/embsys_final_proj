// Aidan Bowman, Vivian Phan
// EEL4742: Embedded Systems
// Fall 2026
// Final Project

// TODO: this is just copied over from my 8.3 program

#include <msp430.h>
#include <stdint.h>

/*** DEFINES ***/
// LEDs and buttons
#define REDLED BIT0             // P1.0
#define GRNLED BIT7             // P9.7
#define BTN1 BIT1               // P1.1
#define BTN2 BIT2               // P1.2
#define BTNJ BIT2               // J1.5 -> P3.2
#define BZZR BIT7               // J4.40 -> P2.7

// UART
#define UART_FLGS UCA1IFG       // Contains the transmit & receive flags
#define UART_RXFG UCRXIFG       // Receive flag
#define UART_TXFG UCTXIFG       // Transmit flag
#define UART_TXBFFR UCA1TXBUF   // Transmit buffer
#define UART_RXBFFR UCA1RXBUF   // Receive buffer

// PROGRAM DEFS
#define JOY_DEADZONE 500
#define DANGER_LIMIT 10
#define CORNER_MASK 0x03
#define MODE_BIT BIT2
#define MOVE_CURSOR 0
#define ADJUST_VALUE 1
#define CENTER 2048 // this should probably be a calibrated value tbdesu

/*** FUNC DECLARATIONS ***/
uint8_t proc_joystick();
void draw_platform();
uint8_t get_max_delta();
void set_buzzer(uint8_t on);
  
void uart_write_uint16(unsigned int n);
void uart_write_string(char *str);
void uart_write_char(unsigned char ch);

void config_ACLK_to_32KHz_crystal();
void init_UART();
void init_UART_ALCK();
void init_ADC();
void init_platform_io();

/*** GLOBAL VARS ***/
volatile uint8_t h[4]; // in order: UL, UR, LL, LR, aka reading order
volatile uint8_t sel;  // [1:0] is corner sel, [2] is current mode

/*** FUNCTIONS ***/
/** MAIN **/
int main() {
  h[0] = 127; h[1] = 127; h[2] = 127; h[3] = 127;
  sel = 0;
  WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
  PM5CTL0 &= ~LOCKLPM5;		// enable GPIO

  // Joystick pushbutton: P3.2, active low
  P3DIR &= ~BTNJ;
  P3REN |=  BTNJ;
  P3OUT |=  BTNJ;
  P3IES |=  BTNJ;
  P3IFG &= ~BTNJ;
  P3IE  |=  BTNJ;
  
  // Buzzer: P2.7 / TB0.6
  P2DIR |= BZZR;
  P2SEL1 &= ~BZZR;
  P2SEL0 |= BZZR;

  // Configure Timer B0 for approximately 1 kHz PWM
  // Assumes SMCLK is running at 1 MHz
  TB0CCR0 = 999;
  TB0CCR6 = 500;
  TB0CCTL6 = OUTMOD_7;
  TB0CTL = TBSSEL__SMCLK | MC__UP | TBCLR;

  // Initially disable the buzzer output
  TB0CCTL6 = OUTMOD_0;
  P2OUT &= ~BZZR;

  config_ACLK_to_32KHz_crystal();
  init_UART(); // have to use SMCLK cuz of the buzzer
  init_ADC();

  uint16_t x;
  uint16_t y;
  uint8_t redraw;

  _enable_interrupts();

  for (;;) {
    // Start ADC conversion
    ADC12CTL0 |= ADC12SC;
    while (ADC12CTL1 & ADC12BUSY) {}

    x = ADC12MEM0;
    y = ADC12MEM1;
    redraw = proc_joystick(x, y);

    if (get_max_delta() > DANGER_LIMIT) {
      set_buzzer(1);
    } else {
      set_buzzer(0);
    }
    
    if (redraw) { draw_platform(); };
   
    _delay_cycles(100000);
  }
}

/** INTERRUPTS **/
#pragma vector = PORT3_VECTOR
__interrupt void P3_ISR() {
  sel ^= MODE_BIT;
  draw_platform();

  P3IFG &= ~BTNJ;
}

/** HELPERS **/
uint8_t proc_joystick(uint16_t x, uint16_t y) {
  if (sel & MODE_BIT) { // ADJUST VALUE
    if ((y > CENTER - JOY_DEADZONE) && (y < CENTER + JOY_DEADZONE)) { return 0; }
    uint8_t corner = sel & CORNER_MASK;

    // cap at 0 and 255 (no roll-over!)
    if (y < CENTER) {
      if (h[corner] > 1) { h[corner]--; }
    } else {
      if (h[corner] < 255) { h[corner]++; }
    }
    
    return 1;
  } else { // MOVE CURSOR
    int oldsel = sel;
    
    if ((x > CENTER - JOY_DEADZONE) && (x < CENTER + JOY_DEADZONE) &&
        (y > CENTER - JOY_DEADZONE) && (y < CENTER + JOY_DEADZONE)) { return 0; }
    
    sel = 0; // we already know MODE_BIT is on, so this sets us to 0
    if (x > CENTER) { sel++; } // if on the right, we're either 1 or 3
    if (y < CENTER) { sel+=2; } // if on the bottom, we're either 2 or 3

    return sel != oldsel;
  }
}

void draw_platform() {
  uint8_t delta = get_max_delta();

  uart_write_string("\033[2J\033[H"); // clear term

  uart_write_string("*** PLATFORM CONTROL ***\n\r\n\r");

  uart_write_string("             TOP\n\r");
  uart_write_string("   .......................\n\r");

  // Upper corners
  uart_write_string(" L | ");
  if ((sel & CORNER_MASK) == 0)
    uart_write_char('>');
  else
    uart_write_char(' ');

  uart_write_uint16(h[0]);

  uart_write_string("         ");

  if ((sel & CORNER_MASK) == 1)
    uart_write_char('<');
  else
    uart_write_char(' ');

  uart_write_uint16(h[1]);
  uart_write_string(" | R\n\r");

  uart_write_string(" E |                   | I\n\r");
  uart_write_string(" F |                   | G\n\r");
  uart_write_string(" T |                   | H\n\r");

  // Lower corners
  uart_write_string("   | ");
  if ((sel & CORNER_MASK) == 2)
    uart_write_char('>');
  else
    uart_write_char(' ');

  uart_write_uint16(h[2]);

  uart_write_string("         ");

  if ((sel & CORNER_MASK) == 3)
    uart_write_char('<');
  else
    uart_write_char(' ');

  uart_write_uint16(h[3]);
  uart_write_string(" | T\n\r");

  uart_write_string("   .......................\n\r");
  uart_write_string("            BOTTOM\n\r\n\r");

  uart_write_string("   ");

  if (sel & MODE_BIT)
    uart_write_string("ADJUST VALUE");
  else
    uart_write_string("MOVE CURSOR");

  uart_write_string("       CUR DELTA\n\r");

  uart_write_string("   ");

  if (sel & MODE_BIT)
    uart_write_string("CHANGE HEIGHT");
  else
    uart_write_string("SELECT CORNER");

  uart_write_string("       ");
  uart_write_uint16(delta);
  uart_write_string(" mm\n\r");

  if (delta > DANGER_LIMIT) {
    uart_write_string("\n\r\n\r");
    uart_write_string("          *** DANGER ***\n\r");
  }
}

uint8_t get_max_delta() {
  uint8_t min = h[0];
  uint8_t max = h[0];
  uint8_t i;

  for (i = 1; i < 4; i++) {
    if (h[i] < min)
      min = h[i];

    if (h[i] > max)
      max = h[i];
  }

  return max - min;
}

void set_buzzer(uint8_t on) {
  if (on) {
    TB0CCTL6 = OUTMOD_7;
  } else {
    TB0CCTL6 = OUTMOD_0;
    P2OUT &= ~BZZR;
  }
}

void uart_write_uint16(unsigned int n) {
  char out[6];
  int i = 0;

  // special case
  if (n == 0) {
    uart_write_char('0');
    return;
  }

  while (n > 0) {
    out[i++] = (n % 10) + '0';
    n /= 10;
  }

  int j;
  for (j = i - 1; j >= 0; j--) {
    uart_write_char(out[j]);
  }
}

void uart_write_string(char *str) {
  int i;
  for (i = 0; ; i++) {
    if (str[i] == '\0') { return; }
    uart_write_char(str[i]);
  }
}

void uart_write_char(unsigned char ch) {
  // Wait for any ongoing transmission to complete
  while (!(UART_FLGS & UART_TXFG)) {}

  // Copy the byte to the transmit buffer
  UART_TXBFFR = ch; // Tx flag goes to 0 and Tx begins!
  return;
}

/** INITS **/
//**********************************
// Configures ACLK to 32 KHz crystal
void config_ACLK_to_32KHz_crystal() {
  // By default, ACLK runs on LFMODCLK at 5MHz/128 = 39 KHz

  // Reroute pins to LFXIN/LFXOUT functionality
  PJSEL1 &= ~BIT4;
  PJSEL0 |= BIT4;

  // Wait until the oscillator fault flags remain cleared
  CSCTL0 = CSKEY; // Unlock CS registers

  do {
    CSCTL5 &= ~LFXTOFFG; // Local fault flag
    SFRIFG1 &= ~OFIFG; // Global fault flag
  } while ((CSCTL5 & LFXTOFFG) != 0);
	
  CSCTL0_H = 0; // Lock CS registers
  return;
}

// Configure UART to the popular configuration
// 9600 baud, 8-bit data, LSB first, no parity bits, 1 stop bit
// no flow control, oversampling reception
// Clock: SMCLK @ 1 MHz (1,000,000 Hz)
void init_UART() {
  // Configure pins to UART functionality
  P3SEL1 &= ~(BIT4|BIT5);
  P3SEL0 |= (BIT4|BIT5);

  // Main configuration register
  UCA1CTLW0 = UCSWRST; // Engage reset; change all the fields to zero

  // Most fields in this register, when set to zero, correspond to the
  // popular configuration
  UCA1CTLW0 |= UCSSEL_2; // Set clock to SMCLK

  // Configure the clock dividers and modulators (and enable oversampling)
  UCA1BRW = 6; // divider

  // Modulators: UCBRF = 8 = 1000 --> UCBRF3 (bit #3)
  // UCBRS = 0x20 = 0010 0000 = UCBRS5 (bit #5)
  UCA1MCTLW = UCBRF3 | UCBRS5 | UCOS16;

  // Exit the reset state
  UCA1CTLW0 &= ~UCSWRST;
}

void init_UART_ALCK() {
  // Configure pins to UART functionality
  P3SEL1 &= ~(BIT4|BIT5);
  P3SEL0 |= (BIT4|BIT5);

  // Main configuration register
  UCA1CTLW0 = UCSWRST; // Engage reset; change all the fields to zero

  // Most fields in this register, when set to zero, correspond to the
  // popular configuration
  UCA1CTLW0 |= UCSSEL_1; // Set clock to SMCLK

  // Configure the clock dividers and modulators (and enable oversampling)
  UCA1BRW = 6; // divider

  // UCBRS = 0xEE = 1101 1101 = bits 7,6,4, 3,2,0
  UCA1MCTLW = (UCBRS7 | UCBRS6 | UCBRS4 | UCBRS3 | UCBRS2 | UCBRS0);

  // Exit the reset state
  UCA1CTLW0 &= ~UCSWRST;
}

void init_ADC() {
  // Configure X-axis pin: A10/P9.2
  P9SEL1 |= BIT2;
  P9SEL0 |= BIT2;

  // Configure Y-axis pin: A4/P8.7
  P8SEL1 |= BIT7;
  P8SEL0 |= BIT7;

  // Turn on ADC
  ADC12CTL0 |= ADC12ON;

  // Disable ENC during configuration
  ADC12CTL0 &= ~ADC12ENC;

  //*************** ADC12CTL0 ***************
  // Sample-and-hold: 32 cycles
  ADC12CTL0 |= ADC12SHT0_3;

  // Multiple Sample and Conversion
  ADC12CTL0 |= ADC12MSC;

  //*************** ADC12CTL1 ***************
  // ADC12SHS: ADC12SC trigger
  // ADC12SHP: Sampling timer
  // ADC12DIV: Divide by 1
  // ADC12SSEL: MODOSC
  // ADC12CONSEQ: Sequence of channels
  ADC12CTL1 = ADC12SHS_0 |
    ADC12SHP |
    ADC12DIV_0 |
    ADC12SSEL_0 |
    ADC12CONSEQ_1;

  //*************** ADC12CTL2 ***************
  ADC12CTL2 |=  ADC12RES_2;
  ADC12CTL2 &= ~ADC12DF;

  //*************** ADC12CTL3 ***************
  // Start sequence at ADC12MEM0
  ADC12CTL3 &= ~ADC12MEM0;

  //*************** ADC12MCTL0 ***************
  // X-axis: A10
  // VR+ = AVCC, VR- = AVSS
  ADC12MCTL0 = ADC12VRSEL_0 | ADC12INCH_10;

  //*************** ADC12MCTL1 ***************
  // Y-axis: A4
  // VR+ = AVCC, VR- = AVSS
  // End of sequence
  ADC12MCTL1 = ADC12VRSEL_0 |
    ADC12INCH_4 |
    ADC12EOS;

  // Enable conversions
  ADC12CTL0 |= ADC12ENC;

  return;
}
