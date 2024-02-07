/*
lgl_mcp3008.c
2021-01-17
Public Domain
*/

#include <stdlib.h>

#include <lgpio.h>

#include "lg_mcp3008.h"

typedef struct mcp3008_s
{
   int sbc;     // sbc connection
   int device;  // SPI device
   int channel; // SPI channel
   int speed;   // SPI bps
   int flags;   // SPI flags
   int spih;    // SPI handle
   callbk_t enable;
} mcp3008_t, *mcp3008_p;

mcp3008_p MCP3008_open(int sbc, int device, int channel, int speed, int flags)
{
   mcp3008_p s;

   s = calloc(1, sizeof(mcp3008_t));

   if (s == NULL) return NULL;

   s->sbc = sbc;         // sbc connection
   s->device = device;   // SPI device
   s->channel = channel; // SPI channel
   s->speed = speed;     // SPI speed
   s->flags = flags;     // SPI flags

   s->spih = lgSpiOpen(device, channel, speed, flags);

   if (s->spih < 0)
   {
      free(s);
      s = NULL;
   }

   return s;
}

mcp3008_p MCP3008_close(mcp3008_p s)
{
   if (s != NULL)
   {
      lgSpiClose(s->spih);
      free(s);
      s = NULL;
   }
   return s;
}

int MCP3008_read_single_ended(mcp3008_p s, int channel)
{
   int value;
   unsigned char buf[16];

   if (s == NULL) return -1;

   if ((channel < 0) || (channel > 7)) return -2;

   if (s->enable != NULL) s->enable(1);

   buf[0] = 1;
   buf[1] = 0x80 + (channel<<4);
   buf[2] = 0;

   lgSpiXfer(s->spih, buf, buf, 3);

   if (s->enable != NULL) s->enable(0);

   value = ((buf[1]&0x03)<<8) + buf[2];

   return value;
}

int MCP3008_read_differential_plus(mcp3008_p s, int channel)
{
   int value;
   unsigned char buf[16];

   if (s == NULL) return -1;

   if ((channel < 0) || (channel > 3)) return -2;

   if (s->enable != NULL) s->enable(1);

   buf[0] = 1;
   buf[1] = (channel<<5);
   buf[2] = 0;

   lgSpiXfer(s->spih, buf, buf, 3);

   if (s->enable != NULL) s->enable(0);

   value = ((buf[1]&0x03)<<8) + buf[2];
   return value;
}

int MCP3008_read_differential_minus(mcp3008_p s, int channel)
{
   int value;
   unsigned char buf[16];

   if (s == NULL) return -1;

   if ((channel < 0) || (channel > 3)) return -2;

   if (s->enable != NULL) s->enable(1);

   buf[0] = 1;
   buf[1] = (channel<<5) + 16;
   buf[2] = 0;

   lgSpiXfer(s->spih, buf, buf, 3);

   if (s->enable != NULL) s->enable(0);

   value = ((buf[1]&0x03)<<8) + buf[2];

   return value;
}

int MCP3008_set_enable(mcp3008_p s, callbk_t enable)
{
   s->enable = enable;

   return 0;
}


#ifdef EXAMPLE

/*
gcc -D EXAMPLE -o mcp3008 lgl_mcp3008.c -llgpio
./mcp3008
*/

#include <stdio.h>

#include <lgpio.h>

#include "lg_mcp3008.h"

int main(int argc, char *argv[])
{
   int sbc=-1;
   mcp3008_p adc=NULL;
   int v0, v1, v2, v3, v01p, v23m;

   adc = MCP3008_open(sbc, 0, 1, 50000, 0);

   if (adc == NULL) return -1;

   while (1)
   {
      lguSleep(0.2);

      v0 = MCP3008_read_single_ended(adc, 0);
      v1 = MCP3008_read_single_ended(adc, 1);
      v2 = MCP3008_read_single_ended(adc, 2);
      v3 = MCP3008_read_single_ended(adc, 3);
      v01p = MCP3008_read_differential_plus(adc, 0);
      v23m = MCP3008_read_differential_minus(adc, 1);

      printf("0=%4d 1=%4d diff=%4d 2=%4d 3=%4d diff=%4d\n",
         v0, v1, v01p, v2, v3, v23m);
   }

   return 0;
}

#endif

