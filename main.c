// EEL4742: Embedded Systems
// Fall 2026
// Final lab project
// Aidan Bowman and Vivian Phan
// Tic-tac-toe

#include <msp430.h>
#include <stdint.h>

/** DEFINES **/
/* MSP430FR6989 + BOOSTXL-EDUMKII
 * TFT GPIO mapping from the user's SUCCESSFUL color test:
 * CLK P1.4, MOSI P1.6, CS P2.5, DC P2.3, RESET P9.4
 * 128x128 ST7735-like TFT, software SPI.
 * Joystick ADC: A10/P9.2 (X), A4/P8.7 (Y), verify axes if needed.
 * Buttons: LaunchPad S1=P1.1, S2=P1.2 (NOT BoosterPack buttons).
 * S1 short=place, S2 short=switch game, S1 long=reset active,
 * S2 long=toggle PvP/PvC and reset active.
 * Bottom 6-digit LCD: game, mode, turn/status, row, col, cell.
 */

/* TEST FIRST WITH 0: isolates TFT from LCD_C pin multiplexing.
 * Once the 3x3 board displays, change to 1 to enable bottom LCD.
 */
#define ENABLE_BOTTOM_LCD 0
/* Set to 1 only after TFT and joystick are verified. */

#define TFT_MADCTL 0xC0  /* rotate display 180 degrees */
#define CLK BIT4
#define MOSI BIT6
#define CS BIT5
#define DC BIT3
#define RST BIT4
#define S1 BIT1
#define S2 BIT2
#define LOW 1300
#define HIGH 2800

// colors
#define WHITE 0xFFFF
#define BLACK 0x0000
#define RED 0xF800
#define BLUE 0x001F
#define GREEN 0x07E0
#define YELLOW 0xFFE0
#define GRAY 0x8410
#define CYAN 0x07FF

/** FUNC DECLARATIONS **/
// TODO move declarations in here

/** STRUCT DEFS **/
typedef struct {
  uint8_t b[9];
  uint8_t cursor;
  uint8_t turn;
  uint8_t mode;
  uint8_t result;
} Game;

/** GLOBAL VARS **/
// 3x5 glyphs, each row is three bits, MSB left.
static const uint8_t glyphs[][6] = {
  {' ',0,0,0,0,0},
  {'A',2,5,7,5,5}, {'B',6,5,6,5,6}, {'C',3,4,4,4,3},
  {'D',6,5,5,5,6}, {'E',7,4,6,4,7}, {'F',7,4,6,4,4},
  {'G',3,4,5,5,3}, {'H',5,5,7,5,5}, {'I',7,2,2,2,7},
  {'L',4,4,4,4,7}, {'M',5,7,7,5,5}, {'N',5,7,7,7,5},
  {'O',2,5,5,5,2}, {'P',6,5,6,4,4}, {'R',6,5,6,5,5},
  {'S',3,4,2,1,6}, {'T',7,2,2,2,2}, {'U',5,5,5,5,7},
  {'V',5,5,5,5,2}, {'W',5,5,7,7,5}, {'X',5,5,2,5,5},
  {'Y',5,5,2,2,2},
  {'0',7,5,5,5,7}, {'1',2,6,2,2,7}, {'2',7,1,7,4,7},
  {'3',7,1,7,1,7}, {'4',5,5,7,1,1}, {'5',7,4,7,1,7},
  {'6',7,4,7,5,7}, {'7',7,1,1,1,1}, {'8',7,5,7,5,7},
  {'9',7,5,7,1,7}, {'-',0,0,7,0,0}, {':',0,2,0,2,0}
};

static const uint8_t winlines[8][3] = {
  {0,1,2},{3,4,5},{6,7,8},
  {0,3,6},{1,4,7},{2,5,8},
  {0,4,8},{2,4,6}
};

static Game games[2];

static uint8_t active=0;
static uint8_t dirty=1;
static uint8_t joyNeutral=1;

static uint16_t joyX=2048;
static uint16_t joyY=2048;

static uint8_t adcOK=0;

// Segment LCD: common TI FR6989 six-digit mapping.
static const uint8_t lcdpos[6] = {9,5,3,18,14,7};

static const uint8_t digits[10][2] ={
  {0xFC,0x28},{0x60,0x20},{0xDB,0},
  {0xF3,0},{0x67,0},{0xB7,0},{0xBF,0},
  {0xE4,0},{0xFF,0},{0xF7,0}};

/** FUNCTIONS **/
static void delay_ms(unsigned int n) {
  while(n--) {
    __delay_cycles(1000);
  }
}

static void spi_byte(uint8_t x) {
  uint8_t i;
    
  for (i = 0; i < 8; i++) {
    P1OUT &= ~CLK;
    if(x & 0x80) {
      P1OUT |= MOSI;
    } else {
      P1OUT &= ~MOSI;
    }
        
    __delay_cycles(2);
    P1OUT |= CLK;
    __delay_cycles(2);
    x <<= 1;
  }
    
  P1OUT &= ~CLK;
}

static void command(uint8_t v) {
  P2OUT &= ~DC;
  P2OUT &= ~CS;
  spi_byte(v);
  P2OUT |= CS;
}

static void data8(uint8_t v) {
  P2OUT |= DC;
  P2OUT &= ~CS;
  spi_byte(v);
  P2OUT |= CS;
}

static void tft_init() {
  P9OUT |= RST;
  delay_ms(20);
  
  P9OUT &= ~RST;
  delay_ms(50);

  P9OUT |= RST;
  delay_ms(150);

  command(0x01);
  delay_ms(150);

  command(0x11);
  delay_ms(150);

  command(0x3A);
  data8(0x05);

  command(0x36);
  data8(TFT_MADCTL);

  command(0x13);
  delay_ms(20);

  command(0x29);
  delay_ms(100);

}

static void window(uint8_t x0,uint8_t y0,uint8_t x1,uint8_t y1) {
  command(0x2A);
  data8(0);
  data8(x0);
  data8(0);
  data8(x1);

  command(0x2B);
  data8(0);
  data8(y0);
  data8(0);
  data8(y1);

  command(0x2C);
}

static void rect(int x,int y,int w,int h,uint16_t color) {
  uint32_t n;

  if(w <= 0 || h <= 0 || x >= 128 || y >= 128 || x < 0 || y < 0) return;

  if(x+w > 128)
    w = 128 - x;

  if(y+h > 128)
    h = 128 - y;

  window((uint8_t)x,
         (uint8_t)y,
         (uint8_t)(x+w-1),
         (uint8_t)(y+h-1));

  P2OUT |= DC;
  P2OUT &= ~CS;

  for(n=0; n<(uint32_t)w*(uint32_t)h; n++) {
    spi_byte((uint8_t)(color>>8));
    spi_byte((uint8_t)color);
  }
  
  P2OUT |= CS;
}

static void pixel(int x, int y, uint16_t color) {
  rect(x,y,1,1,color);
}

static void line(int x0,int y0,int x1,int y1,uint16_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int sx = x0 < x1 ? 1 : -1;
  int dy = -(y1 > y0 f ?y1 - y0 : y0 - y1);
  int sy = y0 < y1 ? 1 : -1;
  int e  = dx + dy;
  int e2;

  for(;;) {
    pixel(x0,y0,c);
    
    if(x0==x1 && y0==y1)
      return;

    e2 = 2*e;
    
    if(e2 >= dy){
      e += dy;
      x0 += sx;
    }
    
    if(e2 <= dx){
      e += dx;
      y0 += sy;
    }
  }
}

static void character(int x,int y,char ch,uint16_t color,int scale) {
  unsigned int i;
  int row, col;
  const uint8_t *g = glyphs[0];

  for(i=0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
    if(glyphs[i][0] == (uint8_t)ch){
      g = glyphs[i];
      break;
    }
  for(row=0;row<5;row++)
    for(col=0;col<3;col++)
      if(g[row+1]&(4>>col))
        rect(x+col*scale,y+row*scale,scale,scale,color);

}

static void text_at(int x,int y,const char *s,uint16_t color,int scale) {
  while(*s) {
    character(x,y,*s++,color,scale);
    x += 4*scale;
  }
}

static void reset(Game *g) {
  uint8_t i;
  for(i=0;i<9;i++)g->b[i]=0;

  g->cursor=4;
  g->turn=1;
  g->result=0;

}

static uint8_t winner(Game *g) {
  uint8_t i,a,b,c;

  for(i=0;i<8;i++){
    a=winlines[i][0];
    b=winlines[i][1];
    c=winlines[i][2];

    if(g->b[a] && g->b[a]==g->b[b] && g->b[b]==g->b[c])return g->b[a];
  }

  return 0;
}

static void check_end(Game *g) {
  uint8_t i,w=winner(g);

  if(w){g->result=w;
    return;
  }

  for(i=0;i<9;i++)if(!g->b[i])return;

  g->result=3;

}

static uint8_t winning_spot(Game *g,uint8_t player) {
  uint8_t i;

  for(i=0;i<9;i++)if(!g->b[i]){
      g->b[i]=player;

      if(winner(g)==player){g->b[i]=0;
        return i;
      }
      g->b[i]=0;
    }
  return 9;
}

static void computer(Game *g) {
  static const uint8_t order[9]={4,0,2,6,8,1,3,5,7};

  uint8_t i,move=winning_spot(g,2);

  if(move==9)move=winning_spot(g,1);

  if(move==9)for(i=0;i<9;i++)if(!g->b[order[i]]){move=order[i];
        break;
      }
  if(move<9){g->b[move]=2;
    g->cursor=move;
    check_end(g);
    g->turn=1;
  }
}

static void place() {
  Game *g=&games[active];

  if(g->result || g->b[g->cursor])return;

  g->b[g->cursor]=g->turn;

  check_end(g);

  if(!g->result){g->turn=3-g->turn;

    if(g->mode==1 && g->turn==2)computer(g);

  }

  dirty=1;
}

static void segment_init() {
#if ENABLE_BOTTOM_LCD
  PJSEL0 |= BIT4|BIT5;

  LCDCPCTL0=0xFFFF;
  LCDCPCTL1=0xFC3F;
  LCDCPCTL2=0x0FFF;

  LCDCCTL0=LCDDIV__1|LCDPRE__16|LCD4MUX|LCDLP;

  LCDCVCTL=VLCD_1|VLCDREF_0|LCDCPEN;

  LCDCCPCTL=LCDCPCLKSYNC;

  LCDCCTL0 |= LCDON;
#endif
}

static void segment_update() {
#if ENABLE_BOTTOM_LCD
  uint8_t i,p;
  Game *g = &games[active];

  uint8_t values[6]={active+1,g->mode+1,g->result?(uint8_t)(g->result+2):g->turn,
                     (uint8_t)(g->cursor/3+1),(uint8_t)(g->cursor%3+1),g->b[g->cursor]};

  for(i=0;i<32;i++)LCDMEM[i]=0;

  for(i=0;i<6;i++){
    p=lcdpos[i];
    LCDMEM[p]=digits[values[i]][0];
    LCDMEM[p+1]=digits[values[i]][1];

  }
#endif
}

static void draw_game() {
  uint8_t i,r,c;
  int x,y;

  Game *g=&games[active];

  rect(0,0,128,128,BLACK);

  text_at(5,3,active==0?"GAME A":"GAME B",CYAN,2);

  text_at(79,3,g->mode?"PVC":"PVP",YELLOW,2);

  text_at(4,18,g->result==1?"X WINS":g->result==2?"O WINS":
          g->result==3?"DRAW":g->turn==1?"X TURN":"O TURN",WHITE,2);

  /* Board: x=19..108, y=34..123, 30px per cell */
  rect(49,34,2,90,WHITE);
  rect(79,34,2,90,WHITE);

  rect(19,64,90,2,WHITE);
  rect(19,94,90,2,WHITE);

  for(i=0;i<9;i++){
    r=i/3;
    c=i%3;
    x=19+30*c;
    y=34+30*r;

    if(i==g->cursor && !g->result){
      rect(x+2,y+2,26,2,YELLOW);
      rect(x+2,y+26,26,2,YELLOW);

      rect(x+2,y+2,2,26,YELLOW);
      rect(x+26,y+2,2,26,YELLOW);

    }

    if(g->b[i]==1){
      line(x+8,y+8,x+22,y+22,RED);
      line(x+9,y+8,x+23,y+22,RED);
      line(x+22,y+8,x+8,y+22,RED);
      line(x+23,y+8,x+9,y+22,RED);
    }else if(g->b[i]==2){
      /* blocky circle, drawn as an octagon */
      line(x+11,y+7,x+19,y+7,BLUE);
      line(x+19,y+7,x+23,y+11,BLUE);
      line(x+23,y+11,x+23,y+19,BLUE);
      line(x+23,y+19,x+19,y+23,BLUE);
      line(x+19,y+23,x+11,y+23,BLUE);
      line(x+11,y+23,x+7,y+19,BLUE);
      line(x+7,y+19,x+7,y+11,BLUE);
      line(x+7,y+11,x+11,y+7,BLUE);
    }
  }

  segment_update();

  dirty=0;
}

/* Joystick analog pins on this BoosterPack header must be checked against
 * the actual LaunchPad pinout. A10=P9.2, A4=P8.7 are the current assumptions.
 * ADC timeout prevents a failed conversion from freezing the game.
 */
static void adc_init() {
  P9DIR &= ~BIT2;

  P8DIR &= ~BIT7;

  P9SEL0 |= BIT2;
  P9SEL1 |= BIT2;

  P8SEL0 |= BIT7;
  P8SEL1 |= BIT7;

  ADC12CTL0 &= ~ADC12ENC;

  ADC12CTL0 = ADC12SHT0_8 | ADC12ON;

  ADC12CTL1 = ADC12SHP | ADC12SSEL_0;

  ADC12CTL2 = ADC12RES_2;

  ADC12MCTL0 = ADC12INCH_10;

  ADC12CTL0 |= ADC12ENC;

}

static uint8_t adc_read(uint8_t channel, uint16_t *out) {
  uint16_t timeout = 60000;

  ADC12CTL0 &= ~ADC12ENC;

  ADC12MCTL0 = (channel == 10) ? ADC12INCH_10 : ADC12INCH_4;

  ADC12IFGR0 &= ~ADC12IFG0;

  ADC12CTL0 |= ADC12ENC;

  ADC12CTL0 |= ADC12SC;

  while (!(ADC12IFGR0 & ADC12IFG0) && timeout) timeout--;

  if (!timeout) return 0;

  *out = ADC12MEM0;

  return 1;
}

static void joystick() {
  uint16_t x,y;
  int row,col,dx=0,dy=0;

  if (!adc_read(10,&x) || !adc_read(4,&y)) { adcOK=0;
    return;
  }

  adcOK=1;
  joyX=x;
  joyY=y;

  row=games[active].cursor/3;
  col=games[active].cursor%3;

  if (x < LOW) dx=-1;
  else if (x > HIGH) dx=1;

  if (y < LOW) dy=1;
  else if (y > HIGH) dy=-1;

  if (!dx && !dy) { joyNeutral=1;
    return;
  }

  if (!joyNeutral) return;

  if (dx) col += dx;
  else row += dy;

  if (col<0) col=0;
  if (col>2) col=2;

  if (row<0) row=0;
  if (row>2) row=2;

  if (games[active].cursor != (uint8_t)(row*3+col)) {
    games[active].cursor=(uint8_t)(row*3+col);

    dirty=1;

  }

  joyNeutral=0;

}

static void buttons() {
  static uint8_t previous1=0,previous2=0;

  static uint16_t hold1=0,hold2=0;

  uint8_t now1=!(P1IN&S1),now2=!(P1IN&S2);

  /* Called every ~20ms. Long press threshold ~800ms. */
  if(now1){if(hold1<500)hold1++;
  }

  if(now2){if(hold2<500)hold2++;
  }

  if(!now1 && previous1){
    if(hold1 >= 40)reset(&games[active]);

    else if(!now2)place();

    dirty=1;
    hold1=0;

  }

  if(!now2 && previous2){
    if(hold2 >= 40){games[active].mode^=1;
      reset(&games[active]);
    }

    else if(!now1){active^=1;
      joyNeutral=0;
    }

    dirty=1;
    hold2=0;

  }
  previous1=now1;
  previous2=now2;

}

int main() {
  WDTCTL=WDTPW|WDTHOLD;

  P1OUT &= ~(CLK|MOSI);
  P1DIR |= CLK|MOSI;

  P2OUT |= CS;
  P2OUT &= ~DC;
  P2DIR |= CS|DC;

  P9OUT |= RST;
  P9DIR |= RST;

  P1SEL0 &= ~(CLK|MOSI);
  P1SEL1 &= ~(CLK|MOSI);

  P2SEL0 &= ~(CS|DC);
  P2SEL1 &= ~(CS|DC);

  P9SEL0 &= ~RST;
  P9SEL1 &= ~RST;

  P1DIR &= ~(S1|S2);
  P1REN |= S1|S2;
  P1OUT |= S1|S2;

  PM5CTL0 &= ~LOCKLPM5;

  adc_init();

  games[0].mode=0;
  games[1].mode=1;

  reset(&games[0]);
  reset(&games[1]);

  delay_ms(100);
  tft_init();

  segment_init();

  dirty=1;

  while(1){
    if(dirty)draw_game();

    buttons();
    joystick();

    delay_ms(20);
  }
}
