#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// --- Constants and Macros ---
// 기능: 공통 상수/매크로 정의
// 설명: TRUE/FALSE, 브레이크포인트 최대 개수, 메모리 크기 등 설정
#define TRUE 1
#define FALSE 0
#define MAX_BP 100
#define MEMORY_SIZE 0x100000 // 1MB

// --- Struct Definitions ---
// 기능: 어셈블러/로더/심볼 정보 구조체 정의
// 설명: OPTAB, SYMTAB, EXTDEF/EXTREF, 수정 레코드, 외부 심볼 테이블 등
struct OPTAB { char name[10]; int code; int format; };
struct SYMTAB { char symtab_label[10]; int loc; };
struct DEFTAB { char name[10]; int addr; };
struct REFTAB { char name[10]; };
struct MOD_RECORD { int location; int length; char symbol_name[10]; };
struct ESTAB { char name[10]; int addr; int length; int control_section; };

// --- Global Variables ---
// 기능: 어셈블러/로더/시뮬레이터 공용 상태 저장
// 설명: 테이블 배열, 사이즈, LOCCTR, 오브젝트코드 버퍼 등
struct OPTAB optab[100];
struct SYMTAB symtab[200];
struct DEFTAB deftab[50];
struct REFTAB reftab[50];
struct MOD_RECORD mod_records[200];

int optab_size = 0;
int symtab_size = 0;
int deftab_size = 0;
int reftab_size = 0;
int mod_record_count = 0;

int startAdress = 0;
int programLength = 0;
char opcode[10], label[10], operand[10];
int LOCCTR = 0;
unsigned int objectcode = 0x000000;
unsigned int operandAddress = 0;
int symtab_index = 0;
int opcode_index = 0;
char objectcode_text[100];
char text_record[100];
int TS = 0;
int is_format_4 = FALSE;
int BASE_REG_LOC = 0;
int is_base_relative_active = FALSE;

// [Loader & Memory]
// 기능: 로더와 시뮬레이터에서 사용할 메모리/ESTAB 상태
int PROGADDR = 0x00; 
unsigned char virtual_memory[MEMORY_SIZE];
unsigned char status_map[MEMORY_SIZE];
int estab_size = 0;
struct ESTAB estab[200];
int TOTAL_PROG_LENGTH = 0; 
int current_dump_address = 0; 
int ENTRY_POINT = 0; 

// [Simulator Registers]
// 기능: SIC/XE 레지스터 집합
// 설명: 48비트 출력 지원 위해 long long 사용, 플로팅포인트는 double
long long reg_A = 0, reg_X = 0, reg_L = 0, reg_B = 0, reg_S = 0, reg_T = 0; 
int reg_SW = 0;        // 조건 플래그 (비교 결과 등)
long long reg_PC = 0;  // Program Counter
double reg_F = 0.0;    // Floating point register

// [Breakpoint]
// 기능: 브레이크포인트 저장 배열
int breakpoints[MAX_BP];
int bp_count = 0;

// 브레이크 포인트에서 중단된 상태인지 저장
int paused_at_breakpoint = FALSE;

// --- Function Prototypes ---
// 기능: 함수 선언부
// 설명: main_loop 이전에 사용되는 모든 함수 프로토타입 명시
void rtrim(char* str);
int hex_str_to_int(char* hex_str, int length);
unsigned char get_byte(long long addr);
int get_word(long long addr);
void set_word(long long addr, int value);
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
int search_estab(char* name, int* addr_ptr);
void loader_pass1(char* filenames[], int file_count);
void loader_pass2(char* filenames[], int file_count);
void print_load_map(void);
void command_load(char* args);
void command_dump(char* args);
void command_bp(char* args);
void reset_simulator_regs();
void print_regs();
void command_run();
void display_help();
void command_progaddr(char* args);

// 기능: 문자열 오른쪽 공백 제거
// 설명: 라인 끝의 공백/개행 문자를 제거하여 파싱 안정성 확보
void rtrim(char *str)
{
    int n = strlen(str);
    while (n > 0 && (isspace((unsigned char)str[n - 1]))) {
        str[n - 1] = '\0';
        n--;
    }
}

// 기능: 부분 HEX 문자열을 정수로 변환
// 설명: 라인 중 일부(길이 length)를 잘라 유효한 16진수만 골라 int로 변환
int hex_str_to_int(char* hex_str, int length) {
    char temp_str[20] = {0};
    if (length > 18) length = 18;
    strncpy(temp_str, hex_str, length);
    temp_str[length] = '\0';

    char clean_str[20] = {0};
    int j = 0;
    for (int i = 0; i < (int)strlen(temp_str); i++) {
        if (isxdigit((unsigned char)temp_str[i])) clean_str[j++] = temp_str[i];
    }
    clean_str[j] = '\0';

    if (strlen(clean_str) == 0) return 0;
    return (int)strtol(clean_str, NULL, 16);
}

// addr을 long long으로 변경
// 기능: 메모리에서 1바이트 읽기
// 설명: 주소 범위를 체크하고 virtual_memory에서 해당 바이트를 반환
unsigned char get_byte(long long addr) {
    if (addr < 0 || addr >= MEMORY_SIZE) return 0;
    return virtual_memory[addr];
}

// addr을 long long으로 변경
// 기능: 메모리에서 3바이트(WORD) 읽기
// 설명: 3바이트를 묶어 24비트 정수로 만들고, 부호 확장을 해서 int로 리턴
int get_word(long long addr) { 
    if (addr < 0 || addr > MEMORY_SIZE - 3) return 0;
    int val = (get_byte(addr) << 16) | (get_byte(addr + 1) << 8) | get_byte(addr + 2);
    if (val & 0x800000) val |= 0xFF000000;   // 24비트 부호 확장
    return val;
}

// addr을 long long으로 변경
// 기능: 메모리에 3바이트(WORD) 기록
// 설명: value의 상위 3바이트를 잘라 virtual_memory에 순서대로 저장
void set_word(long long addr, int value) { 
    if (addr < 0 || addr > MEMORY_SIZE - 3) return;
    virtual_memory[addr]     = (value >> 16) & 0xFF;
    virtual_memory[addr + 1] = (value >> 8) & 0xFF;
    virtual_memory[addr + 2] = value & 0xFF;
}

// --- Assembler Functions ---

// 기능: 어셈블러 전역 상태 초기화
// 설명: OPTAB/SYMTAB/DEFTAB/REFTAB/텍스트 레코드 등 초기값 설정
void reset_assemble_global(void){
    optab_size = 0; 
    symtab_size = 0; 
    deftab_size = 0; 
    reftab_size = 0;
    mod_record_count = 0; 
    startAdress = 0; 
    programLength = 0; 
    LOCCTR = 0;
    TS = 0; 
    BASE_REG_LOC = 0; 
    is_base_relative_active = FALSE;
    strcpy(text_record, "");
}

// 기능: 레지스터 문자 → 번호로 매핑
// 설명: SIC/XE 레지스터 인덱스 (A=0, X=1, L=2, B=3, S=4, T=5, F=6)
int get_register_number(char reg_char) {
    switch (toupper(reg_char)) {
        case 'A': return 0; 
        case 'X': return 1; 
        case 'L': return 2;
        case 'B': return 3; 
        case 'S': return 4; 
        case 'T': return 5;
        case 'F': return 6; 
        default: return -1; 
    }
}

// 기능: OPTAB 초기화
// 설명: opcode.txt에서 읽어오거나, 없으면 기본 내장 명령어 테이블을 구성
void initialize_optab(const char* filename) {
    optab_size = 0;
    FILE *fp_op = fopen(filename, "r");
    if (fp_op != NULL) {
        char line[80], mnemonic[10];
        int opcode_hex, format;
        while (fgets(line, sizeof(line), fp_op) != NULL) {
            if (sscanf(line, "%s %x %d", mnemonic, &opcode_hex, &format) == 3) {
                if (optab_size < 100) {
                    strcpy(optab[optab_size].name, mnemonic);
                    optab[optab_size].code = opcode_hex;
                    optab[optab_size].format = format;
                    optab_size++;
                }
            }
        }
        fclose(fp_op);
    }
    // 파일에서 읽은 게 하나도 없을 경우 기본 내장 테이블 사용
    if (optab_size == 0) {
        struct { char* n; int c; int f; } builtin[] = {
            {"LDA", 0x00, 3}, {"LDX", 0x04, 3}, {"LDL", 0x08, 3}, {"STA", 0x0C, 3},
            {"STX", 0x10, 3}, {"STL", 0x14, 3}, {"ADD", 0x18, 3}, {"SUB", 0x1C, 3},
            {"MUL", 0x20, 3}, {"DIV", 0x24, 3}, {"COMP", 0x28, 3}, {"TIX", 0x2C, 3},
            {"JEQ", 0x30, 3}, {"JGT", 0x34, 3}, {"JLT", 0x38, 3}, {"J", 0x3C, 3},
            {"AND", 0x40, 3}, {"OR", 0x44, 3}, {"JSUB", 0x48, 3}, {"RSUB", 0x4C, 3},
            {"LDCH", 0x50, 3}, {"STCH", 0x54, 3}, {"ADDF", 0x58, 3}, {"SUBF", 0x5C, 3},
            {"MULF", 0x60, 3}, {"DIVF", 0x64, 3}, {"LDB", 0x68, 3}, {"LDS", 0x6C, 3},
            {"LDF", 0x70, 3}, {"LDT", 0x74, 3}, {"STB", 0x78, 3}, {"STS", 0x7C, 3},
            {"STF", 0x80, 3}, {"STI", 0x84, 3}, {"COMPF", 0x88, 3}, 
            {"ADDR", 0x90, 2}, {"SUBR", 0x94, 2}, {"MULR", 0x98, 2}, {"DIVR", 0x9C, 2},
            {"COMPR", 0xA0, 2}, {"SHIFTL", 0xA4, 2}, {"SHIFTR", 0xA8, 2}, {"RMO", 0xB0, 2},
            {"CLEAR", 0xB4, 2}, {"TIXR", 0xB8, 2},
            {"FLOAT", 0xC0, 1}, {"FIX", 0xC4, 1}, {"NORM", 0xC8, 1},
            {"TD", 0xE0, 3}, {"RD", 0xD8, 3}, {"WD", 0xDC, 3}
        };
        int count = sizeof(builtin)/sizeof(builtin[0]);
        for (int i = 0; i < count; i++) {
            strcpy(optab[optab_size].name, builtin[i].n);
            optab[optab_size].code = builtin[i].c;
            optab[optab_size].format = builtin[i].f;
            optab_size++;
        }
    }
}

// 기능: opcode 문자열을 OPTAB에서 검색
// 설명: '+' 프리픽스 제거 후 명령어 테이블에서 일치 항목 찾기
int searchop(char *cp) {
    char temp_op[10]; 
    strcpy(temp_op, cp);
    // 포맷 4(+명령)인 경우 '+' 제거
    if (temp_op[0] == '+') memmove(temp_op, temp_op + 1, strlen(temp_op));
    for (int i = 0; i < optab_size; ++i) {
        if (strcmp(temp_op, optab[i].name) == 0) {
            opcode_index = i; 
            return TRUE; 
        }
    }
    return FALSE;
}

// 기능: SYMTAB에서 심볼 검색
// 설명: #/@ prefix 제거 및 ,X 제거 후 심볼 테이블에서 이름 찾기
int searchsymbol(char *cp) {
    char dp[10]; 
    strcpy(dp, cp);
    char *token = dp; 
    if (dp[0] == '#' || dp[0] == '@') token++; // immediate / indirect 처리
    char* comma_ptr = strstr(token, ","); 
    if (comma_ptr != NULL) *comma_ptr = '\0';   // ,X 같은 부분 잘라냄
    for (int i = 0; i < symtab_size; i++) {
        if (strcmp(token, symtab[i].symtab_label) == 0) {
            symtab_index = i; 
            return TRUE; 
        }
    }
    return FALSE;
}

// 기능: REFTAB(EXTREF 목록)에서 심볼 검색
// 설명: EXTREF로 선언된 외부 심볼인지 확인
int search_reftab(char *cp) {
    char dp[10]; 
    strcpy(dp, cp);
    char *token = dp; 
    if (dp[0] == '#' || dp[0] == '@') token++;
    char* comma_ptr = strstr(token, ","); 
    if (comma_ptr != NULL) *comma_ptr = '\0';
    for (int i = 0; i < reftab_size; i++) {
        if (strcmp(token, reftab[i].name) == 0) return TRUE;
    }
    return FALSE;
}

// 기능: 주어진 문자열이 Directive인지 판별
// 설명: START, END, BYTE, WORD, RESW, RESB, BASE, EXTDEF, EXTREF 등 확인
int is_directive(char* s) {
    const char* directives[] = {
        "START", "END", "BYTE", "WORD", 
        "RESW", "RESB", "BASE", "EXTDEF", "EXTREF", 
        NULL
    };
    for (int i = 0; directives[i] != NULL; i++)
        if (strcmp(s, directives[i]) == 0) return TRUE;
    return FALSE;
}

// 기능: 한 줄의 코드에서 label/opcode/operand 분리
// 설명: 주석(.)인 줄, opcode/directive 우선, 아니면 label + opcode + operand 순으로 분리
void getCode(char* buf, char* out_label, char* out_opcode, char* out_operand) {
    char* token; 
    char temp_buf[100]; 
    char* tokens[3] = {NULL, NULL, NULL}; 
    int token_count = 0;

    strcpy(out_label, ""); 
    strcpy(out_opcode, ""); 
    strcpy(out_operand, "");

    // 주석 라인(맨 앞이 '.')은 label에 '.' 표시만 하고 종료
    if (buf[0] == '.') { 
        strcpy(out_label, "."); 
        return; 
    }

    strcpy(temp_buf, buf);
    token = strtok(temp_buf, " \t\n\r"); 
    while (token != NULL && token_count < 3) {
        tokens[token_count++] = token;
        token = strtok(NULL, " \t\n\r");
    }
    if (token_count == 0) return; 

    // 첫 토큰이 opcode/directive라면 label 없이 opcode/operand만 설정
    if (searchop(tokens[0]) || is_directive(tokens[0])) { 
        strcpy(out_opcode, tokens[0]); 
        if (tokens[1] != NULL) strcpy(out_operand, tokens[1]);
    } else { 
        // 그렇지 않으면 첫 토큰은 label, 다음이 opcode, 그다음이 operand
        strcpy(out_label, tokens[0]); 
        if (tokens[1] != NULL) strcpy(out_opcode, tokens[1]);
        if (tokens[2] != NULL) strcpy(out_operand, tokens[2]);
    }
}

// 기능: LOCCTR(현재 주소) 증가
// 설명: 명령어 포맷 또는 Directive 종류에 따라 바이트 수를 더해 줌
void plus_locctr() {
    is_format_4 = FALSE;

    // +OP 형태면 포맷 4, 길이 4바이트
    if (opcode[0] == '+') { 
        is_format_4 = TRUE; 
        LOCCTR += 4; 
        return; 
    }

    if (searchop(opcode)) { 
        LOCCTR += optab[opcode_index].format; 
    }
    else if (strcmp(opcode, "WORD") == 0) LOCCTR += 3;
    else if (strcmp(opcode, "RESW") == 0) LOCCTR += 3 * atoi(operand);
    else if (strcmp(opcode, "RESB") == 0) LOCCTR += atoi(operand);
    else if (strcmp(opcode, "BYTE") == 0) {
        // C'...' 은 문자 개수만큼, X'...'는 2개의 16진수가 1바이트
        if (operand[0] == 'C') LOCCTR += (int)strlen(operand) - 3; 
        else if (operand[0] == 'X') LOCCTR += ((int)strlen(operand) - 3) / 2; 
    }
}

// 기능: BYTE directive의 피연산자(C'..' 또는 X'..')를 실제 값으로 변환
// 설명: C는 아스키 코드 나열, X는 16진수 문자열을 그대로 붙여서 정수로 만든다
unsigned int get_byte_code(char* cp) {
    unsigned int nsum = 0; 
    char temp_operand[100]; 
    strcpy(temp_operand, cp);

    // C'ABC' / X'F1' 형태에서 데이터 시작 위치
    char *data_start = temp_operand + 2; 
    temp_operand[strlen(temp_operand) - 1] = '\0'; 

    if (cp[0] == 'C') {
        // 문자 하나씩 8비트씩 shift하면서 누적
        for (int i = 0; data_start[i] != '\0'; i++)
            nsum = (nsum << 8) + (int)data_start[i];
    } 
    else if (cp[0] == 'X') { 
        // 2개의 hex가 1바이트, 한 글자씩 4비트로 누적
        for (int i = 0; data_start[i] != '\0'; i++) {
            int n; 
            if (data_start[i] >= '0' && data_start[i] <= '9') 
                n = data_start[i] - '0';
            else 
                n = toupper(data_start[i]) - 'A' + 10;
            nsum = (nsum << 4) + n; 
        }
    }
    return nsum;
}

// 기능: T(Text) 레코드를 오브젝트 파일에 실제로 출력
// 설명: text_record에 누적된 오브젝트 코드들을 한 줄 T 레코드로 작성
void write_T_record(FILE *obj_fp) {
    if (strlen(text_record) > 0) 
        fprintf(obj_fp, "T%06X%02X%s\n", TS, (int)strlen(text_record) / 2, text_record);
    strcpy(text_record, "");
}

// 기능: 어셈블러 Pass 1
// 설명: 소스 파일을 읽으면서 SYMTAB, DEFTAB, REFTAB 구성 및 LOCCTR 계산
void pass_one(FILE *fp) {
    char buf[100];

    // 첫 줄(START 여부) 읽기
    fgets(buf, sizeof(buf), fp); 
    getCode(buf, label, opcode, operand);
    if (strcmp(opcode, "START") == 0) { 
        LOCCTR = (int)strtol(operand, NULL, 16); 
        startAdress = LOCCTR; 
    } else { 
        LOCCTR = 0; 
        startAdress = 0; 
    }

    // 본문 라인 처리
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        getCode(buf, label, opcode, operand);

        if (strcmp(opcode, "END") == 0) break; 
        if (label[0] == '.') continue; // 주석 라인 무시

        // EXTDEF: 외부로 정의되는 심볼 이름을 DEFTAB에 저장
        if (strcmp(opcode, "EXTDEF") == 0) {
            char temp_operand[100]; 
            strcpy(temp_operand, operand); 
            char* sym = strtok(temp_operand, ",");
            while (sym != NULL) { 
                rtrim(sym); 
                strcpy(deftab[deftab_size++].name, sym); 
                sym = strtok(NULL, ","); 
            }
            continue;
        }

        // EXTREF: 외부에서 들어오는 심볼 이름을 REFTAB에 저장
        if (strcmp(opcode, "EXTREF") == 0) {
            char temp_operand[100]; 
            strcpy(temp_operand, operand); 
            char* sym = strtok(temp_operand, ",");
            while (sym != NULL) { 
                rtrim(sym); 
                strcpy(reftab[reftab_size++].name, sym); 
                sym = strtok(NULL, ","); 
            }
            continue;
        }

        // label이 있으면 SYMTAB에 현재 LOCCTR 주소로 등록
        if (label[0] != '\0') {
            if (!searchsymbol(label)) { 
                strcpy(symtab[symtab_size].symtab_label, label); 
                symtab[symtab_size].loc = LOCCTR; 
                symtab_size++; 
            }
        }
        // 현재 라인의 opcode/operand 기반으로 LOCCTR 증가
        plus_locctr();
    }

    // 프로그램 전체 길이 계산
    programLength = LOCCTR - startAdress;

    // DEFTAB에 등록했던 이름들의 실제 주소 채워넣기
    for (int i = 0; i < deftab_size; i++) {
        if (searchsymbol(deftab[i].name))
            deftab[i].addr = symtab[symtab_index].loc;
    }
}

// 기능: 어셈블러 Pass 2
// 설명: SYMTAB/DEFTAB/REFTAB 정보를 이용하여 실제 오브젝트 코드(H/D/R/T/M/E)를 생성
void pass_two(FILE *fp, FILE *obj_fp, FILE *lst_fp) {
    LOCCTR = startAdress; 
    TS = LOCCTR; 
    char buf[100]; 
    int line_num = 0; 
    strcpy(text_record, "");

    // 첫 줄(START) 다시 읽기
    fgets(buf, sizeof(buf), fp); 
    getCode(buf, label, opcode, operand);

    // LST 파일에 첫 줄 출력
    if (strcmp(opcode, "START") == 0)
        fprintf(lst_fp, "%-4d %04X %-8s %-8s %-10s\n", 
                line_num, startAdress, label, opcode, operand);
    
    // H 레코드(프로그램 이름, 시작 주소, 길이)
    char prog_name[7]; 
    strcpy(prog_name, label); 
    if (strlen(prog_name) == 0) strcpy(prog_name, "NONAME"); 
    fprintf(obj_fp, "H%-6s%06X%06X\n", prog_name, startAdress, programLength);
    
    // D 레코드 (EXTDEF)
    if (deftab_size > 0) {
        fprintf(obj_fp, "D"); 
        for (int i = 0; i < deftab_size; i++)
            fprintf(obj_fp, "%-6s%06X", deftab[i].name, deftab[i].addr);
        fprintf(obj_fp, "\n");
    }

    // R 레코드 (EXTREF)
    if (reftab_size > 0) {
        fprintf(obj_fp, "R"); 
        for (int i = 0; i < reftab_size; i++)
            fprintf(obj_fp, "%-6s", reftab[i].name);
        fprintf(obj_fp, "\n");
    }
    
    // 본문 라인 처리 (T/M/E 레코드 중심)
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        line_num += 5; // 라인 번호 증가 단위
        int current_loc_for_listing = LOCCTR; 
        strcpy(objectcode_text, ""); 
        int is_external = FALSE; 

        getCode(buf, label, opcode, operand);

        // END 처리: LST에 마지막 줄 출력 후 종료
        if (strcmp(opcode, "END") == 0) { 
            fprintf(lst_fp, "%-4d %-4s %-8s %-8s %-10s\n", 
                    line_num, "", label, opcode, operand); 
            break; 
        }

        // EXTDEF/EXTREF는 Pass2에서는 코드 생성 없이 LST만 찍고 continue
        if (strcmp(opcode, "EXTDEF") == 0 || strcmp(opcode, "EXTREF") == 0) { 
            fprintf(lst_fp, "%-4d %-4s %-8s %-8s %-10s\n", 
                    line_num, "", label, opcode, operand); 
            continue; 
        }

        // 주석 라인 또는 빈 opcode 라인 처리
        if (label[0] == '.' || opcode[0] == '\0') { 
            if (label[0] == '.') 
                fprintf(lst_fp, "%-4d %s", line_num, buf); 
            continue; 
        }
        
        // 현재 LOCCTR 저장 후 plus_locctr()로 다음 주소 계산
        int current_loc = LOCCTR; 
        plus_locctr(); 
        int next_loc = LOCCTR; 
        LOCCTR = current_loc;  // 이후 계산을 위해 다시 되돌림
        
        // --- 실제 오브젝트 코드 생성 ---
        if (searchop(opcode)) {
            is_format_4 = (opcode[0] == '+'); 
            // 포맷 3 or 4
            if (optab[opcode_index].format == 3 || is_format_4) {

                // operand 주소 계산(심볼/외부심볼/즉시값)
                if (strcmp(operand, "") != 0) { 
                    if (searchsymbol(operand))
                        operandAddress = symtab[symtab_index].loc;
                    else if (search_reftab(operand)) { 
                        // EXTREF 심볼
                        is_external = TRUE; 
                        operandAddress = 0; 
                    }
                    else if (operand[0] == '#') { 
                        // immediate 형태: #숫자 / #심볼
                        char* endptr; 
                        operandAddress = (unsigned int)strtol(operand + 1, &endptr, 10); 
                        if (*endptr != '\0') { 
                            // 숫자가 아니면 심볼로 취급
                            if (searchsymbol(operand)) 
                                operandAddress = symtab[symtab_index].loc; 
                            else 
                                operandAddress = 0; 
                        }
                    } else 
                        operandAddress = 0;
                } else 
                    operandAddress = 0;
                
                if (is_format_4) { 
                    // 포맷4(4바이트): [opcode+ni][xbpe][20-bit addr]
                    int n=1, i=1, x=0, b=0, p=0, e=1;
                    if (operand[0] == '#') { n=0; i=1; } 
                    else if (operand[0] == '@') { n=1; i=0; }
                    if (strstr(operand, ",X") != NULL) x=1;

                    objectcode = (optab[opcode_index].code + (n<<1) + i) << 24; 
                    objectcode |= (x<<23) | (b<<22) | (p<<21) | (e<<20);      
                    objectcode |= (operandAddress & 0xFFFFF); 
                    sprintf(objectcode_text, "%08X", objectcode);

                    // 외부 참조 심볼이면 M 레코드 정보(mod_records)에 추가
                    if (is_external) { 
                        mod_records[mod_record_count].location = current_loc + 1; 
                        mod_records[mod_record_count].length = 5; 
                        char temp_operand[10]; 
                        strcpy(temp_operand, operand); 
                        char* comma_ptr = strstr(temp_operand, ","); 
                        if (comma_ptr != NULL) *comma_ptr = '\0'; 
                        strcpy(mod_records[mod_record_count].symbol_name, temp_operand); 
                        mod_record_count++;
                    }
                } else { 
                    // 포맷3(3바이트): [opcode+ni][xbpe][12-bit disp]
                    int n=1, i=1, x=0, b=0, p=0, e=0; 
                    long disp = 0;

                    if (strcmp(optab[opcode_index].name, "RSUB") == 0) {
                        // RSUB는 피연산자 없이 고정 패턴
                        objectcode = 0x4F0000;
                    } else {
                        if (operand[0] == '#') { n=0; i=1; } 
                        else if (operand[0] == '@') { n=1; i=0; }
                        if (strstr(operand, ",X") != NULL) x=1;

                        if (n==0 && i==1 && !is_external) { 
                            // immediate 숫자일 경우 disp에 직접 값
                            char* endptr; 
                            long val = strtol(operand + 1, &endptr, 10);
                            if (*endptr == '\0') 
                                disp = val; 
                            else 
                                disp = (long)operandAddress;
                        } else { 
                            // PC-relative / Base-relative / 절대 주소
                            disp = (long)operandAddress - next_loc; 
                            if (disp >= -2048 && disp <= 2047) 
                                p = 1; // PC-relative
                            else if (is_base_relative_active) { 
                                disp = (long)operandAddress - BASE_REG_LOC; 
                                if (disp >= 0 && disp <= 4095) 
                                    b = 1; // Base-relative
                                else 
                                    disp = operandAddress; 
                            } else 
                                disp = operandAddress;
                        }
                    }
                    objectcode = (optab[opcode_index].code + (n<<1) + i) << 16; 
                    objectcode |= (x<<15) | (b<<14) | (p<<13) | (e<<12);
                    objectcode |= (disp & 0xFFF); 
                    sprintf(objectcode_text, "%06X", objectcode);
                }
            } 
            // 포맷2
            else if (optab[opcode_index].format == 2) {
                objectcode = (optab[opcode_index].code << 8); 
                char temp_operand[20]; 
                strcpy(temp_operand, operand); 
                char* r1_str = strtok(temp_operand, ","); 
                char* r2_str = strtok(NULL, ",");

                if (r1_str != NULL) { 
                    int r1_num = get_register_number(r1_str[0]); 
                    if (r1_num != -1) objectcode |= (r1_num << 4); 
                }
                if (r2_str != NULL) { 
                    int r2_num = get_register_number(r2_str[0]); 
                    if (r2_num != -1) objectcode |= r2_num; 
                }
                sprintf(objectcode_text, "%04X", objectcode);
            } 
            // 포맷1
            else if (optab[opcode_index].format == 1) { 
                objectcode = optab[opcode_index].code; 
                sprintf(objectcode_text, "%02X", objectcode); 
            }
        } 
        // Directive 처리
        else if (strcmp(opcode, "BYTE") == 0) {
            objectcode = get_byte_code(operand); 
            int len = (operand[0] == 'C') ? (int)((strlen(operand) - 3) * 2) : (int)(strlen(operand) - 3);
            sprintf(objectcode_text, "%.*X", len, (unsigned int)objectcode);
        } 
        else if (strcmp(opcode, "WORD") == 0) { 
            objectcode = (unsigned int)atoi(operand); 
            sprintf(objectcode_text, "%06X", objectcode); 
        }
        else if (strcmp(opcode, "BASE") == 0) { 
            // BASE 주소 세팅 (Base-relative 주소 계산에 사용)
            if (searchsymbol(operand)) { 
                BASE_REG_LOC = symtab[symtab_index].loc; 
                is_base_relative_active = TRUE; 
            } 
        }
        else if (strcmp(opcode, "RESW") == 0 || strcmp(opcode, "RESB") == 0) {
            // 메모리 예약은 실제 오브젝트 코드를 생성하지 않고 T 레코드 끊기
            write_T_record(obj_fp); 
            TS = current_loc + ((strcmp(opcode, "RESW") == 0) ? 3 * atoi(operand) : atoi(operand));
        }

        // LOCCTR 실제 이동
        plus_locctr();

        // RESW/RESB/BASE가 아닌 경우에만 T 레코드에 코드 추가
        if (strcmp(opcode, "RESW") != 0 && strcmp(opcode, "RESB") != 0 && strcmp(opcode, "BASE") != 0) {
            // T 레코드 최대 길이(30바이트=60 hex문자)를 넘으면 끊고 새 T 시작
            if (strlen(text_record) + strlen(objectcode_text) > 60) { 
                write_T_record(obj_fp); 
                TS = current_loc; 
            }
            if (strlen(text_record) == 0) 
                TS = current_loc; 
            strcat(text_record, objectcode_text);
        }

        // LST 파일에 라인 기록
        fprintf(lst_fp, "%-4d %04X %-8s %-8s %-10s %s\n", 
                line_num, current_loc_for_listing, label, opcode, operand, objectcode_text);
    }

    // 남은 T 레코드 출력
    write_T_record(obj_fp);

    // M 레코드 출력 (외부 심볼 수정용)
    for (int i = 0; i < mod_record_count; i++)
        fprintf(obj_fp, "M%06X%02X+%s\n", 
                mod_records[i].location, mod_records[i].length, mod_records[i].symbol_name);

    // E 레코드 (프로그램 시작 주소)
    fprintf(obj_fp, "E%06X\n", startAdress);
}

// 기능: assemble 명령 처리
// 설명: 사용자에게 파일 이름 입력받고, Pass1/Pass2 수행 후 obj/lst 파일 생성
void command_assemble() {
    char input_filename[256], opcode_filename[256], obj_filename[256], lst_filename[256]; 
    char* dot_position;

    printf("Enter opcode file name (e.g., opcode.txt): "); 
    scanf("%s", opcode_filename);
    printf("Enter source file name (e.g., your_source.asm): "); 
    scanf("%s", input_filename);

    reset_assemble_global(); 

    // 확장자를 .obj / .lst 로 바꿔서 출력 파일명 생성
    strncpy(obj_filename, input_filename, 256); 
    dot_position = strrchr(obj_filename, '.'); 
    if (dot_position) strcpy(dot_position, ".obj"); 
    else strcat(obj_filename, ".obj");

    strncpy(lst_filename, input_filename, 256); 
    dot_position = strrchr(lst_filename, '.'); 
    if (dot_position) strcpy(dot_position, ".lst"); 
    else strcat(lst_filename, ".lst");

    initialize_optab(opcode_filename);

    FILE *fp_in, *fp_obj, *fp_lst;

    // Pass1
    if ((fp_in = fopen(input_filename, "r")) == NULL) { 
        fprintf(stderr, "Cannot find: %s\n", input_filename); 
        return; 
    } 
    pass_one(fp_in); 
    fclose(fp_in);

    // Pass2
    if ((fp_in = fopen(input_filename, "r")) == NULL) return; 
    if ((fp_obj = fopen(obj_filename, "w")) == NULL) return; 
    if ((fp_lst = fopen(lst_filename, "w")) == NULL) return;

    pass_two(fp_in, fp_obj, fp_lst); 

    fclose(fp_in); 
    fclose(fp_obj); 
    fclose(fp_lst);

    printf("Assembly successful. Output files: [%s], [%s]\n", obj_filename, lst_filename);
}

// --- Loader Functions ---

// 기능: ESTAB(외부 심볼 테이블)에서 이름 검색
// 설명: control section 이름 또는 외부 심볼 이름을 찾고, 주소를 addr_ptr에 돌려줌
int search_estab(char* name, int* addr_ptr) {
    for (int i = 0; i < estab_size; i++) {
        if (strcmp(estab[i].name, name) == 0) { 
            if (addr_ptr != NULL) 
                *addr_ptr = estab[i].addr; 
            return 1; 
        }
    }
    return 0; 
}

// 기능: Loader Pass1
// 설명: 각 obj 파일의 H/D 레코드를 읽어서 ESTAB 구성, control section 길이 누적
void loader_pass1(char* filenames[], int file_count) {
    int CSADDR = PROGADDR;  // 현재 control section 적재 시작 주소
    int CSLTH = 0;          // 현재 control section 길이
    FILE *fp;
    char line[100];

    estab_size = 0; 
    ENTRY_POINT = PROGADDR; 

    for (int i = 0; i < file_count; i++) {
        fp = fopen(filenames[i], "r");
        if (fp == NULL) {
            fprintf(stderr, "Error: Cannot open file '%s'.\n", filenames[i]);
            continue; 
        }

        while (fgets(line, sizeof(line), fp) != NULL) {
            rtrim(line); 
            if (strlen(line) < 1) continue;

            // H 레코드 처리: control section 이름, 길이 읽기
            if (line[0] == 'H') {
                char cs_name[20] = {0};
                int start_addr_temp = 0;
                int fields = sscanf(line, "H %s %x %x", cs_name, &start_addr_temp, &CSLTH);
                if (fields < 3) {
                    // 공백 없이 붙어있는 포맷일 가능성 대비
                    if (strlen(line) >= 19) 
                        CSLTH = hex_str_to_int(line + 13, 6);
                    else 
                        CSLTH = 0;
                    strncpy(cs_name, line + 1, 6);
                    cs_name[6] = '\0';
                    rtrim(cs_name);
                }
                if (strlen(cs_name) == 0) 
                    sprintf(cs_name, "UNK%d", i + 1);

                // control section 이름 중복 검사
                if (search_estab(cs_name, NULL)) 
                    fprintf(stderr, "Error: Duplicate Control Section Name '%s'\n", cs_name);

                strcpy(estab[estab_size].name, cs_name);
                estab[estab_size].addr = CSADDR;
                estab[estab_size].control_section = 1;  // control section entry
                estab[estab_size].length = CSLTH;
                estab_size++;
            } 
            // D 레코드 처리: 심볼 이름과 상대 주소를 ESTAB에 등록
            else if (line[0] == 'D') {
                char buffer[100];
                strcpy(buffer, line); 
                char* token = strtok(buffer + 1, " \t"); 
                while (token != NULL) {
                    char symbol[20];
                    strcpy(symbol, token);
                    token = strtok(NULL, " \t"); 
                    if (token == NULL) break;
                    int addr = 0;
                    sscanf(token, "%x", &addr);

                    // 중복 심볼 검사
                    if (search_estab(symbol, NULL)) {
                        fprintf(stderr, "Error: Duplicate Symbol '%s'. Skipped in Pass 1.\n", symbol);
                    } else {
                        strcpy(estab[estab_size].name, symbol);
                        estab[estab_size].addr = CSADDR + addr;
                        estab[estab_size].control_section = 0; 
                        estab[estab_size].length = 0;
                        estab_size++;
                    }
                    token = strtok(NULL, " \t"); 
                }
            }
        } 
        fclose(fp);
        // 한 control section 끝났으니 다음 CSADDR로 이동
        CSADDR += CSLTH; 
    } 
    TOTAL_PROG_LENGTH = CSADDR - PROGADDR;
}

// [Fix] Enhanced T-record parsing to handle spaces and exact bytes
// 기능: Loader Pass2
// 설명: T/M/E 레코드를 이용해 실제 virtual_memory에 코드 적재 및 재배치 처리
void loader_pass2(char* filenames[], int file_count) {
    int CSADDR = PROGADDR; 
    int CSLTH = 0;         
    FILE *fp;
    char line[100];
    
    for (int i = 0; i < file_count; i++) {
        fp = fopen(filenames[i], "r");
        if (fp == NULL) continue; 

        while (fgets(line, sizeof(line), fp) != NULL) {
            rtrim(line); 
            if (strlen(line) < 1) continue;

            if (line[0] == 'H') {
                // control section 길이만 읽어서 CSLTH 갱신
                char temp_name[20];
                int temp_start;
                if (sscanf(line, "H %s %x %x", temp_name, &temp_start, &CSLTH) < 3) {
                    if (strlen(line) >= 19) 
                        CSLTH = hex_str_to_int(line + 13, 6);
                    else 
                        CSLTH = 0;
                }
            } 
            else if (line[0] == 'T') {
                int t_addr = 0;
                int t_len = 0;
                char *ptr = line + 1; 
                
                // 주소 및 길이 부분 유연하게 파싱
                while (isspace((unsigned char)*ptr)) ptr++; 
                sscanf(ptr, "%x", &t_addr); 
                while (!isspace((unsigned char)*ptr) && *ptr) ptr++; 
                while (isspace((unsigned char)*ptr)) ptr++;
                sscanf(ptr, "%x", &t_len);
                while (!isspace((unsigned char)*ptr) && *ptr) ptr++; 
                while (isspace((unsigned char)*ptr)) ptr++;

                int current_addr = CSADDR + t_addr;
                
                // 남은 부분에서 HEX 문자만 뽑아 hex_buffer에 저장
                char hex_buffer[300] = {0};
                int buf_idx = 0;
                for (int k = 0; ptr[k] != '\0'; k++) {
                    if (isxdigit((unsigned char)ptr[k])) {
                        hex_buffer[buf_idx++] = ptr[k];
                    }
                }
                
                // 2자리씩 끊어서 1바이트씩 메모리에 기록
                for (int j = 0; j < buf_idx; j += 2) {
                    if (j + 1 >= buf_idx) break; 
                    char byte_str[3] = {hex_buffer[j], hex_buffer[j+1], '\0'};
                    int byte_val = 0;
                    sscanf(byte_str, "%x", &byte_val);
                    if (current_addr < MEMORY_SIZE) {
                        virtual_memory[current_addr] = (unsigned char)byte_val;
                        status_map[current_addr++] = 1; // 사용됨 상태로 변경
                    }
                }
            } 
            else if (line[0] == 'M') {
                // M 레코드: 재배치 정보
                int m_addr = 0;
                int m_len = 0;
                char symbol[20] = {0};
                char sign = '+';
                char *ptr = line + 1;

                while (isspace((unsigned char)*ptr)) ptr++; 
                sscanf(ptr, "%x", &m_addr);
                while (!isspace((unsigned char)*ptr) && *ptr != '\0') ptr++; 
                while (isspace((unsigned char)*ptr) && *ptr != '\0') ptr++; 
                sscanf(ptr, "%x", &m_len);
                while (!isspace((unsigned char)*ptr) && *ptr != '\0') ptr++; 
                while (isspace((unsigned char)*ptr) && *ptr != '\0') ptr++; 

                if (*ptr == '+' || *ptr == '-') {
                    sign = *ptr;
                    strcpy(symbol, ptr + 1);
                } else {
                    strcpy(symbol, ptr);
                }
                rtrim(symbol);

                int modification_value = 0;
                if (!search_estab(symbol, &modification_value)) 
                    continue;  // ESTAB에 없는 심볼이면 건너뜀

                int mod_addr = CSADDR + m_addr;
                long current_val = 0;

                // m_len이 5/6 nibble 인지에 따라 읽는 방법이 다름
                if (m_len == 5) { 
                    current_val = (virtual_memory[mod_addr] & 0x0F) << 16;
                    current_val |= (virtual_memory[mod_addr + 1]) << 8;
                    current_val |= (virtual_memory[mod_addr + 2]);
                    if (current_val & 0x80000) 
                        current_val |= 0xFFF00000; 
                } else if (m_len == 6) { 
                    current_val = (virtual_memory[mod_addr]) << 16;
                    current_val |= (virtual_memory[mod_addr + 1]) << 8;
                    current_val |= (virtual_memory[mod_addr + 2]);
                    if (current_val & 0x800000) 
                        current_val |= 0xFF000000;
                }

                // + 또는 - 기호에 따라 주소 조정
                if (sign == '+') current_val += modification_value;
                else current_val -= modification_value;

                // 수정된 값을 다시 메모리에 써준다
                if (m_len == 5) {
                    virtual_memory[mod_addr] = (virtual_memory[mod_addr] & 0xF0) | ((current_val >> 16) & 0x0F);
                    virtual_memory[mod_addr + 1] = (current_val >> 8) & 0xFF;
                    virtual_memory[mod_addr + 2] = current_val & 0xFF;
                } else if (m_len == 6) {
                    virtual_memory[mod_addr] = (current_val >> 16) & 0xFF;
                    virtual_memory[mod_addr + 1] = (current_val >> 8) & 0xFF;
                    virtual_memory[mod_addr + 2] = current_val & 0xFF;
                }
            } 
            else if (line[0] == 'E') {
                // E 레코드: 엔트리 포인트 설정
                int e_addr = 0;
                if (strlen(line) > 1) {
                    char *ptr = line + 1;
                    while (isspace((unsigned char)*ptr)) ptr++;
                    if (*ptr != '\0') {
                        sscanf(ptr, "%x", &e_addr);
                        ENTRY_POINT = CSADDR + e_addr;
                    }
                }
            }
        } 
        fclose(fp);
        CSADDR += CSLTH; 
    } 
}

// 기능: 적재 지도(Load Map) 출력
// 설명: control section별 시작주소/길이, 외부 심볼 주소 등을 보기 좋게 출력
void print_load_map(void) {
    int total_length = 0;
    printf("Control Section | Symbol Name | Address | Length\n");
    printf("-------------------------------------------------\n");
    for (int i = 0; i < estab_size; i++) {
        if (estab[i].control_section == 1) { 
            printf("%-16s| %-12s| %06X | %06X\n", 
                   estab[i].name, " ", estab[i].addr, estab[i].length);
            total_length += estab[i].length;
        } else {
            printf("%-16s| %-12s| %06X |\n", 
                   " ", estab[i].name, estab[i].addr);
        }
    }
    printf("-------------------------------------------------\n");
    printf("                                  Total Length %06X\n", total_length);
}

// 기능: loader 명령 처리
// 설명: 인자로 받은 obj 파일들을 loader_pass1/2로 로드하고 reg_PC, dump 주소 초기화
void command_load(char* args) {
    char* filenames[10];
    int file_count = 0;
    char temp_args[256];

    strncpy(temp_args, args, 256);
    char* token = strtok(temp_args, " \t");
    while (token != NULL && file_count < 10) {
        filenames[file_count] = (char*)malloc(strlen(token) + 1);
        strcpy(filenames[file_count], token);
        file_count++;
        token = strtok(NULL, " \t");
    }

    if (file_count == 0) {
        printf("Usage: loader filename1 [filename2 ...]\n");
        return;
    }

    // 메모리 초기화 후 PROGADDR부터 로드
    memset(virtual_memory, 0, MEMORY_SIZE); 
    memset(status_map, 0, sizeof(status_map)); // 특정 주소가 '사용된' 상태인지 표시하기 위한 보조배열(0이면 미사용, 1이면 사용된 상태)
    ENTRY_POINT = PROGADDR; 

    printf("\n--- Start Loader Pass 1 ---\n");
    loader_pass1(filenames, file_count);
    printf("--- Start Loader Pass 2 ---\n");
    loader_pass2(filenames, file_count);

    printf("\n[ok] Loading successful. Program loaded from %04X to %04X.\n", 
           PROGADDR, PROGADDR + TOTAL_PROG_LENGTH - 1);

    print_load_map();

    // 로드 완료 후 PC와 dump 시작 주소 설정
    reg_PC = ENTRY_POINT; 
    current_dump_address = PROGADDR; 

    for (int i = 0; i < file_count; i++) 
        free(filenames[i]);
}

// 기능: dump 명령 처리
// 설명: 메모리의 특정 범위를 HEX + ASCII 형태로 출력
void command_dump(char* args) {
    int start_addr = current_dump_address;
    int end_addr = current_dump_address + 0x9F;  // 기본 160바이트 (0xA0 - 1)
    char temp_args[256];

    strncpy(temp_args, args, 256);
    char* token = strtok(temp_args, " \t,"); 

    // 시작 주소/끝 주소가 인자로 들어온 경우 파싱
    if (token != NULL) {
        if (sscanf(token, "%x", &start_addr) != 1) {
            printf("Error: Invalid start address.\n");
            return;
        }
        end_addr = start_addr + 0x9F;
        token = strtok(NULL, " \t,"); 
        if (token != NULL) {
            if (sscanf(token, "%x", &end_addr) != 1) {
                printf("Error: Invalid end address.\n");
                return;
            }
        }
    }

    // 주소 범위 정리
    if (start_addr > end_addr) {
        int temp = start_addr; 
        start_addr = end_addr; 
        end_addr = temp;
    }
    if (start_addr < 0) start_addr = 0;
    if (end_addr >= MEMORY_SIZE) end_addr = MEMORY_SIZE - 1;

    // 16바이트 align
    start_addr = start_addr & 0xFFFFFFF0;

    printf("Memory Dump from %06X to %06X:\n", start_addr, end_addr);

    int current_row_start = start_addr;
    while (current_row_start <= end_addr) {
        printf("%06X  ", current_row_start);

        // HEX 부분
        
        for (int i = 0; i < 16; i++) {
            int addr = current_row_start + i;

            /*
            if (addr <= end_addr) {
                printf("%02X ", virtual_memory[addr]);
            } else {
                printf("   "); 
            }
                */
            if (status_map[addr] == 0) { // 사용되지 않은 상태일 경우 '..'으로 표시. 사용되지 않은 00은 '..'으로 표시하고, 사용된 00은 00으로 표시하기 위함
                if ((i+1) % 4 == 0) { // 4바이트씩 띄어쓰기 위해
                    printf(".. ");
                } else {
                    printf("..");
                } 
            } else {
                if ((i+1) % 4 == 0) {
                    printf("%02X ", virtual_memory[addr]);
                } else {
                    printf("%02X", virtual_memory[addr]);
                }
            }
        }
        
        


        printf(" ");

        // ASCII 부분
        for (int i = 0; i < 16; i++) {
            int addr = current_row_start + i;
            if (addr <= end_addr) {
                unsigned char byte = virtual_memory[addr];
                if (byte >= 0x20 && byte <= 0x7E) 
                    printf("%c", byte);
                else 
                    printf(".");
            } else {
                printf(" "); 
            }
        }
        printf("\n");
        current_row_start += 16;
    }

    current_dump_address = end_addr + 1;
    if (current_dump_address >= MEMORY_SIZE) 
        current_dump_address = 0; 
}

// 기능: bp 명령 처리
// 설명: bp          → 설정된 bp 목록 출력
//       bp clear   → 모든 bp 삭제
//       bp addr    → 해당 주소에 bp 추가
void command_bp(char* args) {
    if (args == NULL || strlen(args) == 0) { 
        printf("breakpoint\n");
        printf("----------\n");
        for (int i = 0; i < bp_count; i++) 
            printf("%X\n", breakpoints[i]);
    } 
    else if (strcmp(args, "clear") == 0) { 
        bp_count = 0;
        printf("[ok] clear all breakpoints\n");
    } 
    else { 
        int addr;
        if (sscanf(args, "%x", &addr) == 1) { 
            breakpoints[bp_count++] = addr;
            printf("[ok] create breakpoint %X\n", addr);
        } else {
            printf("Usage: bp [address] | bp clear | bp\n");
        }
    }
}

// 기능: 시뮬레이터 레지스터 초기화
// 설명: A/X/L/B/S/T, SW, F를 0으로 설정 (PC는 로더가 세팅한 값을 유지)
void reset_simulator_regs() {
    reg_A = 0LL; 
    reg_X = 0LL; 
    reg_L = 0LL; 
    reg_B = 0LL; 
    reg_S = 0LL; 
    reg_T = 0LL; 
    reg_SW = 0; 
    reg_F = 0.0;
    // Note: PC is preserved (set by loader)
}

// 기능: 레지스터 상태 출력
// 설명: 레지스터 값을 16진수로 포맷팅하여 화면에 출력
void print_regs() {
    printf("A : %012llX X : %08llX\n", reg_A, reg_X);
    printf("L : %012llX PC: %012llX\n", reg_L, reg_PC);
    printf("B : %012llX S : %012llX\n", reg_B, reg_S);
    printf("T : %012llX\n", reg_T);
}

// 기능: opcode 상위 6비트로 OPTAB 검색
// 설명: 시뮬레이터에서 메모리의 opcode 값을 통해 포맷 정보 얻을 때 사용
int search_optab(int opcode) {
    for (int i = 0; i < optab_size; i++) {
        if (optab[i].code == opcode)
            return i;
    }
    return -1;
}

// 외부에서 링크할 수 있게 한 번 더 선언(필요 시)
extern int search_optab(int opcode);

// 기능: run 명령 처리 (시뮬레이션 메인 루프)
// 설명: PC에서부터 명령어를 읽고 decode → execute 반복, bp나 범위 밖이면 종료
void command_run() {
    // 첫 run일 때만 ENTRY_POINT로 PC 설정하려면 아래 주석 사용 가능
    // if (reg_PC == 0) reg_PC = ENTRY_POINT;

    while (1) {

        // 실행 주소가 로드된 프로그램 범위를 벗어나면 종료
        if (reg_PC < PROGADDR || reg_PC >= PROGADDR + TOTAL_PROG_LENGTH) {
            print_regs();
            printf("End program\n");
            return;
        }

        // ----- Breakpoint 검사 -----
        int resume_current_instruction = paused_at_breakpoint;
        paused_at_breakpoint = FALSE;

        if (!resume_current_instruction) {
            for (int i = 0; i < bp_count; i++) {
                if (reg_PC == breakpoints[i]) {
                    print_regs();
                    printf("Stop at checkpoint[%04llX]\n", reg_PC);

                    paused_at_breakpoint = TRUE;
                    return;
                }
            }
        }

        // ----- Breakpoint 검사 -----
        /*
        for (int i = 0; i < bp_count; i++) {
            if (reg_PC == breakpoints[i]) {

                print_regs();
                printf("Stop at checkpoint[%04llX]\n", reg_PC);

                // 다음 run에서 BP 이후 명령부터 실행되도록 PC를 미리 증가
                int opcode = virtual_memory[reg_PC] & 0xFC;
                int idx = search_optab(opcode);

                int format = 3;  // default format
                if (idx >= 0) 
                    format = optab[idx].format;

                if (format == 1) reg_PC += 1;
                else if (format == 2) reg_PC += 2;
                else if (format == 3) reg_PC += 3;
                else if (format == 4) reg_PC += 4;

                return;
            }
        }
        */
        // --- 명령어 Fetch ---
        unsigned char op = get_byte(reg_PC);
        int ni = op & 0x03;         // n,i 비트 조합
        int opcode_val = op & 0xFC; // 상위 6비트 opcode
        int format = 3;

        // Check for Format 2 based on optab is better, but manual check for now
        // Format 2 Opcodes (명령어 code로 직접 판별)
        if (opcode_val == 0xB4 || opcode_val == 0xA0 || opcode_val == 0xB8 || 
            opcode_val == 0x90 || opcode_val == 0x94 || opcode_val == 0x98 || 
            opcode_val == 0x9C || opcode_val == 0xA4 || opcode_val == 0xA8 || 
            opcode_val == 0xB0) 
            format = 2; 
        // Format 1 Opcodes
        else if (opcode_val == 0xC0 || opcode_val == 0xC4 || opcode_val == 0xC8) 
            format = 1;

        
        int step = 3;
        long long target_addr = 0;   // 유효 주소
        long long operand_val = 0;   // 피연산 값

        // --- Decode ---
        unsigned char b2 = 0, b3 = 0, b4 = 0;

        if (format == 3) {
            b2 = get_byte(reg_PC + 1);
            b3 = get_byte(reg_PC + 2);
            int x = (b2 & 0x80) >> 7;
            int b = (b2 & 0x40) >> 6;
            int p = (b2 & 0x20) >> 5;
            int e = (b2 & 0x10) >> 4;
            int disp = ((b2 & 0x0F) << 8) | b3;

            if (e) { // Format 4
                step = 4;
                b4 = get_byte(reg_PC + 3);
                disp = (disp << 8) | b4; 
                target_addr = PROGADDR + disp;  // 절대 주소 (simple 버전)
            } else { // Format 3
                step = 3;
                // 12bit disp 부호 확장
                if (disp & 0x800) 
                    disp |= 0xFFFFF000; 
                if (p) 
                    target_addr = (reg_PC + 3) + disp;  // PC-relative
                else if (b) 
                    target_addr = reg_B + disp;        // Base-relative
                else 
                    target_addr = disp;                // direct
            }

            if (x) 
                target_addr += reg_X;  // indexed addressing

            // ni 값에 따라 피연산자 읽는 방식 다름
            if (ni == 1) 
                operand_val = target_addr;      // simple address
            else if (ni == 2) {                 // indirect
                int ptr = get_word(target_addr);
                operand_val = get_word(ptr); 
                target_addr = ptr; 
            } 
            else 
                operand_val = get_word(target_addr);   // 메모리에서 값 읽기
        } 
        else if (format == 2) {
            step = 2;
            b2 = get_byte(reg_PC + 1);
        } 
        else if (format == 1) {
            step = 1;
        }

        long long next_PC = reg_PC + step; // 기본적으로 다음 명령어 PC

        // --- Execute: opcode_val에 따른 동작 ---
        switch (opcode_val) {
            // Load 계열
            case 0x00: reg_A = operand_val; break; // LDA
            case 0x08: reg_L = operand_val; break; // LDL 
            case 0x68: reg_B = operand_val; break; // LDB
            case 0x6C: reg_S = operand_val; break; // LDS 
            case 0x74: reg_T = operand_val; break; // LDT
            case 0x70: reg_F = (double)operand_val; break; // LDF 
            case 0x04: reg_X = operand_val; break; // LDX
            
            // 메모리에서 byte만 읽는 LDCH
            case 0x50: { 
                int byte_val = get_byte(target_addr); 
                reg_A = (reg_A & 0xFFFF00) | byte_val; 
            } break; // LDCH
            
            // Store 계열
            case 0x0C: set_word(target_addr, (int)reg_A); break; // STA
            case 0x10: set_word(target_addr, (int)reg_X); break; // STX
            case 0x14: set_word(target_addr, (int)reg_L); break; // STL
            case 0x78: set_word(target_addr, (int)reg_B); break; // STB 
            case 0x7C: set_word(target_addr, (int)reg_S); break; // STS 
            case 0x84: set_word(target_addr, (int)reg_T); break; // STT 
            case 0x80: set_word(target_addr, (int)reg_F); break; // STF 
            case 0x54: virtual_memory[target_addr] = (unsigned char)(reg_A & 0xFF); break; // STCH 
            
            // 산술 연산
            case 0x18: reg_A += operand_val; break; // ADD 
            case 0x1C: reg_A -= operand_val; break; // SUB 
            case 0x20: reg_A *= operand_val; break; // MUL 
            case 0x24: reg_A /= operand_val; break; // DIV 
            
            // 서브루틴 호출/복귀
            case 0x48: 
                reg_L = next_PC;      // 복귀 주소 저장
                next_PC = target_addr; 
                break; // JSUB
            case 0x4C: // RSUB
                next_PC = reg_L; 
                break;
                
            // 논리 연산
            case 0x40: reg_A &= operand_val; break; // AND
            case 0x44: reg_A |= operand_val; break; // OR

            // 루프/카운트 관련
            case 0x2C: // TIX
                reg_X += 1;
                if (reg_X == operand_val) reg_SW = 0; 
                else if (reg_X < operand_val) reg_SW = -1; 
                else reg_SW = 1; 
                break;
            
            // 분기 계열
            case 0x30: if (reg_SW == 0)  next_PC = target_addr; break; // JEQ
            case 0x34: if (reg_SW == 1)  next_PC = target_addr; break; // JGT
            case 0x38: if (reg_SW == -1) next_PC = target_addr; break; // JLT
            case 0x3C: next_PC = target_addr; break;                  // J (무조건 점프)
            
            // COMP: A와 operand 비교 후 reg_SW 설정
            case 0x28: 
                if (reg_A == operand_val) reg_SW = 0; 
                else if (reg_A < operand_val) reg_SW = -1; 
                else reg_SW = 1; 
                break;
            
            // CLEAR: 레지스터 1개를 0으로
            case 0xB4: {
                int r1 = (b2 & 0xF0) >> 4;
                if      (r1 == 0) reg_A = 0; 
                else if (r1 == 1) reg_X = 0; 
                else if (r1 == 2) reg_L = 0; 
                else if (r1 == 3) reg_B = 0;
                else if (r1 == 4) reg_S = 0; 
                else if (r1 == 5) reg_T = 0;
            } break;

            // TIXR: X를 증가시키고 특정 레지스터와 비교
            case 0xB8: {
                int r1 = (b2 & 0xF0) >> 4;
                reg_X += 1; 
                long long compare_val = 0;
                if      (r1 == 0) compare_val = reg_A; 
                else if (r1 == 1) compare_val = reg_X;
                else if (r1 == 2) compare_val = reg_L; 
                else if (r1 == 3) compare_val = reg_B;
                else if (r1 == 4) compare_val = reg_S; 
                else if (r1 == 5) compare_val = reg_T;
                    
                if (reg_X == compare_val) reg_SW = 0; 
                else if (reg_X < compare_val) reg_SW = -1; 
                else reg_SW = 1;
            } break;

            // COMPR: 두 레지스터끼리 비교
            case 0xA0: {
                int r1 = (b2 & 0xF0) >> 4;
                int r2 = (b2 & 0x0F);
                long long val1 = 0, val2 = 0;

                if      (r1 == 0) val1 = reg_A; 
                else if (r1 == 1) val1 = reg_X; 
                else if (r1 == 2) val1 = reg_L; 
                else if (r1 == 3) val1 = reg_B; 
                else if (r1 == 4) val1 = reg_S; 
                else if (r1 == 5) val1 = reg_T;

                if      (r2 == 0) val2 = reg_A; 
                else if (r2 == 1) val2 = reg_X;
                else if (r2 == 2) val2 = reg_L; 
                else if (r2 == 3) val2 = reg_B; 
                else if (r2 == 4) val2 = reg_S; 
                else if (r2 == 5) val2 = reg_T;
                    
                if (val1 == val2) reg_SW = 0; 
                else if (val1 < val2) reg_SW = -1; 
                else reg_SW = 1;
            } break;

            // 장치 관련 명령어는 여기서는 구현 X (TD/RD/WD)
            case 0xE0: break; // TD 
            case 0xD8: break; // RD 
            case 0xDC: break; // WD 
            
            default:
                printf("Error: Unknown Opcode %02X at PC=%08llX\n", opcode_val, reg_PC);
                goto END_SIMULATION;
        }

        // 다음 명령어로 PC 이동
        reg_PC = next_PC;
    }

END_SIMULATION:
    print_regs();
    printf("End program.\n"); // 마침표 포함하여 출력
    return;
}

// 기능: help 명령 처리
// 설명: 지원하는 명령어 목록 출력
void display_help() {
    printf("h[elp]\n");
    printf("q[uit]\n");
    printf("du[mp] [start [end]]\n"); 
    printf("reset\n");
    printf("opclist\n");
    printf("assemble\n");
    printf("progaddr address\n");
    printf("loader filename1 [filename2 ...]\n");
    printf("bp [address] | bp clear\n");
    printf("run\n");
}

// 기능: progaddr 명령 처리
// 설명: 프로그래머가 지정한 프로그램 적재 시작 주소(PROGADDR)를 설정
void command_progaddr(char* args) {
    int addr;
    if (sscanf(args, "%x", &addr) == 1) {
        PROGADDR = addr;
        current_dump_address = PROGADDR;  // dump 기본 시작 주소
        printf("Program address set to 0x%X.\n", PROGADDR);
        // ★ reg_PC, ENTRY_POINT 절대 건드리지 않음 (loader에서 설정)
    } else {
        printf("Usage: progaddr address (e.g., progaddr 4000)\n");
    }
}

// 기능: 메인 명령 루프
// 설명: sicsim> 프롬프트에서 사용자 입력을 받아 각 command_* 함수 호출
int main_loop() {
    char line[100];
    char command[100];
    char args[100] = {0};

    // 시뮬레이터 시작 시 기본 OPTAB 로드
    initialize_optab("opcode.txt"); 

    while (1) {
        printf("sicsim> ");
        if (fgets(line, sizeof(line), stdin) == NULL) 
            break;

        rtrim(line);

        char temp_line[100];
        strcpy(temp_line, line);
        char* token = strtok(temp_line, " \t\n\r");
        if (token == NULL) 
            continue;

        strcpy(command, token);

        // command 뒤의 나머지 부분을 args로 사용
        char* arg_start = line + (token - temp_line) + strlen(token);
        while (isspace((unsigned char)*arg_start)) arg_start++;
        strcpy(args, arg_start);

        if      (strcmp(command, "h") == 0 || strcmp(command, "help") == 0) 
            display_help();
        else if (strcmp(command, "q") == 0 || strcmp(command, "quit") == 0) 
            break;
        else if (strcmp(command, "du") == 0 || strcmp(command, "dump") == 0) 
            command_dump(args); 
        else if (strcmp(command, "assemble") == 0) 
            command_assemble();
        else if (strcmp(command, "progaddr") == 0) 
            command_progaddr(args);
        else if (strcmp(command, "loader") == 0) 
            command_load(args);
        else if (strcmp(command, "bp") == 0) 
            command_bp(args);
        else if (strcmp(command, "run") == 0) 
            command_run();
        else 
            printf("Unknown command or missing arguments. Type 'help' for available commands.\n");
    }
    return 0;
}

// 기능: 프로그램 진입점
// 설명: main_loop를 호출하여 sicsim 인터프리터 시작
int main() {
    return main_loop();
}