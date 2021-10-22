
/*
We demonstrate horizontal scrolling of two nametables.
Vertical mirroring is set, so nametables A and B are
to the left and right of each other.

New playfield data is randomly generated and updated
offscreen using the vrambuf module.
We update the nametable in 16-pixel-wide vertical strips,
using 2x2 blocks of tiles ("metatiles").

*/

#include "neslib.h"
#include <string.h>

// 0 = horizontal mirroring
// 1 = vertical mirroring
#define NES_MIRRORING 1

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// link the pattern table into CHR ROM
//#link "chr_generic.s"

/// GLOBAL VARIABLES

word x_scroll;		// X scroll amount in pixels
byte seg_height;	// segment height in metatiles
byte seg_width;		// segment width in metatiles
byte seg_char;		// character to draw
byte seg_palette;	// attribute table value

// number of rows in scrolling playfield (without status bar)
#define PLAYROWS 27

// buffers that hold vertical slices of nametable data
char ntbuf1[PLAYROWS];	// left side
char ntbuf2[PLAYROWS];	// right side

// a vertical slice of attribute table entries
char attrbuf[PLAYROWS/4];

#define DEF_METASPRITE_2x1(name,code,pal)\
const unsigned char name[]={\
        0,      0,      (code),   3, \
        8,      0,      (code)+1,   2, \
        0,      0,      (code),   2, \
        0,      0,      (code),   2, \
        128};

DEF_METASPRITE_2x1(bird, 0x8f, 0);
// sprite x/y positions
#define NUM_ACTORS 1
byte actor_x[NUM_ACTORS];
byte actor_y[NUM_ACTORS];
// actor x/y deltas per frame (signed)
sbyte actor_dx[NUM_ACTORS];
sbyte actor_dy[NUM_ACTORS];
/// FUNCTIONS

void reset_players() {
    actor_x[0] = 120;
    actor_y[0] = 60;
    actor_dx[0] = 1;
    actor_dy[0] = -1;
  
    actor_x[1] = 128;
    actor_y[1] = 128;
    actor_dx[1] = 1;
    actor_dy[1] = -3;
  
}

// convert from nametable address to attribute table address
word nt2attraddr(word a) {
  return (a & 0x2c00) | 0x3c0 |
    ((a >> 4) & 0x38) | ((a >> 2) & 0x07);
}

// generate new random segment
void new_segment() {
  seg_height = (rand8() &6);
 //seg_width = (rand8() & 3) + 1;
  seg_width=7;
  //seg_palette = rand8() & 3;
  seg_palette = rand8() & 0;
  seg_char = 0xf4;
}

// draw metatile into nametable buffers
// y is the metatile coordinate (row * 2)
// ch is the starting tile index in the pattern table
void set_metatile(byte y, byte ch) {
  ntbuf1[y*2] = ch;
  ntbuf1[y*2+1] = ch+1;
  ntbuf2[y*2] = ch+2;
  ntbuf2[y*2+1] = ch+3;
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
  byte i,y;
  // clear nametable buffers
  memset(ntbuf1, 0, sizeof(ntbuf1));
  memset(ntbuf2, 0, sizeof(ntbuf2));
  // draw a random star
  //ntbuf1[rand8() & 15] = '.';
  // draw segment slice to both nametable buffers
  for (i=0; i<seg_height; i++) {
    y = PLAYROWS/2-1-i;
    set_metatile(y, seg_char);
    set_attr_entry(x, y, seg_palette);
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
if(seg_width>=6)
  fill_buffer(x/2);
 
 else if(seg_width<6)
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
    char i;
  char oam_id;
  char pad;
  // get data for initial segment
  new_segment();
  x_scroll = 0;
  // infinite loop
  while (1) {
    // ensure VRAM buffer is cleared
        oam_id = 0;
      

    // set player 0/1 velocity based on controller
   // for (i=0; i<2; i++) {
      // poll controller i (0-1)
      pad = pad_poll(0);
      // move actor[i] up/down
      if (pad&PAD_UP && actor_x[i]>7) actor_dx[i]=-1;
      else if (pad&PAD_DOWN && actor_x[i]<214) actor_dx[i]=1;
      else actor_dx[i]=0;
 
      if (pad&PAD_A && actor_x[i]) actor_dx[i]=-2;
      else if (pad&PAD_DOWN && actor_y[i]<212) actor_dy[i]=2;
      else actor_dy[i]=0;
   // }
    // draw and move all actors
    for (i=0; i<NUM_ACTORS; i++) {
      byte runseq = actor_x[i] & 7;
      if (actor_dx[i] >= 0)
        runseq += 8;
      oam_id = oam_meta_spr(actor_x[i], actor_y[i], oam_id, bird);
      actor_x[i] += actor_dx[i];
      actor_y[i] += actor_dy[i];
    ppu_wait_nmi();
    vrambuf_clear();
    // split at sprite zero and set X scroll
    split(x_scroll, 0);
    // scroll to the left
    scroll_left();  
  }
}
}

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x13,			// background color

  0x28,0x2D,0x1A,0x00,	// ladders and pickups
  0x0D,0x20,0x28,0x00,	// floor blocks
  0x00,0x10,0x20,0x00,
  0x06,0x16,0x26,0x00,

  0x16,0x35,0x24,0x00,	// enemy sprites
  0x00,0x37,0x25,0x00,	// rescue person
  0x0D,0x2D,0x1A,0x00,
  0x0D,0x27,0x2A	// player sprites
};

// function to write a string into the name table
//   adr = start address in name table
//   str = pointer to string
void put_str(unsigned int adr, const char *str) {
  vram_adr(adr);        // set PPU read/write address
  vram_write(str, strlen(str)); // write bytes to PPU
}




// main function, run after console reset
void main(void) {

  // set palette colors
  pal_all(PALETTE);
  new_segment();
  x_scroll = 0;

  
  // set attributes
  vram_adr(0x23c0);
  vram_fill(0x55, 8);
  
  // set sprite 0
  oam_clear();
  reset_players();
  //oam_spr(1, 30, 0xa0, 0, 0);
  //oam_spr(actor_x[1], actor_y[1], 0xb6, 0, 2);

 
  
  // clear vram buffer
  vrambuf_clear();
  set_vram_update(updbuf);
  
  // enable PPU rendering (turn on screen)
  ppu_on_all();
      


  // scroll window back and forth
   scroll_demo();
    
}
