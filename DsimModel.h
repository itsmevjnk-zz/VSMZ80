#pragma once
#include "StdAfx.h"
#include "sdk/vsm.hpp"

#define InfoLog(__s__) sprintf_s(LogLineT, "%05d: ", LogLine++); myPopup->print(LogLineT); myPopup->print(__s__); myPopup->print("\n")

//#define DEBUGCALLS

#define SetHigh setstate(time, 1, SHI)
#define SetLow setstate(time, 1, SLO)
#define SetFloat setstate(time, 1, FLT)

#define FLG_C		0
#define FLG_N		1
#define FLG_PV		2
#define FLG_F3		3
#define FLG_H		4
#define FLG_F5		5
#define FLG_Z		6
#define FLG_S		7

enum CYCLES {
	FETCH = 0,
	READ = 1,
	WRITE = 2,
	IOREAD = 3,
	IOWRITE = 4,
	EXEC = 5
};

enum STATES {
	T1p = 0,
	T1n = 1,
	T2p = 2,
	T2n = 3,
	T3p = 4,
	T3n = 5,
	T4p = 6,
	T4n = 7
};

class DsimModel : public IDSIMMODEL
{
public:
	INT isdigital (CHAR *pinname);
	VOID setup (IINSTANCE *inst, IDSIMCKT *dsim);
	VOID irqfire(ABSTIME time, DSIMMODES mode);
	VOID nmifire(ABSTIME time, DSIMMODES mode);
	VOID rsthandler(ABSTIME ime, DSIMMODES mode);
	VOID runctrl (RUNMODES mode);
	VOID actuate (REALTIME time, ACTIVESTATE newstate);
	BOOL indicate (REALTIME time, ACTIVEDATA *data);
	VOID clockstep(ABSTIME time, DSIMMODES mode);
	VOID simulate(ABSTIME time, DSIMMODES mode);
	VOID callback (ABSTIME time, EVENTID eventid);
private:
	VOID SetAddr(UINT16 val, ABSTIME time);
	VOID SetData(UINT8 val, ABSTIME time);
	UINT8 GetData(void);
	void HIZAddr(ABSTIME time);
	void HIZData(ABSTIME time);
	void ResetCPU(ABSTIME time);
	int chk_nz(void);
	int chk_z(void);
	int chk_nc(void);
	int chk_c(void);
	int chk_po(void);
	int chk_pe(void);
	int chk_p(void);
	int chk_m(void);
	int cc(int n);
	void opflags(int n, int is16, int c, int p, int v, int z, int s, int f35);
	void alu(int n, UINT8 r);
	void rot(int n, UINT8 * r);
	void bit(int b, UINT8 * r);
	void add16(UINT16 * a, UINT16 * b, int c);
	void Execute(void);

	IINSTANCE *inst;
	IDSIMCKT *ckt;
	IDSIMPIN *pin_M1;
	IDSIMPIN *pin_MREQ, *pin_IORQ;
	IDSIMPIN *pin_RD, *pin_WR;
	IDSIMPIN *pin_RFSH;
	IDSIMPIN *pin_HALT;
	IDSIMPIN *pin_WAIT;
	IDSIMPIN *pin_INT, *pin_NMI;
	IDSIMPIN *pin_RESET;
	IDSIMPIN *pin_BUSRQ, *pin_BUSAK;
	IDSIMPIN *pin_CLK;
	IDSIMPIN *pin_A[16];
	IDSIMPIN *pin_D[8];

	IDEBUGPOPUP *myPopup;

	// Global variables
	UINT8 cycle = 0;		// Current cycle of the state machine
	UINT8 nextcycle = 0;	// Next cycle of the state machine
	UINT8 state = 0;		// Current t-state
	UINT8 step = 0;			// Instruction execution step (0 = nothing to do)
	UINT8 IsHalted = 0;		// Indicates if the processor is halted
	UINT8 IsWaiting = 0;	// Indicates if the processor is waiting
	UINT8 IsBusRQ = 0;		// Indicates if the processor is on bus request
	UINT8 IsInt = 0;		// Indicates if the processor is interrupted
	UINT8 IsNMI = 0;		// Indicates if the processor is on non-maskable interrupt

	int LogLine = 1;
	char LogLineT[10];
	char LogMessage[256];

	// Processor registers
#define REGSIZE 30
	typedef struct {
		union {
			struct {
				UINT8 ARRAY[REGSIZE];
			};
			struct {
				UINT16 PC, IR, WZ, SP, IY, IX, HL, HL_, DE, DE_, BC, BC_, AF, AF_, IFF;
			};
			struct { // 0    1  2  3  4  5    6    7    8    9   10   11 12 13  14  15 16 17  18  19 20 21  22  23 24 25  26  27    28    29
				UINT8 PCl, PCh, R, I, Z, W, SPl, SPh, IYl, IYh, IXl, IXh, L, H, L_, H_, E, D, E_, D_, C, B, C_, B_, F, A, F_, A_, IFF1, IFF2;
			};
		};
	} tZ80REG;

	// Processor related variables
	UINT8 InstR = 0;		// Instruction Register
	tZ80REG reg;			// Registers
	UINT16 Addr;			// Memory address to read/write
	UINT8 Data;				// Data to/from the data bus
};
