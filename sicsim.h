// sicsim.h

#ifndef SICSIM_H
#define SICSIM_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// --- 상수 및 매크로 (Constants and Macros) ---
#define TRUE 1
#define FALSE 0
#define MAX_BP 100           // 최대 브레이크포인트 개수
#define MEMORY_SIZE 0x100000 // 가상 메모리 크기 (1MB)

// --- 구조체 정의 (Struct Definitions) ---
struct OPTAB { char name[10]; int code; int format; };
struct SYMTAB { char symtab_label[10]; int loc; };
struct DEFTAB { char name[10]; int addr; };
struct REFTAB { char name[10]; };
struct MOD_RECORD { int location; int length; char symbol_name[10]; };
struct ESTAB { char name[10]; int addr; int length; int control_section; };


// --- 전역 변수 선언 (Global Variables Declarations) ---
// main.c에 정의된 변수들에 대한 외부 참조 선언입니다.
extern struct OPTAB optab[100];
extern struct SYMTAB symtab[200];
extern struct DEFTAB deftab[50];
extern struct REFTAB reftab[50];
extern struct MOD_RECORD mod_records[200];

extern int optab_size;
extern int symtab_size;
extern int deftab_size;
extern int reftab_size;
extern int mod_record_count;

extern int startAdress;
extern int programLength;
extern char opcode[10], label[10], operand[10];
extern int LOCCTR;
extern unsigned int objectcode;
extern unsigned int operandAddress;
extern int symtab_index;
extern int opcode_index;
extern char objectcode_text[100];
extern char text_record[100];
extern int TS;
extern int is_format_4;
extern int BASE_REG_LOC;
extern int is_base_relative_active;

extern int PROGADDR;
extern unsigned char virtual_memory[MEMORY_SIZE];
extern int estab_size;
extern struct ESTAB estab[200];
extern int TOTAL_PROG_LENGTH;
extern int current_dump_address;
extern int ENTRY_POINT;

extern long long reg_A, reg_X, reg_L, reg_B, reg_S, reg_T; 
extern int reg_SW;
extern long long reg_PC;
extern double reg_F;

extern int breakpoints[MAX_BP];
extern int bp_count;


// --- 함수 프로토타입 (Function Prototypes) ---

// [Utility & Memory]
void rtrim(char* str);
int hex_str_to_int(char* hex_str, int length);
unsigned char get_byte(long long addr);
int get_word(long long addr);
void set_word(long long addr, int value);

// [Assembler]
void reset_assemble_global(void);
int get_register_number(char reg_char);
void initialize_optab(const char* filename);
int searchop(char *cp);
int searchsymbol(char *cp);
int search_reftab(char *cp);
int is_directive(char* s);
void getCode(char* buf, char* out_label, char* out_opcode, char* out_operand);
void plus_locctr();
unsigned int get_byte_code(char* cp);
void write_T_record(FILE *obj_fp);
void pass_one(FILE *fp);
void pass_two(FILE *fp, FILE *obj_fp, FILE *lst_fp);
void command_assemble();

// [Loader]
int search_estab(char* name, int* addr_ptr);
void loader_pass1(char* filenames[], int file_count);
void loader_pass2(char* filenames[], int file_count);
void print_load_map(void);
void command_load(char* args);

// [Simulator & Debugger Commands]
void command_dump(char* args);
void command_bp(char* args);
void reset_simulator_regs();
void print_regs();
void command_run();
void display_help();
void command_progaddr(char* args);

// [Main]
int main_loop();
int main();

#endif // SICSIM_H