//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency commands
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "proxmark3.h"
#include "data.h"
#include "graph.h"
#include "ui.h"
#include "cmdparser.h"
#include "cmdmain.h"
#include "cmddata.h"
#include "cmdlf.h"
#include "cmdlfawid26.h"
#include "cmdlfhid.h"
#include "cmdlfti.h"
#include "cmdlfem4x.h"
#include "cmdlfhitag.h"
#include "cmdlft55xx.h"
#include "cmdlfpcf7931.h"
#include "cmdlfio.h"

static int CmdHelp(const char *Cmd);

/* send a command before reading */
int CmdLFCommandRead(const char *Cmd)
{
  static char dummy[3];

  dummy[0]= ' ';

  UsbCommand c = {CMD_MOD_THEN_ACQUIRE_RAW_ADC_SAMPLES_125K};
  sscanf(Cmd, "%"lli" %"lli" %"lli" %s %s", &c.arg[0], &c.arg[1], &c.arg[2],(char*)(&c.d.asBytes),(char*)(&dummy+1));
  // in case they specified 'h'
  strcpy((char *)&c.d.asBytes + strlen((char *)c.d.asBytes), dummy);
  SendCommand(&c);
  return 0;
}

int CmdFlexdemod(const char *Cmd)
{
	int i;
	for (i = 0; i < GraphTraceLen; ++i) {
		if (GraphBuffer[i] < 0) {
			GraphBuffer[i] = -1;
		} else {
			GraphBuffer[i] = 1;
		}
	}

	#define LONG_WAIT 100
	int start;
	for (start = 0; start < GraphTraceLen - LONG_WAIT; start++) {
		int first = GraphBuffer[start];
		for (i = start; i < start + LONG_WAIT; i++) {
			if (GraphBuffer[i] != first) {
				break;
			}
		}
		if (i == (start + LONG_WAIT)) {
		break;
		}
	}
	if (start == GraphTraceLen - LONG_WAIT) {
		//PrintAndLog("nothing to wait for");
		return 0;
	}

	GraphBuffer[start] = 2;
	GraphBuffer[start+1] = -2;
	uint8_t bits[64] = {0x00};

	int bit, sum;
	i = start;
	for (bit = 0; bit < 64; bit++) {
		sum = 0;
		for (int j = 0; j < 16; j++) {
			sum += GraphBuffer[i++];
		}

		bits[bit] = (sum > 0) ? 1 : 0;

		PrintAndLog("bit %d sum %d", bit, sum);
	}

	for (bit = 0; bit < 64; bit++) {
		int j;
		int sum = 0;
		for (j = 0; j < 16; j++) {
			sum += GraphBuffer[i++];
		}
		if (sum > 0 && bits[bit] != 1) {
			PrintAndLog("oops1 at %d", bit);
		}
		if (sum < 0 && bits[bit] != 0) {
			PrintAndLog("oops2 at %d", bit);
		}
	}

	// HACK writing back to graphbuffer.
	GraphTraceLen = 32*64;
	i = 0;
	int phase = 0;
	for (bit = 0; bit < 64; bit++) {
	
		phase = (bits[bit] == 0) ? 0 : 1;
		
		int j;
		for (j = 0; j < 32; j++) {
			GraphBuffer[i++] = phase;
			phase = !phase;
		}
	}

	RepaintGraphWindow();
	return 0;
}
  
int CmdIndalaDemod(const char *Cmd)
{
	// Usage: recover 64bit UID by default, specify "224" as arg to recover a 224bit UID

	int state = -1;
	int count = 0;
	int i, j;

	// worst case with GraphTraceLen=64000 is < 4096
	// under normal conditions it's < 2048

	uint8_t rawbits[4096];
	int rawbit = 0;
	int worst = 0, worstPos = 0;
 // PrintAndLog("Expecting a bit less than %d raw bits", GraphTraceLen / 32);
	for (i = 0; i < GraphTraceLen-1; i += 2) {
		count += 1;
		if ((GraphBuffer[i] > GraphBuffer[i + 1]) && (state != 1)) {
			if (state == 0) {
				for (j = 0; j <  count - 8; j += 16) {
					rawbits[rawbit++] = 0;
				}
				if ((abs(count - j)) > worst) {
					worst = abs(count - j);
					worstPos = i;
				}
			}
			state = 1;
			count = 0;
			} else if ((GraphBuffer[i] < GraphBuffer[i + 1]) && (state != 0)) {
				if (state == 1) {
					for (j = 0; j <  count - 8; j += 16) {
						rawbits[rawbit++] = 1;
					}
					if ((abs(count - j)) > worst) {
						worst = abs(count - j);
						worstPos = i;
					}
				}
				state = 0;
				count = 0;
			}
	}
  
	if (rawbit>0){
		PrintAndLog("Recovered %d raw bits, expected: %d", rawbit, GraphTraceLen/32);
		PrintAndLog("worst metric (0=best..7=worst): %d at pos %d", worst, worstPos);
	} else {
		return 0;
	}

	// Finding the start of a UID
	int uidlen, long_wait;
	if (strcmp(Cmd, "224") == 0) {
		uidlen = 224;
		long_wait = 30;
	} else {
		uidlen = 64;
		long_wait = 29;
	}

	int start;
	int first = 0;
	for (start = 0; start <= rawbit - uidlen; start++) {
		first = rawbits[start];
		for (i = start; i < start + long_wait; i++) {
			if (rawbits[i] != first) {
				break;
			}
		}
		if (i == (start + long_wait)) {
			break;
		}
	}
  
	if (start == rawbit - uidlen + 1) {
		//PrintAndLog("nothing to wait for");
		return 0;
	}

	// Inverting signal if needed
	if (first == 1) {
		for (i = start; i < rawbit; i++) {
			rawbits[i] = !rawbits[i];
		}
	}

	// Dumping UID
	uint8_t bits[224] = {0x00};
	char showbits[225] = {0x00};
	int bit;
	i = start;
	int times = 0;
	
	if (uidlen > rawbit) {
		PrintAndLog("Warning: not enough raw bits to get a full UID");
		for (bit = 0; bit < rawbit; bit++) {
			bits[bit] = rawbits[i++];
			// As we cannot know the parity, let's use "." and "/"
			showbits[bit] = '.' + bits[bit];
		}
		showbits[bit+1]='\0';
		PrintAndLog("Partial UID=%s", showbits);
		return 0;
	} else {
		for (bit = 0; bit < uidlen; bit++) {
			bits[bit] = rawbits[i++];
			showbits[bit] = '0' + bits[bit];
		}
		times = 1;
	}
  
	//convert UID to HEX
	uint32_t uid1, uid2, uid3, uid4, uid5, uid6, uid7;
	int idx;
	uid1 = uid2 = 0;
	
	if (uidlen == 64){
		for( idx=0; idx<64; idx++) {
			if (showbits[idx] == '0') {
				uid1 = (uid1<<1) | (uid2>>31);
				uid2 = (uid2<<1) | 0;
			} else {
				uid1 = (uid1<<1) | (uid2>>31);
				uid2 = (uid2<<1) | 1;
			}
		}
		PrintAndLog("UID=%s (%x%08x)", showbits, uid1, uid2);
	}
	else {
		uid3 = uid4 = uid5 = uid6 = uid7 = 0;

		for( idx=0; idx<224; idx++) {
			uid1 = (uid1<<1) | (uid2>>31);
			uid2 = (uid2<<1) | (uid3>>31);
			uid3 = (uid3<<1) | (uid4>>31);
			uid4 = (uid4<<1) | (uid5>>31);
			uid5 = (uid5<<1) | (uid6>>31);
			uid6 = (uid6<<1) | (uid7>>31);
			
			if (showbits[idx] == '0') 
				uid7 = (uid7<<1) | 0;
			else 
				uid7 = (uid7<<1) | 1;
		}
		PrintAndLog("UID=%s (%x%08x%08x%08x%08x%08x%08x)", showbits, uid1, uid2, uid3, uid4, uid5, uid6, uid7);
	}

	// Checking UID against next occurrences
	int failed = 0;
	for (; i + uidlen <= rawbit;) {
		failed = 0;
		for (bit = 0; bit < uidlen; bit++) {
			if (bits[bit] != rawbits[i++]) {
				failed = 1;
				break;
			}
		}
		if (failed == 1) {
			break;
		}
		times += 1;
	}

	PrintAndLog("Occurrences: %d (expected %d)", times, (rawbit - start) / uidlen);

	// Remodulating for tag cloning
	// HACK: 2015-01-04 this will have an impact on our new way of seening lf commands (demod) 
	// since this changes graphbuffer data.
	GraphTraceLen = 32 * uidlen;
	i = 0;
	int phase = 0;
	for (bit = 0; bit < uidlen; bit++) {
		if (bits[bit] == 0) {
		  phase = 0;
		} else {
		  phase = 1;
		}
		int j;
		for (j = 0; j < 32; j++) {
		  GraphBuffer[i++] = phase;
		  phase = !phase;
		}
	}

	RepaintGraphWindow();
	return 1;
}

int CmdIndalaClone(const char *Cmd)
{
	UsbCommand c;
	unsigned int uid1, uid2, uid3, uid4, uid5, uid6, uid7;

	uid1 =  uid2 = uid3 = uid4 = uid5 = uid6 = uid7 = 0;
	int n = 0, i = 0;

	if (strchr(Cmd,'l') != 0) {
		while (sscanf(&Cmd[i++], "%1x", &n ) == 1) {
			uid1 = (uid1 << 4) | (uid2 >> 28);
			uid2 = (uid2 << 4) | (uid3 >> 28);
			uid3 = (uid3 << 4) | (uid4 >> 28);
			uid4 = (uid4 << 4) | (uid5 >> 28);
			uid5 = (uid5 << 4) | (uid6 >> 28);
			uid6 = (uid6 << 4) | (uid7 >> 28);
			uid7 = (uid7 << 4) | (n & 0xf);
		}
		PrintAndLog("Cloning 224bit tag with UID %x%08x%08x%08x%08x%08x%08x", uid1, uid2, uid3, uid4, uid5, uid6, uid7);
		c.cmd = CMD_INDALA_CLONE_TAG_L;
		c.d.asDwords[0] = uid1;
		c.d.asDwords[1] = uid2;
		c.d.asDwords[2] = uid3;
		c.d.asDwords[3] = uid4;
		c.d.asDwords[4] = uid5;
		c.d.asDwords[5] = uid6;
		c.d.asDwords[6] = uid7;
	} else {
		while (sscanf(&Cmd[i++], "%1x", &n ) == 1) {
			uid1 = (uid1 << 4) | (uid2 >> 28);
			uid2 = (uid2 << 4) | (n & 0xf);
		}
		PrintAndLog("Cloning 64bit tag with UID %x%08x", uid1, uid2);
		c.cmd = CMD_INDALA_CLONE_TAG;
		c.arg[0] = uid1;
		c.arg[1] = uid2;
	}

	SendCommand(&c);
	return 0;
}

int CmdLFRead(const char *Cmd)
{
	UsbCommand c = {CMD_ACQUIRE_RAW_ADC_SAMPLES_125K};

	// 'h' means higher-low-frequency, 134 kHz
	if(*Cmd == 'h') {
		c.arg[0] = 1;
	} else if (*Cmd == '\0') {
		c.arg[0] = 0;
	} else if (sscanf(Cmd, "%"lli, &c.arg[0]) != 1) {
		PrintAndLog("Samples 1: 'lf read'");
		PrintAndLog("        2: 'lf read h'");
		PrintAndLog("        3: 'lf read <divisor>'");
		return 0;
	}
	SendCommand(&c);
	WaitForResponse(CMD_ACK,NULL);

	CmdSamples("");
	ShowGraphWindow();
	return 0;
}

static void ChkBitstream(const char *str)
{
	int i;

	/* convert to bitstream if necessary */
	for (i = 0; i < (int)(GraphTraceLen / 2); i++){
		if (GraphBuffer[i] > 1 || GraphBuffer[i] < 0) {
			CmdBitstream(str);
			break;
		}
	}
}

int CmdLFSim(const char *Cmd)
{
	int i,j;
	static int gap;

	sscanf(Cmd, "%i", &gap);

	/* convert to bitstream if necessary */
	ChkBitstream(Cmd);

	printf("Sending [%d bytes]", GraphTraceLen);
	for (i = 0; i < GraphTraceLen; i += USB_CMD_DATA_SIZE) {
		UsbCommand c={CMD_DOWNLOADED_SIM_SAMPLES_125K, {i, 0, 0}};

		for (j = 0; j < USB_CMD_DATA_SIZE; j++) {
			c.d.asBytes[j] = GraphBuffer[i+j];
		}
		SendCommand(&c);
		WaitForResponse(CMD_ACK,NULL);
		printf(".");
	}
	
	printf("\n");
	PrintAndLog("Starting to simulate");
	UsbCommand c = {CMD_SIMULATE_TAG_125K, {GraphTraceLen, gap, 0}};
	SendCommand(&c);
	return 0;
}

int CmdLFSimBidir(const char *Cmd)
{
  // Set ADC to twice the carrier for a slight supersampling
  // HACK: not implemented in ARMSRC.
  PrintAndLog("Not implemented yet.");
  UsbCommand c = {CMD_LF_SIMULATE_BIDIR, {47, 384, 0}};
  SendCommand(&c);
  return 0;
}

/* simulate an LF Manchester encoded tag with specified bitstream, clock rate and inter-id gap */
int CmdLFSimManchester(const char *Cmd)
{
	static int clock, gap;
	static char data[1024], gapstring[8];

	sscanf(Cmd, "%i %s %i", &clock, &data[0], &gap);

	ClearGraph(0);

	for (int i = 0; i < strlen(data) ; ++i)
		AppendGraph(0, clock, data[i]- '0');

	CmdManchesterMod("");

	RepaintGraphWindow();

	sprintf(&gapstring[0], "%i", gap);
	CmdLFSim(gapstring);
	return 0;
}

int CmdLFSnoop(const char *Cmd)
{
	UsbCommand c = {CMD_LF_SNOOP_RAW_ADC_SAMPLES};

	// 'h' means higher-low-frequency, 134 kHz
	c.arg[0] = 0;
	c.arg[1] = -1;

	if (*Cmd == 'l') {
		sscanf(Cmd, "l %"lli, &c.arg[1]);
	} else if (*Cmd == 'h') {
		c.arg[0] = 1;
		sscanf(Cmd, "h %"lli, &c.arg[1]);
	} else if (sscanf(Cmd, "%"lli" %"lli, &c.arg[0], &c.arg[1]) < 1) {
		PrintAndLog("usage 1:  snoop");
		PrintAndLog("      2:  snoop {l,h} [trigger threshold]");
		PrintAndLog("      3:  snoop <divisor> [trigger threshold]");
		return 0;
	}

	SendCommand(&c);
	WaitForResponse(CMD_ACK,NULL);

	#define BUFF_SIZE 8000
	uint8_t data[BUFF_SIZE] = {0x00};

	GetFromBigBuf(data,BUFF_SIZE,0);
	WaitForResponseTimeout(CMD_ACK,NULL, 1500);

	SetGraphBuf(data, BUFF_SIZE);

	return 0;
}

int CmdVchDemod(const char *Cmd)
{
  // Is this the entire sync pattern, or does this also include some
  // data bits that happen to be the same everywhere? That would be
  // lovely to know.
  static const int SyncPattern[] = {
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };

  // So first, we correlate for the sync pattern, and mark that.
  int bestCorrel = 0, bestPos = 0;
  int i;
  // It does us no good to find the sync pattern, with fewer than
  // 2048 samples after it...
  for (i = 0; i < (GraphTraceLen-2048); i++) {
    int sum = 0;
    int j;
    for (j = 0; j < arraylen(SyncPattern); j++) {
      sum += GraphBuffer[i+j]*SyncPattern[j];
    }
    if (sum > bestCorrel) {
      bestCorrel = sum;
      bestPos = i;
    }
  }
  PrintAndLog("best sync at %d [metric %d]", bestPos, bestCorrel);

  char bits[257];
  bits[256] = '\0';

  int worst = INT_MAX;
  int worstPos = 0;

  for (i = 0; i < 2048; i += 8) {
    int sum = 0;
    int j;
    for (j = 0; j < 8; j++) {
      sum += GraphBuffer[bestPos+i+j];
    }
    if (sum < 0) {
      bits[i/8] = '.';
    } else {
      bits[i/8] = '1';
    }
    if(abs(sum) < worst) {
      worst = abs(sum);
      worstPos = i;
    }
  }
  PrintAndLog("bits:");
  PrintAndLog("%s", bits);
  PrintAndLog("worst metric: %d at pos %d", worst, worstPos);

  if (strcmp(Cmd, "clone")==0) {
    GraphTraceLen = 0;
    char *s;
    for(s = bits; *s; s++) {
      int j;
      for(j = 0; j < 16; j++) {
        GraphBuffer[GraphTraceLen++] = (*s == '1') ? 1 : 0;
      }
    }
    RepaintGraphWindow();
  }
  return 0;
}

//by marshmellow
int CmdLFfind(const char *Cmd)
{
	int ans = 0;
	char cmdp = param_getchar(Cmd, 0);
	
	if (strlen(Cmd) > 1 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  lf search <0|1>");
		PrintAndLog("     <use data from Graphbuffer>, if not set, try reading data from tag.");
		PrintAndLog("");
		PrintAndLog("    sample: lf search");
		PrintAndLog("          : lf search 1");
		return 0;
	}

	if (!offline || (cmdp != '1') ){
		ans = CmdLFRead("");
	} else if (GraphTraceLen < 1000) {
		PrintAndLog("Data in Graphbuffer was too small.");
		return 0;
	}

	PrintAndLog("Checking for known tags:");

	ans = Cmdaskmandemod("");
	PrintAndLog("ASK_MAN: %s", (ans) ? "YES":"NO" );

	ans = CmdFSKdemodHID("");
	PrintAndLog("HID: %s", (ans) ? "YES":"NO" );

	ans = CmdFSKdemodIO("");
	PrintAndLog("IO prox: %s", (ans) ? "YES":"NO" );

	ans = CmdIndalaDemod("");
	PrintAndLog("Indala (64): %s", (ans) ? "YES":"NO" );

	ans = CmdIndalaDemod("224");
	PrintAndLog("Indala (224): %s", (ans) ? "YES":"NO" );

	// ans = CmdVchDemod("");
	// PrintAndLog("VeriChip: %s", (ans) ? "YES":"NO" );

	// ans = CmdFlexdemod("");
	// PrintAndLog("FlexPass: %s", (ans) ? "YES":"NO" );
	
	if (!ans)
		PrintAndLog("No Known Tags Found!\n");

	return 0;
}

static command_t CommandTable[] = 
{
  {"help",        CmdHelp,            1, "This help"},
  {"cmdread",     CmdLFCommandRead,   0, "<off period> <'0' period> <'1' period> <command> ['h'] -- Modulate LF reader field to send command before read (all periods in microseconds) (option 'h' for 134)"},

  {"flexdemod",   CmdFlexdemod,       1, "Demodulate samples for FlexPass"},
  {"indalademod", CmdIndalaDemod,     1, "['224'] -- Demodulate samples for Indala 64 bit UID (option '224' for 224 bit)"},
  {"indalaclone", CmdIndalaClone,     0, "<UID> ['l']-- Clone Indala to T55x7 (UID in HEX)(option 'l' for 224 UID"},
  {"vchdemod",    CmdVchDemod,        1, "['clone'] -- Demodulate samples for VeriChip"},

  
  {"read",        CmdLFRead,          0, "['h' or <divisor>] -- Read 125/134 kHz LF ID-only tag (option 'h' for 134, alternatively: f=12MHz/(divisor+1))"},
  {"search",      CmdLFfind,          1, "Read and Search for valid known tag (in offline mode it you can load first then search)"},
  {"sim",         CmdLFSim,           0, "[GAP] -- Simulate LF tag from buffer with optional GAP (in microseconds)"},
  {"simbidir",    CmdLFSimBidir,      0, "Simulate LF tag (with bidirectional data transmission between reader and tag)"},
  {"simman",      CmdLFSimManchester, 0, "<Clock> <Bitstream> [GAP] Simulate arbitrary Manchester LF tag"},
  {"snoop",       CmdLFSnoop,         0, "['l'|'h'|<divisor>] [trigger threshold]-- Snoop LF (l:125khz, h:134khz)"},
  
  {"awid26",        CmdLFAWID26,        1, "{ AWID26 tags }"},
  {"em4x",        CmdLFEM4X,          1, "{ EM4X tags }"},      
  {"hid",         CmdLFHID,           1, "{ HID tags }"},
  {"hitag",       CmdLFHitag,         1, "{ Hitag tags and transponders }"},
  {"io",     	  CmdLFIO,	          1, "{ ioProx tags }"},
  {"pcf7931",     CmdLFPCF7931,       1, "{ PCF7931 tags }"},
  {"ti",          CmdLFTI,            1, "{ TI tags }"},
  {"t55xx",       CmdLFT55XX,         1, "{ T55xx tags }"},
 
  {NULL, NULL, 0, NULL}
};

int CmdLF(const char *Cmd)
{
  CmdsParse(CommandTable, Cmd);
  return 0; 
}

int CmdHelp(const char *Cmd)
{
  CmdsHelp(CommandTable);
  return 0;
}
