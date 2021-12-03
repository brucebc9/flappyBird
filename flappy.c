
#include "neslib.h"
#include <stdlib.h>
#include <string.h>
#include "flappyBird_titlescreen.h"
#include "flappyBird_titlescreen2.h"

// 0 = horizontal mirroring
// 1 = vertical mirroring
#define NES_MIRRORING 1
#define MAX_SPEED 8
#define y_acceleration 2

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// setup Famitone library
//#link "famitone2.s"
void __fastcall__ famitone_update(void);
//#link "music_aftertherain.s"
extern char after_the_rain_music_data[];
//#link "demosounds.s"
extern char demo_sounds[];

// link the pattern table into CHR ROM
//#link "chr_generic.s"
//#include "flappyBird_PAL.pal"


/// GLOBAL VARIABLES

word x_scroll;		// X scroll amount in pixels
byte seg_height;	// segment height in metatiles
byte seg_height2;	// inverse segment height
byte last_seg_height;	// remembers previous segment height;
byte seg_width;		// segment width in metatiles of pipes
byte seg_char;		// character to draw
byte seg_palette;	// attribute table value
byte high;		// high position of pipe opening
byte low;		// low position of pipe opening
byte x_pos;		// defines position by 8*8 tile
byte x_exact_pos;	// defines positoin by exact pixel position
byte gameover;		// stores state of game, gameover is 1
word player_score;	// stores the player's score
char oam_id;		// ID of sprite
char pad;		// needed to read controller input
char last_controller_state; //keeps track of the previous controller input
char i;			// multi-use variable useful for loops
byte direction;


static unsigned char bright;
static unsigned char frame_cnt;
static unsigned char wait;
static int iy,dy;


// number of rows in scrolling playfield (without status bar)
#define PLAYROWS 27
#define CHAR(x) ((x+64))
#define COLOR_SCORE 1

//#define PLAYER_MAX_VELOCITY -10 // Max speed of the player; we won't let you go past this.
//#define PLAYER_VELOCITY_ACCEL 2 // How quickly do we get up to max velocity? 
// buffers that hold vertical slices of nametable data
#define FP_BITS 4
#define bird_color 0

char ntbuf1[PLAYROWS];	// left side
char ntbuf2[PLAYROWS];	// right side

// a vertical slice of attribute table entries
char attrbuf[PLAYROWS/4];

#define DEF_METASPRITE_2x2(name,code,pal)\
const unsigned char name[]={\
        0,      0,      (code),   bird_color, \
        8,      0,      (code)+1,   bird_color, \
        0,      8,      (code)+16,   bird_color, \
        8,      8,      (code)+17,   bird_color, \
        128};

DEF_METASPRITE_2x2(bird, 0x06, 0);
DEF_METASPRITE_2x2(birdFly, 0x02, 0);
DEF_METASPRITE_2x2(birdFly2, 0x04, 0);
DEF_METASPRITE_2x2(bird_down, 0x20, 0);
DEF_METASPRITE_2x2(enemyCloud, 0x5C, 0);
DEF_METASPRITE_2x2(bulletBill, 0x5E, 0);

const unsigned char* const birdSeq[16] = {
  bird, birdFly, birdFly2, 
  bird, birdFly, birdFly2, 
  bird, birdFly,
  bird, birdFly, birdFly2, 
  bird, birdFly, birdFly2, 
  bird, birdFly,
};

const unsigned char CoinsSpr[]={
  0,  0,0x7b,3,
  8,  0,0x82,3,
  16,  0,0x8B,3,
  24,  0,0x7D,3,
  32,  0,0x82,3,
  40,  0,0x7E,3,
  128
};

// sprite x/y positions
#define NUM_ACTORS 1
byte actor_x[NUM_ACTORS];
byte actor_y[NUM_ACTORS];
// actor x/y deltas per frame (signed)
sbyte actor_dx[NUM_ACTORS];
sbyte actor_dy[NUM_ACTORS];

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x22,			// background color

  0x29,0x38,0x0D,0x00,	// 
  0x0D,0x20,0x00,0x00,	// pipes & tiles
  0x0D,0x1A,0x39,0x00,
  0x10,0x00,0x30,0x00,

  0x0D,0x16,0x30,0x00,	// 
  0x0D,0x15,0x30,0x00,	//
  0x0D,0x12,0x30,0x00,
  0x29,0x00,0x0D	// player sprites
};

/// FUNCTIONS

// generate new random segment
void new_segment() {
  last_seg_height=seg_height;
  seg_height = (rand8() & 5)+1;
  //seg_height =5;
  seg_height2=(6-seg_height)+1;
  seg_width=8;
  seg_palette = 0;
  seg_char = 0xDF;
}

// function to write a string into the name table
//   adr = start address in name table
//   str = pointer to string
void put_str(unsigned int adr, const char *str) {
  vram_adr(adr);        // set PPU read/write address
  vram_write(str, strlen(str)); // write bytes to PPU
}

void draw_bcd_word(byte col, byte row, word bcd) {
  byte j;
  static char buf[5];
  buf[4] = CHAR('0');
  for (j=2; j<0x80; j--) {
    buf[j] = CHAR('0'+(bcd&0xf));
    bcd >>= 4;
  }
  vrambuf_put(NTADR_A(col, row), buf, 3);
}

void add_score(word bcd) {
  player_score = bcd_add(player_score, bcd);
  draw_bcd_word(3, 3, player_score);
}

// returns absolute value of x
byte iabs(int x) {
  return x >= 0 ? x : -x;
}

void reset_players() {
    actor_x[0] = 80;
    actor_y[0] = 80;
    actor_dx[0] = 0;
    actor_dy[0] = 1;
  
}

void clrscr() {
  vrambuf_clear();
  ppu_off();
  vram_adr(0x2000);
  vram_fill(8, 32*28);
  vram_adr(0x24c0);
  ppu_on_bg();
}

void pal_fade_to(unsigned to)
{
 if(!to) music_stop();

  while(bright!=to)
  {
    delay(4);
    if(bright<to) ++bright; else --bright;
    pal_bright(bright);
  }

  if(!bright)
  {
    ppu_off();
    set_vram_update(NULL);
    scroll(0,0);
  }
}

// convert from nametable address to attribute table address
word nt2attraddr(word a) {
  return (a & 0x2c00) | 0x3c0 |
    ((a >> 4) & 0x38) | ((a >> 2) & 0x07);
}

// draw metatile into nametable buffers
// y is the metatile coordinate (row * 2)
// ch is the starting tile index in the pattern table
void set_metatile(byte y, byte ch) {
  if (seg_width%2==0){
  ntbuf1[y*2] = ch;
  ntbuf1[y*2+1] = ch;
  ntbuf2[y*2] = ch+1;
  ntbuf2[y*2+1] = ch+1;
  }
  else{
  ntbuf1[y*2] = ch+1;
  ntbuf1[y*2+1] = ch+1;
  ntbuf2[y*2] = ch+2;
  ntbuf2[y*2+1] = ch+2;
  }
}

// set attribute table entry in attrbuf
// x and y are metatile coordinates
// pal is the index to set
void set_attr_entry(byte x, byte y, byte pal) {
  if (y&1) pal <<= 4;
  if (x&1) pal <<= 2;
  attrbuf[y/2] |= pal;
}

// fill ntbuf with tile data
// x = metatile coordinate
void fill_buffer(byte x) {
  byte i,y,j;
  // clear nametable buffers
  memset(ntbuf1, 0, sizeof(ntbuf1));
  memset(ntbuf2, 0, sizeof(ntbuf2));
  // draw a random star
 // ntbuf1[rand8() & 15] = '.';
  // draw segment slice to both nametable buffers
  for (i=0; i<seg_height; i++) {
    y = PLAYROWS/2-2-i;
    set_metatile(y, seg_char);
    set_attr_entry(x, y, seg_palette);
    }
  for (i=0; i<seg_height2; i++){
    j = PLAYROWS/2-13+i;
    set_metatile(j, seg_char);
    set_attr_entry(x, j, seg_palette);
  }
  
}
void fill_blank(byte x) {
  //byte i,y;
  // clear nametable buffers
  memset(ntbuf1, 0, sizeof(ntbuf1));
  memset(ntbuf2, 0, sizeof(ntbuf2));
  // draw a random star
  //ntbuf1[rand8() & 15] = '.';
  // draw segment slice to both nametable buffers
 /* for (i=0; i<seg_height; i++) {
    y = PLAYROWS/2-1-i;
    set_metatile(y, seg_char);
    set_attr_entry(x, y, seg_palette);
  }*/
  x=x;
}

// write attribute table buffer to vram buffer
void put_attr_entries(word addr) {
  byte i;
  for (i=0; i<PLAYROWS/4; i++) {
    VRAMBUF_PUT(addr, attrbuf[i], 0);
    addr += 8;
  }
  vrambuf_end();
}

// update the nametable offscreen
// called every 8 horizontal pixels
void update_offscreen() {
  register word addr;
  byte x;
  //byte wait;
  //byte i;
  // divide x_scroll by 8
  // to get nametable X position
  x = (x_scroll/8 + 32) & 63;
  // fill the ntbuf arrays with tiles
if(seg_width>=7)
  fill_buffer(x/2);

  
 
 else if(seg_width<8)
   fill_blank(x/2);
  
  // get address in either nametable A or B
  if (x < 32)
    addr = NTADR_A(x, 4);
  else
    addr = NTADR_B(x&31, 4);
  // draw vertical slice from ntbuf arrays to name table
  // starting with leftmost slice
  vrambuf_put(addr | VRAMBUF_VERT, ntbuf1, PLAYROWS);
  // then the rightmost slice
  vrambuf_put((addr+1) | VRAMBUF_VERT, ntbuf2, PLAYROWS);
  // compute attribute table address
  // then set attribute table entries
  // we update these twice to prevent right-side artifacts
  put_attr_entries(nt2attraddr(addr));
  // every 4 columns, clear attribute table buffer
  if ((x & 4) == 2) {
    memset(attrbuf, 0, sizeof(attrbuf));
  }
  // decrement segment width, create new segment when it hits zero
  if (--seg_width == 0) {
    new_segment();
  
  }
}

void update()
{

   low=209-(last_seg_height*16);
   high=low-74;
  
   if(x_scroll>160)
    {
     if ((x_pos>3&&x_pos<10) && (actor_y[0]<high || actor_y[0]>low+4))
      {
       //reset_players();
       sfx_play(1,0);
       pal_fade_to(8);
        gameover=1;
       //sfx_play(3,0);
      }
    } 
   if (actor_y[0]>210)
   {
     sfx_play(1,0);
     pal_fade_to(8);
  //reset_players();
     gameover=1;
     //sfx_play(32,0);
   }
  pal_fade_to(4);
  
}

void draw_sprite(){
  // draw and move all actors
  //if (actor_dy[0]>0){
   // direction=1;
//  }
  //else if (actor_dy[0]>0){
    //direction=1;
//  }
 // else if (actor_dy[0]<0){
  //  direction=0;
 // }
          
      for (i=0; i<NUM_ACTORS; i++) {
      byte runseq = actor_y[i] & 7;
      if (actor_dy[i] >= 0)
        runseq += 8;
      if (direction==1)
      oam_id = oam_meta_spr(actor_x[i], actor_y[i], oam_id, bird_down);
      oam_id = oam_meta_spr(actor_x[i], actor_y[i], oam_id, birdSeq[runseq]);
      actor_x[i] += actor_dx[i];
      actor_y[i] += actor_dy[i]; 
      }
  
  
  	// draw "coins" at the top in sprites
	oam_id = oam_meta_spr(24,8,oam_id, CoinsSpr);
	//temp1 = coins + 0xf0;
	//oam_id = oam_spr(64,16,temp1,3,oam_id);
}

void loser_screen()
{
  direction=1;
  while (1)
  {
    oam_id=4;
      if (actor_y[0]<211)
      {
      actor_dy[0]=2;
      actor_y[0] += actor_dy[0];
      draw_sprite();
      ppu_wait_nmi();
      vrambuf_clear();
      // split at sprite zero and set X scroll
     // split(x_scroll, 0);
      }

    {

      ppu_wait_frame();
      if(pad_trigger(0)&PAD_START) break;

    }
  }
}




void read_controller()
{
   last_controller_state=pad;
    // set player 0/1 velocity based on controller
    for (i=0; i<1; i++) 
    {
      // poll controller i (0-1)
      pad = pad_poll(0);
      // move actor[i] up/down
      if (pad&PAD_UP && actor_y[i]>8) {
        actor_dy[0]=-7;
       // sfx_play(3,0);
        }
      else if (pad&PAD_DOWN && actor_y[i]<212) actor_dy[i]=1;
      else if (actor_y[i]>211){
        actor_dy[0]=0;
      }
      else{
        actor_dy[0]=2;}
    }

}

void check_score(){
        if ((x_scroll & 7) == 0)
      {
        if(x_scroll>160)
         {     
          
         if (x_pos==8)
         {
         sfx_play(0,0);
         add_score(1);
         }
         
         } 
      }
}
  

// scrolls the screen left one pixel
void scroll_left() {
  // update nametable every 16 pixels
  if ((x_scroll & 15) == 0) {
    update_offscreen();
  }
  // increment x_scroll
  ++x_scroll;
}

// main loop, scrolls left continuously
void scroll_demo() {
  // get data for initial segment
  new_segment();
  last_seg_height=seg_height;
 // draw_bcd_word(4,2,CHAR("Hello"));
  //infinite loop
  while (1) 
  {
    oam_id = 4;
    read_controller(); 
    draw_sprite();

    x_pos = ((x_scroll+3)/8 + 32) & 15;
    x_exact_pos = ((x_scroll+3)/8 + 32) & 255;
    
    //updates score and collisions every 2 pixels
    //if ((x_scroll & 7) == 0)
    //update();
    
    if(gameover==1)
      break;

    check_score();
       
    // ensure VRAM buffer is cleared
    ppu_wait_nmi();
    vrambuf_clear();
 
    // split at sprite zero and set X scroll
    split(x_scroll, 0);
               
    // scroll to the left
    scroll_left();
    if(gameover==1)
      break;
  }
  
 loser_screen();

}



void title_screen(void)
{
  byte i;
  scroll(0,240);//title is aligned to the color attributes, so shift it a bit to the right

  vram_adr(NTADR_A(0,0));
  
  vram_unrle(flappyBird_titlescreen);

 
 //vram_adr(NAMETABLE_C);//clear second nametable, as it is visible in the jumping effect
  //vram_fill(40,1024);

  pal_bg(PALETTE);
  pal_bright(4);
  ppu_on_bg();
  delay(20);//delay just to make it look better

  iy=240<<FP_BITS;
  dy=-8<<FP_BITS;
  frame_cnt=0;
  wait=0;
  bright=4;
  
  while(1)
  {
    ppu_wait_frame();
    scroll(0,iy>>FP_BITS);
    if(pad_trigger(0)&PAD_START) break;
    iy+=dy;
    if(iy<0)
    {
      iy=0;
      dy=-dy>>1;
    }
    if(dy>(-8<<FP_BITS)) dy-=2;
    if(wait)
    {
      --wait;
    }
    else
    {
      pal_col(2,(frame_cnt&32)?0x1a:0x39);//blinking press start text
      ++frame_cnt;
    }
  }

  scroll(1,0);//if start is pressed, show the title at whole
  sfx_play(0,0);//titlescreen sound effect
  for(i=0;i<16;++i)//and blink the text faster
  {
    pal_col(2,i&1?0x1a:0x39);
    delay(4);
  }
  pal_fade_to(4);
  vrambuf_clear();
  ppu_off();
  vram_adr(NTADR_A(0,0));
  vram_fill(0, 32*32);
  vrambuf_flush();
  set_vram_update(updbuf);
}

// main function, run after console reset
void main(void) {

  // set palette colors
  pal_all(PALETTE);
  famitone_init(after_the_rain_music_data);
  sfx_init(demo_sounds);
  // set music callback function for NMI
  nmi_set_callback(famitone_update);
  // play music
 music_play(0);
 //title_screen();

while(1){
  
  clrscr();

  player_score = 0;
  gameover=0;
  x_scroll=0;
  direction=0;
  // set sprite 0
  oam_clear();
  reset_players();
  
  //sets sprite 0 to declare split line
  oam_spr(0, 29, 0xa0, 0x20, 0); 
  //put_str(NTADR_A(2,2), "Birdie");
  
  // set attributes
  // clear vram buffer
  //vrambuf_clear();
  set_vram_update(updbuf);
  add_score(0);
  // enable PPU rendering (turn on screen)
  ppu_on_all();
  
      
  // scroll window back and forth
  //main portion of the game
  scroll_demo();
  ppu_off();

}
    
}
