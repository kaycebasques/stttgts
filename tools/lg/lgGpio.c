/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

#include "lgpio.h"

#include "lgDbg.h"
#include "lgGpio.h"
#include "lgHdl.h"
#include "lgPthAlerts.h"
#include "lgPthTx.h"

#define LG_CHIP_MODE_UNKNOWN  0

#define LG_CHIP_BIT_INPUT  (1<<0)
#define LG_CHIP_BIT_OUTPUT (1<<1)
#define LG_CHIP_BIT_ALERT  (1<<2)
#define LG_CHIP_BIT_GROUP  (1<<3)

void xWrite(lgChipObj_p chip, int gpio, int value);

callbk_t lgGpioSamplesFunc = NULL;
void *lgGpioSamplesUserdata = NULL;

static inline void xSetBit(uint64_t *b, int n)
{
   *b |= ((uint64_t)1 << n);
}

static inline void xChangeBit(uint64_t *b, int n)
{
   *b ^= ((uint64_t)1 << n);
}

static inline void xClearBit(uint64_t *b, int n)
{
   *b &= ~((uint64_t)1 << n);
}

static inline int xTestBit(uint64_t b, int n)
{
   return !!(b & ((uint64_t)1 << n));
}

static inline void xAssignBit(uint64_t *b, int n, int value)
{
   if (value) xSetBit(b, n);
   else       xClearBit(b, n);
}

static void _lgGpiochipClose(void *objPtr)
{
   int i;
   lgChipObj_p chip;

   chip = objPtr;

   if (chip == NULL) return;

   /* stop any PWM on chip */
   lgPthTxStop(chip);

   /* stop any event reads on chip */
   lgPthAlertStop(chip);

   usleep(100000); /* should be long enough for PWM/events to stop */

   for (i=0; i<chip->lines; i++)
   {
      if (chip->LineInf[i].mode != LG_CHIP_MODE_UNKNOWN)
      {
         /* free GPIO */
         LG_DBG(LG_DEBUG_ALLOC, "free GPIO: %d mode %d (%d of %d)",
            i,
            chip->LineInf[i].mode,
            chip->LineInf[i].offset+1,
            chip->LineInf[i].group_size);

         if (chip->LineInf[i].offset == 0)
         {
            /* group leader - remove all group resources */

            if (chip->LineInf[i].fd != -1)
            {
               LG_DBG(LG_DEBUG_ALLOC, "close fd: %d", chip->LineInf[i].fd);
               close(chip->LineInf[i].fd);
               chip->LineInf[i].fd = -1;
            }

            if (chip->LineInf[i].offsets_p)
            {
               LG_DBG(LG_DEBUG_ALLOC, "free offsets: *%p",
                  (void*)chip->LineInf[i].offsets_p);
               free(chip->LineInf[i].offsets_p);
               chip->LineInf[i].offsets_p = NULL;
            }

            if (chip->LineInf[i].values_p)
            {
               LG_DBG(LG_DEBUG_ALLOC, "free values: *%p",
                  (void*)chip->LineInf[i].values_p);

               free(chip->LineInf[i].values_p);
               chip->LineInf[i].values_p = NULL;
            }
         }
      }
   }

   LG_DBG(LG_DEBUG_ALLOC, "free LineInf: *%p", (void*)chip->LineInf);

   if (chip->LineInf) free(chip->LineInf);

   LG_DBG(LG_DEBUG_ALLOC, "close chip fd: %d", chip->fd);

   close(chip->fd);
}

static int xGpioHandleRequest(
   lgChipObj_p chip, struct gpio_v2_line_request *req)
{
   int i, gpio, mode;
   int status;
   uint32_t *offsets_p;
   uint64_t *values_p;

   LG_DBG(LG_DEBUG_USER, "chip=*%p req=*%p", (void*)chip, (void*)req);

   LG_DBG(LG_DEBUG_ALLOC, "request %d with flags %llu GPIO=[%s]",
      req->num_lines, req->config.flags, lgDbgInt2Str(req->num_lines,
      (int *)req->offsets));

   status = ioctl(chip->fd, GPIO_V2_GET_LINE_IOCTL, req);

   if (status == 0)
   {
      offsets_p = calloc(req->num_lines, sizeof(uint32_t));

      values_p = calloc(1, sizeof(uint64_t));

      if ((offsets_p == NULL) || (values_p == NULL))
      {
         free(offsets_p); // passing NULL is legal
         free(values_p); // passing NULL is legal
         close(req->fd);
         return LG_NOT_ENOUGH_MEMORY;
      }

      LG_DBG(LG_DEBUG_ALLOC, "alloc offsets: *%p, values: *%p",
         (void*)offsets_p, (void*)values_p);

      mode = 0;

      if (req->config.flags & GPIO_V2_LINE_FLAG_INPUT)
         mode |= LG_CHIP_BIT_INPUT;

      if (req->config.flags & GPIO_V2_LINE_FLAG_OUTPUT)
         mode |= LG_CHIP_BIT_OUTPUT;

      if (req->num_lines > 1)
         mode |= LG_CHIP_BIT_GROUP;

      for (i=0; i<req->num_lines; i++)
      {
         gpio = req->offsets[i];

         chip->LineInf[gpio].mode = mode;
         chip->LineInf[gpio].group_size = req->num_lines;
         chip->LineInf[gpio].fd = req->fd;

         chip->LineInf[gpio].offset = i;

         chip->LineInf[gpio].offsets_p = offsets_p;
         chip->LineInf[gpio].values_p = values_p;

         offsets_p[i] = gpio;
      }
   }
   else
   {
      if (errno == EBUSY) status = LG_GPIO_BUSY;
      else
      {
         LG_DBG(LG_DEBUG_ALWAYS, "%s", strerror(errno));
         status = LG_UNEGPECTED_ERROR;
      }
   }
   return status;
}

uint64_t xMakeFlags(uint64_t s)
{
   uint64_t f = 0;

   if (s & LG_RISING_EDGE)     f |= GPIO_V2_LINE_FLAG_EDGE_RISING;
   if (s & LG_FALLING_EDGE)    f |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
   if (s & LG_SET_ACTIVE_LOW)  f |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
   if (s & LG_SET_OPEN_DRAIN)  f |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
   if (s & LG_SET_OPEN_SOURCE) f |= GPIO_V2_LINE_FLAG_OPEN_SOURCE;
   if (s & LG_SET_PULL_UP)     f |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
   if (s & LG_SET_PULL_DOWN)   f |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
   if (s & LG_SET_PULL_NONE)   f |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
   if (s & LG_SET_INPUT)       f |= GPIO_V2_LINE_FLAG_INPUT;
   if (s & LG_SET_OUTPUT)      f |= GPIO_V2_LINE_FLAG_OUTPUT;

   /*
   if (s & LG_SET_REALTIME_CLOCK) f |=
      GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME;
   */


   return f;
}

uint64_t xMakeStatus(uint64_t f)
{
   uint64_t s = 0;

   if (f & GPIO_V2_LINE_FLAG_USED)           s |= LG_GPIO_IS_KERNEL;
   if (f & GPIO_V2_LINE_FLAG_ACTIVE_LOW)     s |= LG_GPIO_IS_ACTIVE_LOW;
   if (f & GPIO_V2_LINE_FLAG_INPUT)          s |= LG_GPIO_IS_INPUT;
   if (f & GPIO_V2_LINE_FLAG_OUTPUT)         s |= LG_GPIO_IS_OUTPUT;
   if (f & GPIO_V2_LINE_FLAG_EDGE_RISING)    s |= LG_GPIO_IS_RISING_EDGE;
   if (f & GPIO_V2_LINE_FLAG_EDGE_FALLING)   s |= LG_GPIO_IS_FALLING_EDGE;
   if (f & GPIO_V2_LINE_FLAG_OPEN_DRAIN)     s |= LG_GPIO_IS_OPEN_DRAIN;
   if (f & GPIO_V2_LINE_FLAG_OPEN_SOURCE)    s |= LG_GPIO_IS_OPEN_SOURCE;
   if (f & GPIO_V2_LINE_FLAG_BIAS_PULL_UP)   s |= LG_GPIO_IS_PULL_UP;
   if (f & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN) s |= LG_GPIO_IS_PULL_DOWN;
   if (f & GPIO_V2_LINE_FLAG_BIAS_DISABLED)  s |= LG_GPIO_IS_PULL_NONE;

   /*
   if (f & GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME)
      s |= LG_GPIO_IS_REALTIME_CLOCK;
   */

   return s;
}


static int xClaim(
   lgChipObj_p chip,
   int lFlags,
   int size,
   const int *gpios,
   const int *values)
{
   int i;
   uint64_t m=0, v=0;

   struct gpio_v2_line_request req;

   LG_DBG(LG_DEBUG_USER, "chip=*%p size=%d gpios=[%s] values=[%s] lFlags=%x",
      chip, size, lgDbgInt2Str(size, (int*)gpios),
      lgDbgInt2Str(size, (int*)values), lFlags);

   memset(&req, 0, sizeof(req));

   if (size && (size <= GPIO_V2_LINES_MAX))
   {
      for (i=0; i<size; i++)
      {
         if (((unsigned)gpios[i] > chip->lines) ||
             (chip->LineInf[gpios[i]].banned)) return LG_NOT_PERMITTED;

         req.offsets[i] = gpios[i];
      }

      if (values != NULL)
      {
         req.config.num_attrs = 1;
	 req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;

         for (i=0; i<size; i++)
         {
            xSetBit(&m, i);
            xAssignBit(&v, i, values[i]);
         }
      }

      req.config.flags = xMakeFlags(lFlags);

      req.config.attrs[0].mask = m;
      req.config.attrs[0].attr.values = v;

      strncpy(req.consumer, chip->userLabel, sizeof(req.consumer));

      req.num_lines = size;

      return xGpioHandleRequest(chip, &req);
   }
   else return LG_BAD_GROUP_SIZE;
}

// implement public API

static int xSetAsFree(lgChipObj_p chip, int gpio)
{
   lgLineInf_p GPIO;
   int i, g;
   lgTxRec_p pTx;
   lgAlertRec_p pEvt;

   LG_DBG(LG_DEBUG_TRACE, "chip=*%p gpio=%d", (void*)chip, gpio);

   if (gpio >= chip->lines) return LG_BAD_GPIO_NUMBER;

   GPIO = &chip->LineInf[gpio];

   if (GPIO->mode == LG_CHIP_MODE_UNKNOWN)
   {
      LG_DBG(LG_DEBUG_ALLOC,
         "free unallocated GPIO: %d (mode %d)", gpio, GPIO->mode);

      return LG_OKAY;
   }

   if (GPIO->mode & LG_CHIP_BIT_ALERT)
   {
      LG_DBG(LG_DEBUG_ALLOC,
         "free alert GPIO: %d (mode %d)", gpio, GPIO->mode);

      if ((pEvt = lgGpioGetAlertRec(chip, gpio)) != NULL)
         pEvt->active = 0;

      for (i=0; i<10; i++)
      {
         LG_DBG(LG_DEBUG_ALLOC, "waiting for inactive: %d", gpio);

         usleep(200);

         if ((pEvt = lgGpioGetAlertRec(chip, gpio)) == NULL) break;
      }

      close(GPIO->fd);

      GPIO->mode = LG_CHIP_MODE_UNKNOWN;

      return LG_OKAY;
   }

   // legal as long as group leader (a singleton is always a leader)

   if (gpio == GPIO->offsets_p[0])
   {
      LG_DBG(LG_DEBUG_ALLOC,
         "group free GPIO: %d (mode %d)", gpio, GPIO->mode);

      for (i=0; i<GPIO->group_size; i++)
      {
         g = GPIO->offsets_p[i];

         lgPthTxLock();

         if ((pTx = lgGpioGetTxRec(chip, g, LG_TX_PWM)) != NULL)
         {
            pTx->active = 0;
            LG_DBG(LG_DEBUG_ALLOC, "set PWM inactive: %d", gpio);
         }

         if ((pTx = lgGpioGetTxRec(chip, g, LG_TX_WAVE)) != NULL)
         {
            pTx->active = 0;
            LG_DBG(LG_DEBUG_ALLOC, "set PWM inactive: %d", gpio);
         }

         lgPthTxUnlock();

         chip->LineInf[g].mode = LG_CHIP_MODE_UNKNOWN;

         LG_DBG(LG_DEBUG_ALLOC, "set unused: %d", g);
      }

      LG_DBG(LG_DEBUG_ALLOC, "close fd: %d", GPIO->fd);

      close(GPIO->fd);

      LG_DBG(LG_DEBUG_ALLOC, "free offsets: *%p, values: *%p",
         (void*)GPIO->offsets_p, (void*)GPIO->values_p);

      free(GPIO->offsets_p);
      free(GPIO->values_p);

      return LG_OKAY;
   }

   return LG_NOT_GROUP_LEADER;
}

static int xSetAsOutput(
   lgChipObj_p chip, int lFlags, int size,
   const int *gpios, const int *values)
{
   int mode;

   LG_DBG(LG_DEBUG_TRACE, "chip=*%p gpio=%d", (void*)chip, gpios[0]);

   lFlags &= ~(LG_SET_INPUT);
   lFlags |= LG_SET_OUTPUT;

   if (size == 1)
   {
      /* single GPIO */

      mode = chip->LineInf[gpios[0]].mode;

      if (mode & LG_CHIP_BIT_OUTPUT)
      {
         /* nothing to do, already an output */
         return LG_OKAY;
      }
      else
      {
         if (!(mode & LG_CHIP_BIT_GROUP))
         {
            /* do auto free if singleton */
            LG_DBG(LG_DEBUG_ALLOC, "set as output auto free %d", gpios[0]);
            xSetAsFree(chip, gpios[0]);
         }

         return xClaim(chip, lFlags, size, gpios, values);
      }
   }
   else
   {
      return xClaim(chip, lFlags, size, gpios, values);
   }
}

static int xSetAsInput(
   lgChipObj_p chip, int lFlags, int size, const int *gpios)
{
   int mode;

   LG_DBG(LG_DEBUG_TRACE, "chip=*%p gpio=%d", (void*)chip, gpios[0]);

   lFlags &= ~(LG_SET_OUTPUT);
   lFlags |= LG_SET_INPUT;

   if (size == 1)
   {
      /* single GPIO */

      mode = chip->LineInf[gpios[0]].mode;

      if (mode & LG_CHIP_BIT_INPUT)
      {
         /* nothing to do, already an input */
         return LG_OKAY;
      }
      else
      {
         if (!(mode & LG_CHIP_BIT_GROUP))
         {
            /* do auto free if singleton */
            LG_DBG(LG_DEBUG_ALLOC, "set as input auto free %d", gpios[0]);
            xSetAsFree(chip, gpios[0]);
         }

         return xClaim(chip, lFlags, size, gpios, NULL);
      }
   }
   else
   {
      return xClaim(chip, lFlags, size, gpios, NULL);
   }
}

static int xSetAsPwm(
   lgChipObj_p chip,
   int gpio,
   int micros_on,
   int micros_off,
   int micros_offset,
   int cycles)
{
   lgLineInf_p GPIO;
   lgTxRec_p p;
   int zero = 0;
   int status = 0;

   LG_DBG(LG_DEBUG_TRACE, "chip=*%p gpio=%d", (void*)chip, gpio);

   GPIO = &chip->LineInf[gpio];

   /* do we need to change the mode? */

   if (GPIO->mode == LG_CHIP_MODE_UNKNOWN)
   {
      xSetAsOutput(
         chip, GPIO_V2_LINE_FLAG_OUTPUT, 1, &gpio, &zero);
   }
   else
   {
      if (!(GPIO->mode & LG_CHIP_BIT_OUTPUT))
      {
         /* not an output */
         if (!(GPIO->mode & LG_CHIP_BIT_GROUP))
         {
            /* is a singleton */
            xSetAsOutput(
               chip, GPIO_V2_LINE_FLAG_OUTPUT, 1, &gpio, &zero);
         }
      }
   }

   /* return if in wrong mode */

   if (!(GPIO->mode & LG_CHIP_BIT_OUTPUT)) return LG_GPIO_NOT_AN_OUTPUT;

   lgPthTxLock();

   if (((p = lgGpioGetTxRec(chip, gpio, LG_TX_PWM)) != NULL) && p->active)
   {
      if (micros_on || micros_off)
      {
         if ((micros_on + micros_off) > lgMinTxDelay)
         {
            /* delete prior pending entry if it has infinite cycles */

            if ((p->entries > 1) && (p->cycles[p->entries-1] == -1))
            {
               --p->entries;
            }

            if (p->entries < LG_TX_BUF)
            {
               p->micros_on[p->entries] = micros_on;
               p->micros_off[p->entries] = micros_off;
               if (cycles) p->cycles[p->entries] = cycles;
               else p->cycles[p->entries] = -1;
               p->entries++;
               status = LG_TX_BUF - p->entries;
            }
            else status = LG_TX_QUEUE_FULL;
         }
         else status = LG_BAD_PWM_MICROS;
      }
      else
      {
         p->active = 0;
      }

      lgPthTxUnlock();
   }
   else
   {
      lgPthTxUnlock();

      if ((micros_on + micros_off) > lgMinTxDelay)
      {
         lgGpioCreateTxRec(
            chip, gpio, micros_on, micros_off, micros_offset, cycles);
         status = LG_TX_BUF - 1;
      }
      else return LG_BAD_PWM_MICROS;
   }

   return status;
}

static int xWave(
   lgChipObj_p chip, int gpio, int count, lgPulse_p pulses)
{
   lgLineInf_p GPIO;
   lgTxRec_p p;
   int i;
   int zero = 0;
   int status = 0;
   lgPulse_p pulsesTmp;

   LG_DBG(LG_DEBUG_TRACE, "chip=*%p gpio=%d", (void*)chip, gpio);

   GPIO = &chip->LineInf[gpio];

   if (!(GPIO->mode & LG_CHIP_BIT_OUTPUT))
   {
      /* not an output */
      if (!(GPIO->mode & LG_CHIP_BIT_GROUP))
      {
         /* auto set output if a singleton */
         xSetAsOutput(
            chip, GPIO_V2_LINE_FLAG_OUTPUT, 1, &gpio, &zero);
      }
   }

   if (!(GPIO->mode & LG_CHIP_BIT_OUTPUT)) return LG_GPIO_NOT_AN_OUTPUT;

   pulsesTmp = malloc(count * sizeof(lgPulse_t));

   if (!pulsesTmp) return LG_NO_MEMORY;

   for (i=0; i<count; i++)
   {
      pulsesTmp[i] = pulses[i];
   }

   lgPthTxLock();

   if (((p = lgGpioGetTxRec(chip, gpio, LG_TX_WAVE)) != NULL) && p->active)
   {
      if (p->entries < LG_TX_BUF)
      {
         p->pulses[p->entries] = pulsesTmp;
         p->num_pulses[p->entries] = count;
         p->entries++;
         status = LG_TX_BUF - p->entries;
      }
      else
      {
         free(pulsesTmp);
         status = LG_TX_QUEUE_FULL;
      }

      lgPthTxUnlock();
   }
   else
   {
      lgPthTxUnlock();

      lgGroupCreateWaveRec(chip, gpio, count, pulsesTmp);
      status = LG_TX_BUF - 1;
   }

   return status;
}

void xWrite(lgChipObj_p chip, int gpio, int value)
{
   lgLineInf_p GPIO;
   struct gpio_v2_line_values lv;
   uint64_t m = 0;

   LG_DBG(LG_DEBUG_TRACE, "chip=*%p gpio=%d value=%d", (void*)chip, gpio, value);

   GPIO = &chip->LineInf[gpio];

   xSetBit(&m, GPIO->offset);
   xAssignBit(GPIO->values_p, GPIO->offset, value);

   lv.mask = m;
   lv.bits = *GPIO->values_p;

   ioctl(GPIO->fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);
}

void xGroupWrite(
   lgChipObj_p chip, int gpio, uint64_t groupBits, uint64_t groupMask)
{
   int i;
   lgLineInf_p GPIO;
   struct gpio_v2_line_values lv;

   GPIO = &chip->LineInf[gpio];

   for (i=0; i<GPIO->group_size; i++)
   {
      if (groupMask & ((uint64_t)1<<i))
      {
         xAssignBit(GPIO->values_p, i, groupBits & (1<<i));
      }
   }

   lv.mask = groupMask;
   lv.bits = *GPIO->values_p;

   ioctl(GPIO->fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);
}

// public API

int lgGpiochipOpen(int gpioDev)
{
   int fd;
   int handle;
   lgChipObj_p chip;
   struct gpiochip_info info;
   char chipName[128];
   lgLineInf_p lInf; /* used to stop -fanalyzer warning */

   LG_DBG(LG_DEBUG_TRACE, "gpioDev=%d", gpioDev);

   memset(&info, 0, sizeof(info));

   if (gpioDev < 0)
      PARAM_ERROR(LG_BAD_GPIOCHIP, "bad gpioDev (%d)", gpioDev);

   sprintf(chipName, "/dev/gpiochip%d", gpioDev);

   fd = open(chipName, O_RDWR | O_CLOEXEC);

   if (fd < 0)
      PARAM_ERROR(LG_CANNOT_OPEN_CHIP, "can't open gpiochip (%s)", chipName);

   if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info))
   {
      close(fd);
      PARAM_ERROR(LG_NOT_A_GPIOCHIP, "ioct failed (%s)", chipName);
   }

   handle = lgHdlAlloc(
      LG_HDL_TYPE_GPIO, sizeof(lgChipObj_t), (void**)&chip, _lgGpiochipClose);

   if (handle < 0)
   {
      close(fd);
      return LG_NOT_ENOUGH_MEMORY;
   }

   chip->gpiochip = gpioDev;

   chip->handle = handle;

   /* calloc will zero all members */
   lInf = calloc(info.lines, sizeof(lgLineInf_t));

   if (lInf == NULL)
   {
      lgHdlFree(handle, LG_HDL_TYPE_GPIO);
      ALLOC_ERROR(LG_NOT_ENOUGH_MEMORY, "can't allocate gpio lines");
   }

   chip->LineInf = lInf;

   LG_DBG(LG_DEBUG_ALLOC, "alloc LineInf: *%p", (void*)chip->LineInf);

   strncpy(chip->name,  info.name,  sizeof(chip->name));
   strncpy(chip->label, info.label, sizeof(chip->label));

   chip->lines = info.lines;

   chip->fd = fd;

   strncpy(chip->userLabel, "lg", sizeof(chip->userLabel));

   lgPthTxStart();

   lgPthAlertStart();

   return handle;
}

int lgGpiochipClose(int handle)
{
   int status;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d", handle);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      status = lgHdlFree(handle, LG_HDL_TYPE_GPIO);

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioSetBannedState(int handle, int gpio, int banned)
{
   int status;
   lgLineInf_p GPIO;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE,
      "handle=%d gpio=%d banned=%d", handle, gpio, banned);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];
         GPIO->banned = banned;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}



int lgGpioGetChipInfo(int handle, lgChipInfo_p chipInfo)
{
   int status;
   struct gpiochip_info cinfo;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d chipInfo=*%p", handle, (void*)chipInfo);

   memset(&cinfo, 0, sizeof(cinfo));

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      status = ioctl(chip->fd, GPIO_GET_CHIPINFO_IOCTL, &cinfo);

      if (status == 0)
      {
         chipInfo->lines = cinfo.lines;
         strncpy(chipInfo->name, cinfo.name, sizeof(chipInfo->name));
         strncpy(chipInfo->label, cinfo.label, sizeof(chipInfo->label));
      }
      else status = LG_BAD_CHIPINFO_IOCTL;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioGetLineInfo(int handle, int gpio, lgLineInfo_p lineInfo)
{
   int status;
   struct gpio_v2_line_info linfo;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d lineInfo=*%p",
      handle, gpio, (void*)lineInfo);

   memset(&linfo, 0, sizeof(linfo));

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         linfo.offset = gpio;

         status = ioctl(chip->fd, GPIO_V2_GET_LINEINFO_IOCTL, &linfo);

         if (status == 0)
         {
            lineInfo->offset = linfo.offset;
            lineInfo->lFlags = xMakeStatus(linfo.flags) |
               (chip->LineInf[gpio].mode << 8);
            strncpy(lineInfo->name, linfo.name, sizeof(lineInfo->name));
            strncpy(lineInfo->user, linfo.consumer, sizeof(lineInfo->user));
         }
         else status = LG_BAD_LINEINFO_IOCTL;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioGetMode(int handle, int gpio)
{
   int status;
   struct gpio_v2_line_info linfo;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d", handle, gpio);

   memset(&linfo, 0, sizeof(linfo));

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         linfo.offset = gpio;

         status = ioctl(chip->fd, GPIO_V2_GET_LINEINFO_IOCTL, &linfo);

         if (status == 0)
         {
            status = xMakeStatus(linfo.flags) | (chip->LineInf[gpio].mode << 8);
         }
         else status = LG_BAD_LINEINFO_IOCTL;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGroupFree(int handle, int gpio)
{
   int status;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d", handle, gpio);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         if (chip->LineInf[gpio].mode != LG_CHIP_MODE_UNKNOWN)
         {
            if (chip->LineInf[gpio].offset == 0)
            {
               status = xSetAsFree(chip, gpio);
            }
            else status = LG_NOT_GROUP_LEADER;
         }
         else status = LG_GPIO_NOT_ALLOCATED;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioFree(int handle, int gpio)
{
   int status;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d", handle, gpio);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         if (chip->LineInf[gpio].mode != LG_CHIP_MODE_UNKNOWN)
         {
            if (chip->LineInf[gpio].offset == 0)
            {
               status = xSetAsFree(chip, gpio);
            }
            else status = LG_NOT_GROUP_LEADER;
         }
         else status = LG_GPIO_NOT_ALLOCATED;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioSetUser(int handle, const char *user)
{
   int status;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d user=%s", handle, user);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      strncpy(chip->userLabel, user, sizeof(chip->userLabel)-1);

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGroupClaimInput(
   int handle, int lFlags, int size, const int *gpios)
{
   int status;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d lFlags=%x size=%d gpios=[%s]",
      handle, lFlags, size, lgDbgInt2Str(size, (int*)gpios));

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      status = xSetAsInput(chip, lFlags, size, gpios);

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioClaimInput(int handle, int lFlags, int gpio)
{
   int gpios[]={gpio};

   LG_DBG(LG_DEBUG_TRACE,
      "handle=%d lFlags=%x gpio=%d", handle, lFlags, gpio);

   return lgGroupClaimInput(handle, lFlags, 1, gpios);
}


int lgGroupClaimOutput(
   int handle, int lFlags, int size, const int *gpios, const int *values)
{
   int status;
   lgChipObj_p chip;

   LG_DBG(LG_DEBUG_TRACE,
      "handle=%d lFlags=%x size=%d gpios=[%s] values=[%s]",
       handle, lFlags, size, lgDbgInt2Str(size, (int*)gpios),
      lgDbgInt2Str(size, (int*)values));

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      status = xSetAsOutput(chip, lFlags, size, gpios, values);

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioClaimOutput(int handle, int lFlags, int gpio, int value)
{
   int gpios[]={gpio};
   int values[]={value};

   LG_DBG(LG_DEBUG_TRACE,
      "handle=%d lFlags=%x gpio=%d value=%d",
       handle, lFlags, gpio, value);

   return lgGroupClaimOutput(handle, lFlags, 1, gpios, values);
}

int lgGpioClaimAlert(
   int handle, int lFlags, int eFlags, int gpio, int nfyHandle)
{
   int status;
   int mode;
   uint64_t flags;
   uint32_t *offsets_p;
   lgChipObj_p chip;
   lgAlertRec_p p;
   struct gpio_v2_line_request req;
   uint64_t *values_p;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d lFlags=%x eFlags=%x gpio=%d nfyHandle=%d",
      handle, lFlags, eFlags, gpio, nfyHandle);

   memset(&req, 0, sizeof(req));

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         mode = chip->LineInf[gpio].mode;

         if (!(mode & LG_CHIP_BIT_GROUP))
         {
            LG_DBG(LG_DEBUG_ALLOC, "set as alert auto free %d", gpio);

            xSetAsFree(chip, gpio);

            flags = xMakeFlags(lFlags|eFlags) | GPIO_V2_LINE_FLAG_INPUT;

            req.num_lines = 1;
            req.offsets[0] = gpio;
            req.config.flags = flags;
            strncpy(req.consumer, chip->userLabel, sizeof(req.consumer));

            LG_DBG(LG_DEBUG_TRACE, "flags %"PRIu64, flags);

            status = ioctl(chip->fd, GPIO_V2_GET_LINE_IOCTL, &req);

            if (status == 0)
            {
               offsets_p = calloc(1, sizeof(uint32_t));

               if (offsets_p == NULL)
               {
                  close(req.fd);
                  return LG_NOT_ENOUGH_MEMORY;
               }

               chip->LineInf[gpio].offsets_p = offsets_p;

               values_p = calloc(1, sizeof(values_p));

               if (values_p == NULL)
               {
                  free(offsets_p);
                  chip->LineInf[gpio].offsets_p = NULL;
                  chip->LineInf[gpio].values_p = NULL;
                  close(req.fd);
                  return LG_NOT_ENOUGH_MEMORY;
               }

               chip->LineInf[gpio].values_p = values_p;

               chip->LineInf[gpio].mode = LG_CHIP_BIT_ALERT;
               chip->LineInf[gpio].eFlags = eFlags;
               chip->LineInf[gpio].group_size = 1;
               chip->LineInf[gpio].fd = req.fd;
               chip->LineInf[gpio].offset = 0;

               if ((p = lgGpioGetAlertRec(chip, gpio)) != NULL)
                  p->active= 0;

               lgGpioCreateAlertRec(
                  chip, gpio, &chip->LineInf[gpio], nfyHandle);
            }
            else status = LG_BAD_EVENT_REQUEST;
         }
         else status = LG_INVALID_GROUP_ALERT;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgTxPulse(
   int handle,
   int gpio,
   int micros_on,
   int micros_off,
   int micros_offset,
   int cycles)
{
   lgChipObj_p chip;
   int status;

   LG_DBG(LG_DEBUG_TRACE,
      "handle=%d gpio=%d on=%d off=%d offset=%d cycles=%d",
      handle, gpio, micros_on, micros_off, micros_offset, cycles);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         status = xSetAsPwm(
            chip, gpio, micros_on, micros_off, micros_offset, cycles);
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgTxWave(int handle, int gpio, int count, lgPulse_p pulses)
{
   lgChipObj_p chip;
   int status;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d count=%d", handle, gpio, count);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         status = xWave(chip, gpio, count, pulses);
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgTxBusy(int handle, int gpio, int kind)
{
   lgChipObj_p chip;
   lgTxRec_p p;
   int status;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d kind=%d", handle, gpio, kind);

   if ((kind != LG_TX_PWM) && (kind != LG_TX_WAVE))
      PARAM_ERROR(LG_BAD_TX_TYPE, "bad tx kind (%d)", kind);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         lgPthTxLock();

         if (((p = lgGpioGetTxRec(chip, gpio, kind)) != NULL) && p->active)
            status = 1;

         lgPthTxUnlock();
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgTxRoom(int handle, int gpio, int kind)
{
   lgChipObj_p chip;
   lgTxRec_p p;
   int status;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d kind=%d", handle, gpio, kind);

   if ((kind != LG_TX_PWM) && (kind != LG_TX_WAVE))
      PARAM_ERROR(LG_BAD_TX_TYPE, "bad tx kind (%d)", kind);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         lgPthTxLock();

         if (((p = lgGpioGetTxRec(chip, gpio, kind)) != NULL) && p->active)
            status = LG_TX_BUF - p->entries;
         else
            status = LG_TX_BUF;

         lgPthTxUnlock();
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgTxPwm(
   int handle,
   int gpio,
   float pwmFrequency,
   float pwmDutyCycle,
   int pwmOffset,
   int pwmCycles)
{
   int micros, micros_on, micros_off;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d freq=%f duty=%f",
      handle, gpio, pwmFrequency, pwmDutyCycle);

   if (pwmFrequency == 0.0) return lgTxPulse(handle, gpio, 0, 0, 0, 0);

   if ((pwmFrequency < 0.1) || (pwmFrequency > 1e4))
      PARAM_ERROR(LG_BAD_PWM_FREQ,
         "bad PWM frequency (%f)", pwmFrequency);

   if ((pwmDutyCycle < 0.0) || (pwmDutyCycle > 100.0))
      PARAM_ERROR(LG_BAD_PWM_DUTY,
         "bad PWM duty cycle (%f)", pwmDutyCycle);

   micros = ((1.0e6 / pwmFrequency) + 0.5);
   micros_on = ((pwmDutyCycle / 100.0 * micros) + 0.5);
   micros_off = micros - micros_on;

   return lgTxPulse(
      handle, gpio, micros_on, micros_off, pwmOffset, pwmCycles);
}

int lgTxServo(
   int handle,
   int gpio,
   int pulseWidth,
   int servoFrequency,
   int servoOffset,
   int servoCycles)

{
   int micros, micros_on, micros_off;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d freq=%d width=%d",
      handle, gpio, servoFrequency, pulseWidth);

   if (pulseWidth == 0) return lgTxPulse(handle, gpio, 0, 0, 0, 0);

   if ((servoFrequency < 40) || (servoFrequency > 500))
      PARAM_ERROR(LG_BAD_SERVO_FREQ,
         "bad servo frequency (%d)", servoFrequency);

   if ((pulseWidth < 500) || (pulseWidth > 2500))
      PARAM_ERROR(LG_BAD_SERVO_WIDTH,
         "bad servo pulse width (%d)", pulseWidth);

   micros = ((1.0e6 / servoFrequency) + 0.5);
   micros_on = pulseWidth;
   micros_off = micros - micros_on;

   if (micros_off < 0)
      PARAM_ERROR(LG_BAD_SERVO_WIDTH,
         "bad servo pulse width (%d)", pulseWidth);

   return lgTxPulse(
      handle, gpio, micros_on, micros_off, servoOffset, servoCycles);
}

int lgGpioRead(int handle, int gpio)
{
   int status;
   lgLineInf_p GPIO;
   lgChipObj_p chip;
   struct gpio_v2_line_values lv;
   uint64_t m = 0;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d", handle, gpio);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];

         if (GPIO->mode == LG_CHIP_MODE_UNKNOWN)
         {
            status =  xSetAsInput(chip, 0, 1, &gpio);
         }

         if (GPIO->mode != LG_CHIP_MODE_UNKNOWN)
         {
            xSetBit(&m, GPIO->offset);

            lv.mask = m;

            status = ioctl(GPIO->fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &lv);

            if (status == 0)
               status = xTestBit(lv.bits, GPIO->offset);
            else
               status = LG_BAD_READ;
         }
         else status = LG_GPIO_NOT_ALLOCATED;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioWrite(int handle, int gpio, int value)
{
   int status;
   lgLineInf_p GPIO;
   lgChipObj_p chip;
   struct gpio_v2_line_values lv;
   uint64_t m = 0;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d value=%d", handle, gpio, value);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];

         if (GPIO->mode & LG_CHIP_BIT_OUTPUT)
         {
            xSetBit(&m, GPIO->offset);
            xAssignBit(GPIO->values_p, GPIO->offset, value);

            lv.mask = m;
            lv.bits = *GPIO->values_p;

            status = ioctl(
               GPIO->fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);

            if (status)
            {
               LG_DBG(LG_DEBUG_ALWAYS, "%s", strerror(errno));
               status = LG_BAD_WRITE;
            }
         }
         else
         {
            /* not an output */
            if (!(GPIO->mode & LG_CHIP_BIT_GROUP))
            {
               /* auto set output if a singleton */
               status = xSetAsOutput(chip, 0, 1, &gpio, &value);
            }
            else status = LG_GPIO_NOT_AN_OUTPUT;
         }
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}


int lgGroupRead(
   int handle, int gpio, uint64_t *bits)
{
   lgLineInf_p GPIO;
   int status;
   lgChipObj_p chip;
   struct gpio_v2_line_values lv;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d bits=%"PRIx64"",
      handle, gpio, *bits);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];

         if (GPIO->offset == 0)
         {
            if (GPIO->mode != LG_CHIP_MODE_UNKNOWN)
            {
               lv.mask = -1;
               status = ioctl(GPIO->fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &lv);

               if (status == 0)
               {
                  *bits = lv.bits;

                  status = GPIO->group_size;
               }
               else status = LG_BAD_READ;
            }
            else status = LG_GPIO_NOT_ALLOCATED;
          }
          else status = LG_NOT_GROUP_LEADER;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGroupWrite(
   int handle, int gpio, uint64_t groupBits, uint64_t groupMask)
{
   int status;
   int i;
   lgLineInf_p GPIO;
   lgChipObj_p chip;
   struct gpio_v2_line_values lv;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d bits=%"PRIx64" mask=%"PRIx64"",
      handle, gpio, groupBits, groupMask);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];

         if (GPIO->offset == 0)
         {
            if (GPIO->mode != LG_CHIP_MODE_UNKNOWN)
            {
               for (i=0; i<GPIO->group_size; i++)
               {
                  if (groupMask & (1<<i))
                  {
                     xAssignBit(GPIO->values_p, i, groupBits & (1<<i));
                  }
               }

               lv.mask = groupMask;
               lv.bits = groupBits;

               status = ioctl(GPIO->fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);

               if (status == 0)
               {
                  status = GPIO->group_size;
               }
               else status = LG_BAD_WRITE;
            }
            else status = LG_GPIO_NOT_ALLOCATED;
         }
         else status = LG_NOT_GROUP_LEADER;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioSetDebounce(int handle, int gpio, int debounce_us)
{
   int status;
   lgLineInf_p GPIO;
   lgChipObj_p chip;
   lgAlertRec_p p;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d debounce_us=%d",
      handle, gpio, debounce_us);

   if ((debounce_us < 0) || (debounce_us > LG_MAX_MICS_DEBOUNCE))
      PARAM_ERROR(LG_BAD_DEBOUNCE_MICS, "bad debounce (%d)", debounce_us);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];

         GPIO->debounce_us = debounce_us;

         if ((p = lgGpioGetAlertRec(chip, gpio)) != NULL)
            p->debounce_nanos = debounce_us * 1e3;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioSetWatchdog(int handle, int gpio, int watchdog_us)
{
   int status;
   lgLineInf_p GPIO;
   lgChipObj_p chip;
   lgAlertRec_p p;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d watchdog_us=%d",
      handle, gpio, watchdog_us);

   if ((watchdog_us < 0) || (watchdog_us > LG_MAX_MICS_WATCHDOG))
      PARAM_ERROR(LG_BAD_WATCHDOG_MICS, "bad watchdog (%d)", watchdog_us);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];

         GPIO->watchdog_us = watchdog_us;

         if ((p = lgGpioGetAlertRec(chip, gpio)) != NULL)
            p->watchdog_nanos = watchdog_us * 1e3;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

int lgGpioSetAlertsFunc(
   int handle, int gpio, lgGpioAlertsFunc_t cbf, void *userdata)
{
   lgLineInf_p GPIO;
   lgChipObj_p chip;
   int status;

   LG_DBG(LG_DEBUG_TRACE, "handle=%d gpio=%d func=*%p userdata=*%p",
      handle, gpio, cbf, userdata);

   status = lgHdlGetLockedObj(handle, LG_HDL_TYPE_GPIO, (void **)&chip);

   if (status == LG_OKAY)
   {
      if (gpio < chip->lines)
      {
         GPIO = &chip->LineInf[gpio];
         GPIO->alertFunc = cbf;
         GPIO->userdata = userdata;
      }
      else status = LG_BAD_GPIO_NUMBER;

      lgHdlUnlock(handle);
   }

   return status;
}

void lgGpioSetSamplesFunc(lgGpioAlertsFunc_t cbf, void *userdata)
{
   LG_DBG(LG_DEBUG_TRACE, "func=*%p userdata=*%p", cbf, userdata);

   lgGpioSamplesFunc = cbf;
   lgGpioSamplesUserdata = userdata;
}

