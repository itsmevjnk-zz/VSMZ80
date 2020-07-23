#include "StdAfx.h"
#include "DsimModel.h"

int hold_state = 0; // set to 1 to hold execution state, used for unnecessarily time consuming instructions

int instr_x, instr_y, instr_z, instr_p, instr_q;
int instr_pre = 0; // instruction prefix (REMEMBER TO RESET TO 0 AFTER EXECUTING ANY PREFIXED INSTRUCTIONS!)

volatile unsigned long long int z80_clk = 0; // cycle counter
int z80_up = 0; // set to 1 after reset, activates Z80 ops

void DsimModel::HIZAddr(ABSTIME time) {						// Sets the address bus to HIZ
	int i;

	for (i = 0; i < 16; i++) {
		pin_A[i]->SetFloat;
	}
}

void DsimModel::HIZData(ABSTIME time) {						// Sets the data bus to HIZ
	int i;

	for (i = 0; i < 8; i++) {
		pin_D[i]->SetFloat;
	}
}

void DsimModel::SetAddr(UINT16 val, ABSTIME time) {			// Sets an address onto the address bus
	int i, j;

	for (i = 0; i < 16; i++) {
		j = (val >> i) & 0x01;
		if (j) {
			pin_A[i]->SetHigh;
		} else {
			pin_A[i]->SetLow;
		}
	}
}

void DsimModel::SetData(UINT8 val, ABSTIME time) {			// Sets a value onto the data bus
	int i, j;

	for (i = 0; i < 8; i++) {
		j = (val >> i) & 0x01;
		if (j) {
			pin_D[i]->SetHigh;
		} else {
			pin_D[i]->SetLow;
		}
	}
}

UINT8 DsimModel::GetData(void) {							// Reads a value from the data bus
	int i;
	UINT8 val = 0;

	for (i = 0; i < 8; i++) {
		if (ishigh(pin_D[i]->istate()))
			val |= (1 << i);
	}
	return(val);
}

void DsimModel::ResetCPU(ABSTIME time) {					// Rests the CPU
	int i;

#ifdef DEBUGCALLS
	InfoLog("Resetting CPU...");
#endif

	// zeroes all the flags
	cycle = 0;
	nextcycle = 0;
	state = 0;
	step = 0;
	IsHalted = 0;
	IsWaiting = 0;
	IsBusRQ = 0;
	IsInt = 0;
	IsNMI = 0;

	// zeroes all the registers
	for (i = 0; i < REGSIZE; i++)
		reg.ARRAY[i] = 0;

	// sets all output pins to high
	pin_M1->SetHigh;
	pin_MREQ->SetHigh;
	pin_IORQ->SetHigh;
	pin_RD->SetHigh;
	pin_WR->SetHigh;
	pin_RFSH->SetHigh;
	pin_HALT->SetHigh;
	pin_BUSAK->SetHigh;

	HIZAddr(time);
	HIZData(time);
}

/* condition checks */
int DsimModel::chk_nz(void) {
	return ((reg.F & (1 << FLG_Z)) ? 0 : 1);
}
int DsimModel::chk_z(void) {
	return ((reg.F & (1 << FLG_Z)) ? 1 : 0);
}
int DsimModel::chk_nc(void) {
	return ((reg.F & (1 << FLG_C)) ? 0 : 1);
}
int DsimModel::chk_c(void) {
	return ((reg.F & (1 << FLG_C)) ? 1 : 0);
}
int DsimModel::chk_po(void) {
	return ((reg.F & (1 << FLG_PV)) ? 0 : 1);
}
int DsimModel::chk_pe(void) {
	return ((reg.F & (1 << FLG_PV)) ? 1 : 0);
}
int DsimModel::chk_p(void) {
	return ((reg.F & (1 << FLG_S)) ? 0 : 1);
}
int DsimModel::chk_m(void) {
	return ((reg.F & (1 << FLG_S)) ? 0 : 1);
}
int DsimModel::cc(int n) {
	switch (n) {
	case 0: return chk_nz();
	case 1: return chk_z();
	case 2: return chk_nc();
	case 3: return chk_c();
	case 4: return chk_po();
	case 5: return chk_pe();
	case 6: return chk_p();
	case 7: return chk_m();
	default: return 0;
	}
}

void DsimModel::opflags(int n, int is16, int c, int p, int v, int z, int s, int f35) {
	if (c) {
		if (is16 && (UINT32) n > 65535) reg.F |= (1 << FLG_C);
		else if (!is16 && (UINT32) n > 255)  reg.F |= (1 << FLG_C);
		else reg.F &= ~(1 << FLG_C);
	}
	if (p) {
		int b = 0;
		for (int i = ((is16) ? 15 : 7); i >= 0; i--) {
			if (n & (1 << i)) b ^= 1;
		}
		if(b) reg.F |= (1 << FLG_PV);
		else reg.F &= ~(1 << FLG_PV);
	}
	if (v) {
		int n2 = n & ((is16) ? 0xFFFF : 0xFF);
		if (is16 && n2 > 65535) reg.F |= (1 << FLG_PV);
		else if (!is16 && n2 > 255)  reg.F |= (1 << FLG_PV);
		else reg.F &= ~(1 << FLG_PV);
	}
	if (z) {
		if (!n) reg.F |= (1 << FLG_Z);
		else reg.F &= ~(1 << FLG_Z);
	}
	if (s) {
		int n2;
		if (is16) n2 = (INT16)n;
		else n2 = (INT8)n;
		if (n2 < 0) reg.F |= (1 << FLG_S);
		else reg.F &= ~(1 << FLG_S);
	}
	if (f35) {
		reg.F &= ~((1 << FLG_F3) | (1 << FLG_F5));
		reg.F |= n & ((1 << FLG_F3) | (1 << FLG_F5));
	}
}

/* ALU operations */
void DsimModel::alu(int n, UINT8 r) {
	int t;
	switch (n) {
	case 0: // ADD A,
		t = reg.A + r;
		opflags(t, 0, 1, 0, 1, 1, 1, 1);
		reg.F &= ~(1 << FLG_N);
		break;
	case 1: // ADC A,
		t = reg.A + r + chk_c();
		opflags(t, 0, 1, 0, 1, 1, 1, 1);
		reg.F &= ~(1 << FLG_N);
		break;
	case 2: // SUB
		t = reg.A - r;
		opflags(t, 0, 1, 0, 1, 1, 1, 1);
		reg.F |= (1 << FLG_N);
		break;
	case 3: // SBC A,
		t = reg.A - r - chk_c();
		opflags(t, 0, 1, 0, 1, 1, 1, 1);
		reg.F |= (1 << FLG_N);
		break;
	case 4: // AND
		t = reg.A & r;
		opflags(t, 0, 0, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_C));
		reg.F |= (1 << FLG_H);
		break;
	case 5: // XOR
		t = reg.A ^ r;
		opflags(t, 0, 0, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_C));
		reg.F &= ~(1 << FLG_H);
		break;
	case 6: // OR
		t = reg.A | r;
		opflags(t, 0, 0, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_C));
		reg.F &= ~(1 << FLG_H);
		break;
	case 7: // CP
		t = reg.A - r;
		opflags(t, 0, 1, 0, 1, 1, 1, 0);
		reg.F |= (1 << FLG_N);
		reg.F &= ~((1 << FLG_F3) | (1 << FLG_F5));
		reg.F |= r & ((1 << 3) | (1 << 5));
		break;
	default: break;
	}
	if (n != 7) reg.A = (UINT8) t;
}

/* rotation/shift operations */
void DsimModel::rot(int n, UINT8 *r) {
	int t;
	switch (n) {
	case 0: // RLC
		t = *r << 1;
		t |= (t >> 8) & 1;
		if(r == &reg.A) opflags(t, 0, 1, 0, 0, 0, 0, 1);
		else opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	case 1: // RRC
		t = *r >> 1;
		if (*r & 1) t |= (1 << 7) | (1 << 8);
		else t &= ~((1 << 7) | (1 << 8));
		if (r == &reg.A) opflags(t, 0, 1, 0, 0, 0, 0, 1);
		else opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	case 2: // RL
		t = *r << 1;
		t |= reg.F & (1 << FLG_C);
		if (r == &reg.A) opflags(t, 0, 1, 0, 0, 0, 0, 1);
		else opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	case 3: // RR
		t = *r >> 1;
		t |= (reg.F & (1 << FLG_C)) << 7;
		if (*r & 1) t |= (1 << 8);
		else t &= ~(1 << 8);
		if (r == &reg.A) opflags(t, 0, 1, 0, 0, 0, 0, 1);
		else opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	case 4: // SLA
		t = *r << 1;
		opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	case 5: // SRA
		t = *r >> 1;
		t |= ((*r & 1) << 8) | (*r & (1 << 7));
		opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	case 6: // SLL
		t = *r << 1;
		t |= 1;
		opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	case 7: // SRL
		t = *r >> 1;
		t |= ((*r & 1) << 8);
		opflags(t, 0, 1, 1, 0, 1, 1, 1);
		reg.F &= ~((1 << FLG_N) | (1 << FLG_H));
		break;
	}
	*r = (UINT8)t;
}

/* test bit */
void DsimModel::bit(int b, UINT8 *r) {
	opflags(*r, 0, 0, 0, 0, 0, 0, 1);
	if (*r & (1 << b)) reg.F &= ~((1 << FLG_Z) | (1 << FLG_PV));
	else reg.F |= ((1 << FLG_Z) | (1 << FLG_PV));
	if (b == 7 && (*r & (1 << b))) reg.F |= (1 << FLG_S);
}

/* 16bit addition routine */
void DsimModel::add16(UINT16 *a, UINT16 *b, int c) {
	int t = *a + *b;
	if (c) {
		t += chk_c();
		opflags(t, 1, 1, 0, 0, 1, 1, 0);
	} else opflags(t, 1, 1, 0, 0, 0, 0, 0);
	reg.F &= ~((1 << FLG_F3) | (1 << FLG_F5) | (1 << FLG_N));
	reg.F |= (t & ((1 << 11) | (1 << 13))) >> 8;
	*a = (UINT16)t;
}

int done = 0;// Indicates done executing

void DsimModel::Execute(void) {								// Executes an instruction
	/* algorithmic instruction decode mechanism */
	UINT8 *tab_r[8] = { &reg.B, &reg.C, &reg.D, &reg.E, &reg.H, &reg.L, NULL /* (HL) */, &reg.A };
	UINT16 *tab_rp[4] = { &reg.BC, &reg.DE, &reg.HL, &reg.SP };
	UINT16 *tab_rp2[4] = { &reg.BC, &reg.DE, &reg.HL, &reg.AF };
#ifdef DEBUGCALLS
	char *disp_r[8] = { "B", "C", "D", "E", "H", "L", "(HL)", "A" };
	char *disp_rp[4] = { "BC", "DE", "HL", "SP" };
	char *disp_rp2[4] = { "BC", "DE", "HL", "AF" };
	sprintf_s(LogMessage, "    Executing 0x%02x step %d...", InstR, step);
	InfoLog(LogMessage);
#endif
	int t = 0; // general purpose temp variable
	switch (instr_pre) {
	case 0xCB: // CB prefixed
		switch (instr_x) {
		case 0: // rot[y] r[z]
			if (instr_z == 6) { // (HL)
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.HL;
					break;
				case 2:
					cycle = EXEC;
					hold_state = 1;
					rot(instr_y, &Data);
					break;
				case 4:
					cycle = WRITE;
					hold_state = 0;
					Addr = reg.HL;
					done++;
					instr_pre = 0;
					break;
				}
			}
			else {
				rot(instr_y, tab_r[instr_z]);
				done++;
				instr_pre = 0;
			}
			break;
		case 1: // BIT y, r[z]
			if (instr_z == 6) {
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.HL;
					break;
				case 2:
					bit(instr_y, &Data);
					done++;
					instr_pre = 0;
					break;
				}
			}
			else {
				bit(instr_y, tab_r[instr_z]);
				done++;
				instr_pre = 0;
			}
			break;
		case 2: // RES y, r[z]
			if (instr_z == 6) {
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.HL;
					break;
				case 2:
					cycle = EXEC;
					hold_state = 1;
					Data &= ~(1 << instr_y);
					break;
				case 4:
					cycle = WRITE;
					hold_state = 0;
					Addr = reg.HL;
					done++;
					instr_pre = 0;
					break;
				}
			}
			else {
				*tab_r[instr_z] &= ~(1 << instr_y);
				done++;
				instr_pre = 0;
			}
			break;
		case 3: // SET y, r[z]
			if (instr_z == 6) {
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.HL;
					break;
				case 2:
					cycle = EXEC;
					hold_state = 1;
					Data |= (1 << instr_y);
					break;
				case 4:
					cycle = WRITE;
					hold_state = 0;
					Addr = reg.HL;
					done++;
					instr_pre = 0;
					break;
				}
			}
			else {
				*tab_r[instr_z] |= (1 << instr_y);
				done++;
				instr_pre = 0;
			}
			break;
		}
		break;
	default: // unprefixed
		switch (instr_x) {
		case 0:
			switch (instr_z) {
			case 0:
				switch (instr_y) {
				case 0: // NOP
					done++;
					break;
				case 1: // EX AF, AF'
					t = reg.AF;
					reg.AF = reg.AF_;
					reg.AF_ = t;
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        AF=0x%04x AF'=0x%04x", reg.AF, reg.AF_);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				case 2: // DJNZ *
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						reg.L = Data;
						cycle = EXEC;
						hold_state = 1;
						reg.B--;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        B=0x%02x", reg.B);
						InfoLog(LogMessage);
#endif
						break;
					case 4:
						if (!reg.B) {
							hold_state = 0;
							done++;
						}
						break;
					case 5:
						reg.PC += (INT8)reg.L - 2;
						break;
					case 14:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        PC=0x%04x", reg.PC);
						InfoLog(LogMessage);
#endif
						hold_state = 0;
						done++;
						break;
					default:
						break;
					}
					break;
				case 3: // JR *
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						reg.PC += (INT8)Data - 2;
						cycle = EXEC;
						hold_state = 1;
						break;
					case 12:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        PC=0x%04x", reg.PC);
						InfoLog(LogMessage);
#endif
						hold_state = 0;
						break;
					}
					break;
				default: // JR cond, *
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						reg.L = Data;
						if (!cc(instr_y - 4)) done++;
						else {
							cycle = EXEC;
							hold_state = 1;
						}
						break;
					case 12:
						reg.PC += (INT8)reg.L - 2;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        PC=0x%04x", reg.PC);
						InfoLog(LogMessage);
#endif
						hold_state = 0;
						break;
					}
					break;
				}
				break;
			case 1:
				switch (instr_q) {
				case 0: // LD rp[p], **
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						reg.Z = Data;
						Addr = reg.PC++;
						break;
					case 3:
						reg.W = Data;
						*tab_rp[instr_p] = reg.WZ;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        %s=0x%04x", disp_rp[instr_p], *tab_rp[instr_p]);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
					break;
				case 1: // ADD HL, rp[p]
					switch (step++) {
					case 1:
						add16(&reg.HL, tab_rp[instr_p], 0);
						hold_state = 1;
						break;
					case 15:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        HL=0x%04x", reg.HL);
						InfoLog(LogMessage);
#endif
						hold_state = 0;
						done++;
						break;
					default:
						break;
					}
					break;
				}
				break;
			case 2:
				switch (instr_q) {
				case 0:
					switch (instr_p) {
					case 0: // LD (BC), A
						switch (step++) {
						case 1:
							cycle = WRITE;
							Addr = reg.BC;
							Data = reg.A;
							break;
						case 2:
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        (BC)=0x%02x", reg.A);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					case 1: // LD (DE), A
						switch (step++) {
						case 1:
							cycle = WRITE;
							Addr = reg.DE;
							Data = reg.A;
							break;
						case 2:
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        (DE)=0x%02x", reg.A);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					case 2: // LD (**), HL
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.PC++;
							break;
						case 2:
							reg.Z = Data;
							Addr = reg.PC++;
							break;
						case 3:
							reg.W = Data;
							cycle = WRITE;
							Addr = reg.WZ;
							Data = reg.L;
							break;
						case 4:
							Addr = reg.WZ + 1;
							Data = reg.H;
							break;
						case 5:
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        0x%04x=0x%04x", reg.WZ, reg.HL);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					case 3: // LD (**), A
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.PC++;
							break;
						case 2:
							reg.Z = Data;
							Addr = reg.PC++;
							break;
						case 3:
							reg.W = Data;
							cycle = WRITE;
							Addr = reg.WZ;
							Data = reg.A;
							break;
						case 4:
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        0x%04x=0x%02x", reg.WZ, reg.A);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					}
					break;
				case 1:
					switch (instr_p) {
					case 0: // LD A, (BC)
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.BC;
							break;
						case 2:
							reg.A = Data;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        A=0x%02x", reg.A);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					case 1: // LD A, (DE)
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.DE;
							break;
						case 2:
							reg.A = Data;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        A=0x%02x", reg.A);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					case 2: // LD HL, (**)
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.PC++;
							break;
						case 2:
							reg.Z = Data;
							Addr = reg.PC++;
							break;
						case 3:
							reg.W = Data;
							Addr = reg.WZ;
							break;
						case 4:
							reg.L = Data;
							Addr = reg.WZ + 1;
							break;
						case 5:
							reg.H = Data;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        HL=0x%04x", reg.HL);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					case 3: // LD A, (**)
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.PC++;
							break;
						case 2:
							reg.Z = Data;
							Addr = reg.PC++;
							break;
						case 3:
							reg.W = Data;
							Addr = reg.WZ;
							break;
						case 4:
							reg.A = Data;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        A=0x%02x", reg.A);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					}
					break;
				}
				break;
			case 3:
				switch (instr_q) {
				case 0: // INC rp[p]
					switch (step++) {
					case 1:
						*tab_rp[instr_p]++;
						reg.F &= ~(1 << FLG_N);
						opflags((int)*tab_rp[instr_p], 1, 0, 0, 1, 1, 1, 1);
						hold_state = 1;
						break;
					case 5:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        %s=0x%04x", disp_rp[instr_p], *tab_rp[instr_p]);
						InfoLog(LogMessage);
#endif
						done++;
						hold_state = 0;
						break;
					default:
						break;
					}
					break;
				case 1: // DEC rp[p]
					switch (step++) {
					case 1:
						*tab_rp[instr_p]--;
						reg.F |= (1 << FLG_N);
						opflags((int)*tab_rp[instr_p], 1, 0, 0, 1, 1, 1, 1);
						hold_state = 1;
						break;
					case 5:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        %s=0x%04x", disp_rp[instr_p], *tab_rp[instr_p]);
						InfoLog(LogMessage);
#endif
						done++;
						hold_state = 0;
						break;
					default:
						break;
					}
					break;
				}
				break;
			case 4: // INC r[y]
				*tab_r[instr_y]++;
				reg.F &= ~(1 << FLG_N);
				opflags((int)*tab_r[instr_y], 0, 0, 0, 1, 1, 1, 1);
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "        %s=0x%02x", disp_r[instr_y], *tab_r[instr_y]);
				InfoLog(LogMessage);
#endif
				done++;
				break;
			case 5: // DEC r[y]
				*tab_r[instr_y]--;
				reg.F |= (1 << FLG_N);
				opflags((int)*tab_r[instr_y], 0, 0, 0, 1, 1, 1, 1);
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "        %s=0x%02x", disp_r[instr_y], *tab_r[instr_y]);
				InfoLog(LogMessage);
#endif
				done++;
				break;
			case 6: // LD r[y], *
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.PC++;
					break;
				case 2:
					*tab_r[instr_y] = Data;
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        %s=0x%02x", disp_r[instr_y], *tab_r[instr_y]);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				}
				break;
			case 7:
				switch (instr_y) {
				case 4: // DAA
					if ((reg.A & 15) > 9 || (reg.F | (1 << FLG_H))) reg.A += 6;
					if (((reg.A >> 4) & 15) > 9 || (reg.F | (1 << FLG_C))) reg.A += 0x60;
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        A=0x%02x", reg.A);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				case 5: // CPL
					reg.A = ~reg.A;
					reg.F |= (1 << FLG_H);
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        A=0x%02x", reg.A);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				case 6: // SCF
					reg.F |= (1 << FLG_C);
					reg.F &= ~((1 << FLG_H) | (1 << FLG_N));
					done++;
					break;
				case 7: // CCF
					if (reg.F & (1 << FLG_C)) reg.F &= ~(1 << FLG_C);
					else reg.F |= (1 << FLG_C);
					if (reg.F & (1 << FLG_H)) reg.F &= ~(1 << FLG_H);
					else reg.F |= (1 << FLG_H);
					reg.F &= ~(1 << FLG_N);
					done++;
					break;
				default: // RLCA/RRCA/RLA/RRA
					rot(instr_y, &reg.A);
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        A=0x%02x", reg.A);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				}
				break;
			}
			break;
		case 1:
			if (instr_y == 6 && instr_z == 6) { // HALT
												/* TODO: implement */
				while (1); // not the most ideal thing but will work for the time being as we don't have interrupts yet
			}
			else { // LD r[y], r[z]
				if (instr_y == 6) { // register to (HL)
					switch (step++) {
					case 1:
						cycle = WRITE;
						Addr = reg.HL;
						Data = *tab_r[instr_z];
						break;
					case 2:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        (HL)=0x%02x", *tab_r[instr_z]);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
				}
				else if (instr_z == 6) { // (HL) to register
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.HL;
						break;
					case 2:
						*tab_r[instr_y] = Data;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        %s=0x%02x", disp_r[instr_y], *tab_r[instr_y]);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
				}
				else { // register to register
					*tab_r[instr_y] = *tab_r[instr_z];
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        %s=0x%02x", disp_r[instr_y], *tab_r[instr_y]);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				}
			}
			break;
		case 2: // alu[y] r[z]
			if (instr_z == 6) { // ALU operation with (HL)
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.HL;
					break;
				case 2:
					alu(instr_y, Data);
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        A=0x%02x", reg.A);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				}
			}
			else {
				alu(instr_y, *tab_r[instr_z]);
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "        A=0x%02x", reg.A);
				InfoLog(LogMessage);
#endif
				done++;
				break;
			}
			break;
		case 3:
			switch (instr_z) {
			case 0: // RET cc[y]
				switch (step++) {
				case 1:
					hold_state = 1;
					break;
				default:
					if (cc(instr_y)) { // this will be called on every step, but looks like there's no better way
						switch (step) {
						case 2:
							cycle = READ;
							Addr = reg.SP++;
							break;
						case 3:
							reg.Z = Data;
							Addr = reg.SP++;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        SP=0x%04x", reg.SP);
							InfoLog(LogMessage);
#endif
							break;
						case 4:
							reg.W = Data;
							reg.PC = reg.WZ;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        PC=0x%04x", reg.WZ);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
					}
					else {
						hold_state = 0;
						done++;
						break;
					}
					break;
				}
				break;
			case 1:
				switch (instr_q) {
				case 0: // POP rp2[p]
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.SP++;
						break;
					case 2:
						reg.Z = Data;
						Addr = reg.SP++;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        SP=0x%04x", reg.SP);
						InfoLog(LogMessage);
#endif
						break;
					case 3:
						reg.W = Data;
						*tab_rp2[instr_p] = reg.WZ;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        %s=0x%04x", disp_rp2[instr_p], *tab_rp2[instr_p]);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
					break;
				case 1:
					switch (instr_p) {
					case 0: // RET
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.SP++;
							break;
						case 2:
							reg.Z = Data;
							Addr = reg.SP++;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        SP=0x%04x", reg.SP);
							InfoLog(LogMessage);
#endif
							break;
						case 3:
							reg.W = Data;
							reg.PC = reg.WZ;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        PC=0x%04x", reg.WZ);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					case 1: // EXX
						t = reg.BC;
						reg.BC = reg.BC_;
						reg.BC_ = t;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        BC=0x%04x BC'=0x%04x", reg.BC, reg.BC_);
						InfoLog(LogMessage);
#endif
						t = reg.DE;
						reg.DE = reg.DE_;
						reg.DE_ = t;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        DE=0x%04x DE'=0x%04x", reg.DE, reg.DE_);
						InfoLog(LogMessage);
#endif
						t = reg.HL;
						reg.HL = reg.HL_;
						reg.HL_ = t;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        HL=0x%04x HL'=0x%04x", reg.HL, reg.HL_);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					case 2: // JP (HL)
						reg.PC = reg.HL;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        PC=0x%04x", reg.PC);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					case 3: // LD SP, HL
						switch (step++) {
						case 1:
							reg.SP = reg.HL;
							hold_state = 1;
							break;
						case 5:
							hold_state = 0;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        SP=0x%04x", reg.SP);
							InfoLog(LogMessage);
#endif
							done++;
							break;
						}
						break;
					}
					break;
				case 2: // JP cc[y], **
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						reg.Z = Data;
						Addr = reg.PC++;
						break;
					case 3:
						reg.WZ = Data;
						if (cc(instr_y)) reg.PC = reg.WZ;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        PC=0x%04x", reg.PC);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
					break;
				}
			case 2: // JP cc[y], **
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.PC++;
					break;
				case 2:
					reg.Z = Data;
					Addr = reg.PC++;
					break;
				case 3:
					reg.W = Data;
					if (cc(instr_y)) {
						reg.PC = reg.WZ;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        PC=0x%04x", reg.WZ);
						InfoLog(LogMessage);
#endif
					}
					done++;
					break;
				}
				break;
			case 3:
				switch (instr_y) {
				case 0: // JP **
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						Addr = reg.PC++;
						reg.Z = Data;
						break;
					case 3:
						reg.W = Data;
						reg.PC = reg.WZ;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        PC=0x%04x", reg.WZ);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
					break;
				case 1: // CB prefix
					instr_pre = (instr_pre << 8) | 0xCB;
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        Instruction prefix = 0x%x", instr_pre);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				case 2: // OUT (*), A
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						cycle = IOWRITE;
						Addr = Data;
						Data = reg.A;
						break;
					case 3:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        I/O 0x%02x = 0x%02x", Addr, reg.A);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
					break;
				case 3: // IN (*), A
					switch (step++) {
					case 1:
						cycle = READ;
						Addr = reg.PC++;
						break;
					case 2:
						cycle = IOREAD;
						Addr = Data;
						break;
					case 3:
						Data = reg.A;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        A = 0x%02x", reg.A);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
					break;
				case 4: // EX (SP), HL
					switch (step++) {
					case 1:
						hold_state = 1;
						break;
					case 7:
						hold_state = 0;
						cycle = READ;
						Addr = reg.SP;
						break;
					case 8:
						reg.Z = reg.L;
						reg.L = Data;
						Addr = reg.SP + 1;
						break;
					case 9:
						reg.W = reg.H;
						reg.H = Data;
						cycle = WRITE;
						Addr = reg.SP;
						Data = reg.Z;
						break;
					case 10:
						Addr = reg.SP + 1;
						Data = reg.W;
						break;
					case 11:
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        (SP)=0x%04x HL=0x%04x", reg.WZ, reg.HL);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					default:
						break;
					}
					break;
				case 5: // EX DE, HL
					t = reg.DE;
					reg.DE = reg.HL;
					reg.HL = t;
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        DE=0x%04x HL=0x%04x", reg.DE, reg.HL);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				case 6: // DI
						/* TODO: implement */
					done++;
					break;
				case 7: // EI
						/* TODO: implement */
					done++;
					break;
				}
				break;
			case 4: // CALL cc[y], **
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.PC++;
					break;
				case 2:
					reg.Z = Data;
					Addr = reg.PC++;
					break;
				case 3:
					reg.W = Data;
					if (!cc(instr_y)) done++;
					else {
						cycle = EXEC;
						state = T4n;
						hold_state = 1;
					}
					break;
				case 5:
					cycle = WRITE;
					reg.SP -= 2;
					Addr = reg.SP;
					Data = reg.PCl;
					break;
				case 6:
					Addr = reg.SP + 1;
					Data = reg.PCh;
					break;
				case 7:
					reg.PC = reg.WZ;
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        PC=0x%04x", reg.WZ);
					InfoLog(LogMessage);
#endif
					done++;
					hold_state = 0;
					break;
				}
				break;
			case 5:
				switch (instr_q) {
				case 0: // PUSH rp2[p]
					switch (step++) {
					case 1:
						reg.SP -= 2;
						reg.WZ = *tab_rp2[instr_p];
						hold_state = 1;
						break;
					case 2:
						cycle = WRITE;
						Addr = reg.SP;
						Data = reg.Z;
						break;
					case 3:
						Addr = reg.SP + 1;
						Data = reg.W;
						done++;
						break;
					}
					break;
				case 1:
					switch (instr_p) {
					case 0: // CALL **
						switch (step++) {
						case 1:
							cycle = READ;
							Addr = reg.PC++;
							break;
						case 2:
							reg.Z = Data;
							Addr = reg.PC++;
							break;
						case 3:
							reg.W = Data;
							cycle = EXEC;
							state = T4n;
							hold_state = 1;
							break;
						case 5:
							cycle = WRITE;
							reg.SP -= 2;
							Addr = reg.SP;
							Data = reg.PCl;
							break;
						case 6:
							Addr = reg.SP + 1;
							Data = reg.PCh;
							break;
						case 7:
							reg.PC = reg.WZ;
#ifdef DEBUGCALLS
							sprintf_s(LogMessage, "        PC=0x%04x", reg.WZ);
							InfoLog(LogMessage);
#endif
							done++;
							hold_state = 0;
							break;
						}
						break;
					case 1: // DD prefix
						instr_pre = (instr_pre << 8) | 0xDD;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        Instruction prefix = 0x%x", instr_pre);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					case 2: // ED prefix
						instr_pre = (instr_pre << 8) | 0xED;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        Instruction prefix = 0x%x", instr_pre);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					case 3: // FD prefix
						instr_pre = (instr_pre << 8) | 0xFD;
#ifdef DEBUGCALLS
						sprintf_s(LogMessage, "        Instruction prefix = 0x%x", instr_pre);
						InfoLog(LogMessage);
#endif
						done++;
						break;
					}
					break;
				}
				break;
			case 6: // alu[y] *
				switch (step++) {
				case 1:
					cycle = READ;
					Addr = reg.PC++;
					break;
				case 2:
					alu(instr_y, Data);
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        A=0x%02x", reg.A);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				}
				break;
			case 7: // RST y*8
				switch (step++) {
				case 1:
					reg.SP -= 2;
					cycle = WRITE;
					Addr = reg.SP;
					Data = reg.PCl;
					break;
				case 2:
					Addr = reg.SP + 1;
					Data = reg.PCh;
					break;
				case 3:
					reg.PC = instr_y * 8;
#ifdef DEBUGCALLS
					sprintf_s(LogMessage, "        PC=0x%04x", reg.WZ);
					InfoLog(LogMessage);
#endif
					done++;
					break;
				}
				break;
			}
			break;
		}
		break;
	}
	if (step) {
		if (done) {
			cycle = FETCH;
			step = 0;
		}
	}
}

INT DsimModel::isdigital(CHAR *pinname) {
	return TRUE;											// Indicates all the pins are digital
}

VOID DsimModel::setup(IINSTANCE *instance, IDSIMCKT *dsimckt) {

	int n;
	char s[8];

	inst = instance;
	ckt = dsimckt;

	CREATEPOPUPSTRUCT *cps = new CREATEPOPUPSTRUCT;
	cps->caption = "Z80 Simulator Debugger Log";			// WIN Header
	cps->flags = PWF_VISIBLE | PWF_SIZEABLE;				// Show + Size
	cps->type = PWT_DEBUG;									// WIN DEBUG
	cps->height = 500;
	cps->width = 400;
	cps->id = 123;

	myPopup = (IDEBUGPOPUP *)instance->createpopup(cps);

	InfoLog("Connecting control pins...");

	pin_M1 = inst->getdsimpin("$M1$", true);				// Connects M1 cycle pin
	pin_MREQ = inst->getdsimpin("$MREQ$", true);			// Connects memory request pin
	pin_IORQ = inst->getdsimpin("$IORQ$", true);			// Connects IO request pin
	pin_RD = inst->getdsimpin("$RD$", true);				// Connects memory read pin
	pin_WR = inst->getdsimpin("$WR$", true);				// Connects memory write pin
	pin_RFSH = inst->getdsimpin("$RFSH$", true);			// Connects memory refresh pin
	pin_HALT = inst->getdsimpin("$HALT$", true);			// Connects halt pin
	pin_WAIT = inst->getdsimpin("$WAIT$", true);			// Connects memory wait pin
	pin_INT = inst->getdsimpin("$INT$", true);				// Connects interrupt request pin
	pin_NMI = inst->getdsimpin("$NMI$", true);				// Connects non-maskable interrupt pin
	pin_RESET = inst->getdsimpin("$RESET$", true);			// Connects reset pin
	pin_BUSRQ = inst->getdsimpin("$BUSRQ$", true);			// Connects bus request pin
	pin_BUSAK = inst->getdsimpin("$BUSAK$", true);			// Connects bus acknowledge pin
	pin_CLK = inst->getdsimpin("CLK", true);				// Connects Clock pin

	InfoLog("Connecting data pins...");
	for (n = 0; n < 8; n++) {								// Connects Data pins
		s[0] = 'D';
		_itoa_s(n, &s[1], 7, 10);
		pin_D[n] = inst->getdsimpin(s, true);
	}

	InfoLog("Connecting address pins...");
	for (n = 0; n < 16; n++) {								// Connects Address pins
		s[0] = 'A';
		_itoa_s(n, &s[1], 7, 10);
		pin_A[n] = inst->getdsimpin(s, true);
	}

	// Connects function to handle Clock steps (instead of using "simulate")
	pin_CLK->sethandler(this, (PINHANDLERFN)&DsimModel::clockstep);
	pin_INT->sethandler(this, (PINHANDLERFN)&DsimModel::irqfire);
	pin_NMI->sethandler(this, (PINHANDLERFN)&DsimModel::nmifire);
	pin_RESET->sethandler(this, (PINHANDLERFN)&DsimModel::rsthandler);

	InfoLog("Hold $RESET$ low for at least 3 clock cycles to activate");
	// ResetCPU(0);
}

VOID DsimModel::irqfire(ABSTIME time, DSIMMODES mode) {
	if (pin_INT->isnegedge()) {
#ifdef DEBUGCALLS
		sprintf_s(LogMessage, "$INT$ active");
		InfoLog(LogMessage);
#endif
	}
}

VOID DsimModel::nmifire(ABSTIME time, DSIMMODES mode) {
	if (pin_NMI->isnegedge()) {
#ifdef DEBUGCALLS
		sprintf_s(LogMessage, "$NMI$ active");
		InfoLog(LogMessage);
#endif
	}
}

unsigned long long int z80_rst_start = 0;
VOID DsimModel::rsthandler(ABSTIME ime, DSIMMODES mode) {
	if (pin_RESET->isnegedge()) { // RESET pin activates
		z80_rst_start = z80_clk;
		ResetCPU(0); // reset the Z80
		z80_up = 0; // block CPU from running

	}
	else if (pin_RESET->isposedge()) { // RESET end
		if (z80_clk - z80_rst_start < 3) { // not enough cycles
#ifdef DEBUGCALLS
			InfoLog("CPU reset failed");
			sprintf_s(LogMessage, "Expected at least 3 cycles, got %d cycle(s)", z80_clk - z80_rst_start);
			InfoLog(LogMessage);
#endif
		}
		else {
#ifdef DEBUGCALLS
			InfoLog("CPU reset completed");
#endif
			z80_up = 1; // lets the CPU run again
		}
	}
}

VOID DsimModel::runctrl(RUNMODES mode) {
}

VOID DsimModel::actuate(REALTIME time, ACTIVESTATE newstate) {
}

BOOL DsimModel::indicate(REALTIME time, ACTIVEDATA *data) {
	return FALSE;
}

VOID DsimModel::clockstep(ABSTIME time, DSIMMODES mode) {
	if (pin_CLK->isposedge()) z80_clk++;
	if (z80_up && pin_CLK->isedge()) {

#ifdef DEBUGCALLS
		sprintf_s(LogMessage, "Cycle %d state %d...", cycle, state);
		InfoLog(LogMessage);
#endif
		switch (cycle) {
			/*----------------------------------------------*/
		case FETCH:											// Instruction fetch cycle
			switch (state) {
			case T1p:
				done = 0;
#ifdef DEBUGCALLS
				InfoLog("  Fetch...");
				sprintf_s(LogMessage, "    Setting instruction address to 0x%04x...", reg.PC);
				InfoLog(LogMessage);
#endif
				pin_M1->SetLow;
				SetAddr(reg.PC, time);
				break;
			case T1n:
				pin_MREQ->SetLow;
				pin_RD->SetLow;
				break;
			case T2p:
				reg.PC++;
				break;
			case T2n:
				break;
			case T3p:
#ifdef DEBUGCALLS
				InfoLog("    Reading instruction...");
#endif
				InstR = GetData();
				instr_z = (InstR & 7);
				instr_y = (InstR >> 3) & 7;
				instr_x = (InstR >> 6) & 3;
				instr_q = instr_y & 1;
				instr_p = (instr_y >> 1) & 3;
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "      -> 0x%02x (x=%d,y=%d,z=%d,p=%d,q=%d...", InstR, instr_x, instr_y, instr_z, instr_p, instr_q);
				InfoLog(LogMessage);
#endif
				pin_MREQ->SetHigh;
				pin_RD->SetHigh;
				pin_M1->SetHigh;
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "    Setting refresh address to 0x%04x...", reg.IR);
				InfoLog(LogMessage);
#endif
				SetAddr(reg.IR, time + 20000);				// Puts the refresh address on the bus 20ns after RD goes up
				reg.W = reg.R++;
				reg.R = (reg.W & 0x80) | (reg.R & 0x7f);	// Increments only the 7 first bits of R (the 8th bit stays the same)
				pin_RFSH->setstate(time + 22000, 1, SLO);	// And brings RFSH low 2ns after that
				break;
			case T3n:
				pin_MREQ->SetLow;
				break;
			case T4n:
				pin_MREQ->SetHigh;
				if(!hold_state) step = 1;									// Start execution of the fetched instruction
				Execute();
				pin_RFSH->SetHigh;
				break;
			}

			if(!hold_state) state++;
			if (state > T4n)
				state = T1p;
			break;
		case EXEC: // continue execution cycle (identical to FETCH cycle at T4n)
			pin_MREQ->SetHigh;
			if (!hold_state) step = 1;									// Start execution of the fetched instruction
			Execute();
			pin_RFSH->SetHigh;
			if (!hold_state) {
				state = T1p;
				cycle = FETCH;
			}
			break;
			/*----------------------------------------------*/
		case READ:											// Memory read cycle
			switch (state) {
			case T1p:
#ifdef DEBUGCALLS
				InfoLog("  Read...");
				sprintf_s(LogMessage, "    Setting read memory address to 0x%04x...", Addr);
				InfoLog(LogMessage);
#endif
				SetAddr(Addr, time);
				break;
			case T1n:
				pin_MREQ->SetLow;
				pin_RD->SetLow;
				break;
			case T3n:
#ifdef DEBUGCALLS
				InfoLog("    Reading data...");
#endif
				Data = GetData();
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "      -> 0x%02x...", Data);
				InfoLog(LogMessage);
#endif
				pin_MREQ->SetHigh;
				pin_RD->SetHigh;
				Execute();
				break;
			}
			state++;
			if (state > T3n)
				state = T1p;
			break;
			/*----------------------------------------------*/
		case WRITE:											// Memory write cycle
			switch (state) {
			case T1p:
#ifdef DEBUGCALLS
				InfoLog("  Write...");
				sprintf_s(LogMessage, "    Setting write memory address to 0x%04x...", Addr);
				InfoLog(LogMessage);
#endif
				SetAddr(Addr, time);
				break;
			case T1n:
				pin_MREQ->SetLow;
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "    Setting data to 0x%02x...", Data);
				InfoLog(LogMessage);
#endif
				SetData(Data, time);
				break;
			case T2n:
				pin_WR->SetLow;
				break;
			case T3n:
				pin_MREQ->SetHigh;
				pin_WR->SetHigh;
				HIZData(time + 20000);						// Put the data bus in FLT 20ns after the WR pin goes up
				Execute();
				break;
			}
			state++;
			if (state > T3n)
				state = T1p;
			break;
		case IOREAD:											// I/O read cycle
			switch (state) {
			case T1p:
#ifdef DEBUGCALLS
				InfoLog("  I/O Read...");
				sprintf_s(LogMessage, "    Setting read memory address to 0x%04x...", Addr);
				InfoLog(LogMessage);
#endif
				SetAddr(Addr, time);
				break;
			case T2p:
				pin_IORQ->SetLow;
				pin_RD->SetLow;
				break;
			case T4p: // supposed to be T3 according to Z80 docs, but in this case T3 is TW
#ifdef DEBUGCALLS
				InfoLog("    Reading data...");
#endif
				Data = GetData();
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "      -> 0x%02x...", Data);
				InfoLog(LogMessage);
#endif
			case T4n:
				pin_IORQ->SetHigh;
				pin_RD->SetHigh;
				Execute();
				break;
			}
			state++;
			if (state > T4n)
				state = T1p;
			break;
			/*----------------------------------------------*/
		case IOWRITE:											// I/O write cycle
			switch (state) {
			case T1p:
#ifdef DEBUGCALLS
				InfoLog("  I/O Write...");
				sprintf_s(LogMessage, "    Setting write memory address to 0x%04x...", Addr);
				InfoLog(LogMessage);
#endif
				SetAddr(Addr, time);
				break;
			case T1n:
#ifdef DEBUGCALLS
				sprintf_s(LogMessage, "    Setting data to 0x%02x...", Data);
				InfoLog(LogMessage);
#endif
				SetData(Data, time);
				break;
			case T2p:
				pin_IORQ->SetLow;
				pin_WR->SetLow;
				break;
			case T4n:
				pin_IORQ->SetHigh;
				pin_WR->SetHigh;
				HIZData(time + 20000);						// Put the data bus in FLT 20ns after the WR pin goes up
				Execute();
				break;
			}
			state++;
			if (state > T4n)
				state = T1p;
			break;
		}
	}
}

VOID DsimModel::simulate(ABSTIME time, DSIMMODES mode) {
}

VOID DsimModel::callback(ABSTIME time, EVENTID eventid) {
}
