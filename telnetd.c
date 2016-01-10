/*
 * Copyright (c) 2003, Adam Dunkels.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file is part of the uIP TCP/IP stack
 *
 * $Id: telnetd.c,v 1.2 2006/06/07 09:43:54 adam Exp $
 *
 */

#include "uip.h"
#include "telnetd.h"

#define ISO_nl       0x0a
#define ISO_cr       0x0d

#define STATE_NORMAL 0
#define STATE_IAC    1
#define STATE_WILL   2
#define STATE_WONT   3
#define STATE_DO     4
#define STATE_DONT   5
#define STATE_CLOSE  6

#define TELNET_IAC   255
#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254

/*    ,--------------------.
 *   /   Connection state  |
 *  /   ,------------------'
 *  |  /  Action
 *  | -|--
 *  |0.|  < Send >
 *  |  |  IAC WILL LINEMODE
 *  |  |  255 251  34
 *  | -|--
 *  |1.|  < Wait for >
 *  |  |  IAC DO  LINEMODE
 *  |  |  255 253 34
 *  |  |  [0] [1] [2]
 *  | -|--
 *  |2.|  < Send >
 *  |  |  IAC SB  LINEMODE EDIT 0 IAC SE
 *  |  |  255 250 34       1    0 255 240
 *  | -|--
 *  |3.|  < Wait for >
 *  |  |  IAC SB  LINEMODE EDIT 0|ACK IAC SE
 *  |  |  255 250 34       1    4     255 240
 *  |  |  [3] [4] [5]      [6]  [7]   [8] [9]
 *  | -|-- 
 *  |4.|  < Send >
 *  |  |  IAC SB  LINEMODE TRAPSIG 0 IAC SE
 *  |  |  255 250 34       2       0 255 240
 *  | -|--
 *  |5.|  < Wait for >
 *  |  |  IAC SB  LINEMODE TRAPSIG 0|ACK IAC SE
 *  |  |  255 250 34       2       4     255 240
 *   \/   [10][11][12]     [13]    [14]  [15][16]
 */

const u8_t rxExpected[] = {
	255, 253, 34,
	255, 250, 34, 1, 4, 255, 240,
	255, 250, 34, 2, 4, 255, 240
}
u8_t telnetState = 0;

/*---------------------------------------------------------------------------*/
void telnetd_init(void)
{
  uip_listen(HTONS(23));
  memb_init(&linemem);
  shell_init();
}
/*---------------------------------------------------------------------------*/
static void
senddata(void)
{
  static char *bufptr, *lineptr;
  static int buflen, linelen;
  
  bufptr = uip_appdata;
  buflen = 0;
  for(s.numsent = 0; s.numsent < TELNETD_CONF_NUMLINES &&
	s.lines[s.numsent] != NULL ; ++s.numsent) {
    lineptr = s.lines[s.numsent];
    linelen = strlen(lineptr);
    if(linelen > TELNETD_CONF_LINELEN) {
      linelen = TELNETD_CONF_LINELEN;
    }
    if(buflen + linelen < uip_mss()) {
      memcpy(bufptr, lineptr, linelen);
      bufptr += linelen;
      buflen += linelen;
    } else {
      break;
    }
  }
  uip_send(uip_appdata, buflen);
}
/*---------------------------------------------------------------------------*/
static void
closed(void)
{
  static unsigned int i;
  
  for(i = 0; i < TELNETD_CONF_NUMLINES; ++i) {
    if(s.lines[i] != NULL) {
      dealloc_line(s.lines[i]);
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
get_char(u8_t c)
{
  if(c == ISO_cr) {
    return;
  }
  
  s.buf[(int)s.bufptr] = c;
  if(s.buf[(int)s.bufptr] == ISO_nl ||
     s.bufptr == sizeof(s.buf) - 1) {
    if(s.bufptr > 0) {
      s.buf[(int)s.bufptr] = 0;
      /*      petsciiconv_topetscii(s.buf, TELNETD_CONF_LINELEN);*/
    }
    shell_input(s.buf);
    s.bufptr = 0;
  } else {
    ++s.bufptr;
  }
}
/*---------------------------------------------------------------------------*/
static void
sendopt(u8_t option, u8_t value)
{
  char *line;
  line = alloc_line();
  if(line != NULL) {
    line[0] = TELNET_IAC;
    line[1] = option;
    line[2] = value;
    line[3] = 0;
    sendline(line);
  }
}
/*---------------------------------------------------------------------------*/
static void
newdata(void)
{
  u16_t len;
  u8_t c;
  char *dataptr;
    
  
  len = uip_datalen();
  dataptr = (char *)uip_appdata;
  
  while(len > 0 && s.bufptr < sizeof(s.buf)) {
    c = *dataptr;
    ++dataptr;
    --len;
    switch(s.state) {
    case STATE_IAC:
      if(c == TELNET_IAC) {
	get_char(c);
	s.state = STATE_NORMAL;
      } else {
	switch(c) {
	case TELNET_WILL:
	  s.state = STATE_WILL;
	  break;
	case TELNET_WONT:
	  s.state = STATE_WONT;
	  break;
	case TELNET_DO:
	  s.state = STATE_DO;
	  break;
	case TELNET_DONT:
	  s.state = STATE_DONT;
	  break;
	default:
	  s.state = STATE_NORMAL;
	  break;
	}
      }
      break;
    case STATE_WILL:
      /* Reply with a DONT */
      sendopt(TELNET_DONT, c);
      s.state = STATE_NORMAL;
      break;
      
    case STATE_WONT:
      /* Reply with a DONT */
      sendopt(TELNET_DONT, c);
      s.state = STATE_NORMAL;
      break;
    case STATE_DO:
      /* Reply with a WONT */
      sendopt(TELNET_WONT, c);
      s.state = STATE_NORMAL;
      break;
    case STATE_DONT:
      /* Reply with a WONT */
      sendopt(TELNET_WONT, c);
      s.state = STATE_NORMAL;
      break;
    case STATE_NORMAL:
      if(c == TELNET_IAC) {
	s.state = STATE_IAC;
      } else {
	get_char(c);
      }
      break;
    }

    
  }
  
}
/*---------------------------------------------------------------------------*/
void
telnetd_appcall(void)
{
  static unsigned int i;
  if(uip_connected()) {
    /*    tcp_markconn(uip_conn, &s);*/
    for(i = 0; i < TELNETD_CONF_NUMLINES; ++i) {
      s.lines[i] = NULL;
    }
    s.bufptr = 0;
    s.state = STATE_NORMAL;

    shell_start();
  }

  if(s.state == STATE_CLOSE) {
    s.state = STATE_NORMAL;
    uip_close();
    return;
  }
  
  if(uip_closed() ||
     uip_aborted() ||
     uip_timedout()) {
    closed();
  }
  
  if(uip_acked()) {
    acked();
  }
  
  if(uip_newdata()) {
    newdata();
  }
  
  if(uip_rexmit() ||
     uip_newdata() ||
     uip_acked() ||
     uip_connected() ||
     uip_poll()) {
    senddata();
  }
}
/*---------------------------------------------------------------------------*/