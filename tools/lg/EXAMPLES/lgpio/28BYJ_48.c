/*
28BYJ_48.c
2020-11-21
Public Domain

http://abyz.me.uk/lg/lgpio.html

gcc -Wall -o 28BYJ_48 28BYJ_48.c -llgpio

./28BYJ_48 [chip] gpio1 gpio2 gpio3 gpio4

E.g.

./28BYJ_48 20 21 22 23 # gpiochip=0 gpio1=20 gpio2=21 gpio3=22 gpio4=23

./28BYJ_48 2 7 5 11 3  # gpiochip=2 gpio1=7 gpio2=5 gpio3=11 gpio4=3
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include <lgpio.h>

int coils[] = {7, 3, 11, 9, 13, 12, 14, 6};

int forward(int *pos)
{
   if (++(*pos) > 7) *pos = 0;
   return coils[*pos];
}

int backward(int *pos)
{
   if (--(*pos) < 0) *pos = 7;
   return coils[*pos];
}

int main(int argc, char *argv[])
{
   int h, err, i;
   int pos=0;
   int chip = 0;
   int GPIO[4];
   int ZEROS[4] = {0, 0, 0, 0};

   if (argc == 6) /* chip gpio1 gpio2 gpio3 gpio4 */
   {
      chip = atoi(argv[1]);
      GPIO[0] = atoi(argv[2]);
      GPIO[1] = atoi(argv[3]);
      GPIO[2] = atoi(argv[4]);
      GPIO[3] = atoi(argv[5]);
   }

   else if (argc == 5) /* gpio1 gpio2 gpio3 gpio4 (chip 0) */
   {
      chip = 0;
      GPIO[0] = atoi(argv[1]);
      GPIO[1] = atoi(argv[2]);
      GPIO[2] = atoi(argv[3]);
      GPIO[3] = atoi(argv[4]);
   }

   else
   {
      fprintf(stderr, "Usage: ./28BYJ_48 [chip] gpio1 gpio2 gpio3 gpio4\n");
      return -1;
   }

   h = lgGpiochipOpen(chip);

   if (h < 0)
   {
      fprintf(stderr, "can't open gpiochip %d (%s)\n",
         chip, lguErrorText(h));
      return -1;
   }

   err = lgGroupClaimOutput(h, 0, 4, GPIO, ZEROS);

   if (err < 0)
   {
      fprintf(stderr, "can't claim GPIO for output (%s)\n",
         lguErrorText(err));
      return -1;
   }

   for (i=0; i<4096; i++)
   {
      lgGroupWrite(h, GPIO[0],  forward(&pos), -1);
      lguSleep(0.0015);
   }

   for (i=0; i<4096; i++)
   {
      lgGroupWrite(h, GPIO[0],  backward(&pos), -1);
      lguSleep(0.0015);
   }

   lgGroupFree(h, GPIO[0]);

   lgGpiochipClose(h);
}

