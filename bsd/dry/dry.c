/****************** "DHRYSTONE" Benchmark Program ***************************/
#define Version "C, Version 2.2"

/* Standard headers for 2.11BSD, NetBSD 10, and macOS */
#include <stdio.h>
#include <stdlib.h>  /* for malloc, exit, atoi */
#include <string.h>  /* for strcpy, strcmp */
#include <sys/types.h>
#include <sys/times.h>

/* Define HZ based on system - use times() return value */
#ifndef HZ
#include <unistd.h>
#ifdef _SC_CLK_TCK
#define HZ sysconf(_SC_CLK_TCK)
#else
#define HZ 60  /* Default for 2.11BSD PDP-11 */
#endif
#endif

/* Use times(2) for timing */
#define TIMES
#define CLOCK_TYPE "times()"

/* Type definitions */
#ifdef NOENUM
#define Ident_1 0
#define Ident_2 1
#define Ident_3 2
#define Ident_4 3
#define Ident_5 4
typedef int Enumeration;
#else
typedef enum {Ident_1, Ident_2, Ident_3, Ident_4, Ident_5} Enumeration;
#endif

#define Null 0
#define true 1
#define false 0

typedef int One_Thirty;
typedef int One_Fifty;
typedef char Capital_Letter;
typedef int Boolean;
typedef char Str_30[31];

typedef struct record {
    struct record *Ptr_Comp;
    Enumeration Discr;
    union {
        struct {
            Enumeration Enum_Comp;
            int Int_Comp;
            char Str_Comp[31];
        } var_1;
        struct {
            Enumeration E_Comp_2;
            char Str_2_Comp[31];
        } var_2;
        struct {
            char Ch_1_Comp;
            char Ch_2_Comp;
        } var_3;
    } variant;
} Rec_Type, *Rec_Pointer;

#ifndef REG
#define REG
#else
#define REG register
#endif

/* Function prototypes */
void Proc_1(REG Rec_Pointer Ptr_Val_Par);
void Proc_2(One_Fifty *Int_Par_Ref);
void Proc_3(Rec_Pointer *Ptr_Ref_Par);
void Proc_4(void);
void Proc_5(void);
void Proc_6(Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par);
void Proc_7(One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref);
void Proc_8(int Arr_1_Par_Ref[50], int Arr_2_Par_Ref[50][50], int Int_1_Par_Val, int Int_2_Par_Val);
Enumeration Func_1(Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val);
Boolean Func_2(Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref);
Boolean Func_3(Enumeration Enum_Par_Val);

#ifdef NOSTRUCTASSIGN
#define structassign(d, s) memcpy(&(d), &(s), sizeof(d))
#else
#define structassign(d, s) d = s
#endif

/* Global Variables */
Rec_Pointer Ptr_Glob, Next_Ptr_Glob;
int Int_Glob;
Boolean Bool_Glob;
char Ch_1_Glob, Ch_2_Glob;
int Arr_1_Glob[50];
int Arr_2_Glob[50][50];

#ifndef REG
Boolean Reg = false;
#else
Boolean Reg = true;
#endif

Boolean Done;
long Begin_Time, End_Time, User_Time;
float Microseconds, Dhrystones_Per_Second;

#define Mic_secs_Per_Second 1000000.0
#define NUMBER_OF_RUNS 500000 /* Suitable for PDP-11/83 */
#define Too_Small_Time (2*HZ)

int main(int argc, char *argv[])
{
    One_Fifty Int_1_Loc;
    REG One_Fifty Int_2_Loc;
    One_Fifty Int_3_Loc;
    REG char Ch_Index;
    Enumeration Enum_Loc;
    Str_30 Str_1_Loc;
    Str_30 Str_2_Loc;
    REG int Run_Index;
    REG int Number_Of_Runs;

    /* Arguments */
    if (argc > 2) {
        printf("Usage: %s [number of loops]\n", argv[0]);
        exit(1);
    }
    if (argc == 2) {
        Number_Of_Runs = atoi(argv[1]);
    } else {
        Number_Of_Runs = NUMBER_OF_RUNS;
    }
    if (Number_Of_Runs <= 0) {
        Number_Of_Runs = NUMBER_OF_RUNS;
    }

    /* Initializations */
    Next_Ptr_Glob = (Rec_Pointer)malloc(sizeof(Rec_Type));
    Ptr_Glob = (Rec_Pointer)malloc(sizeof(Rec_Type));

    Ptr_Glob->Ptr_Comp = Next_Ptr_Glob;
    Ptr_Glob->Discr = Ident_1;
    Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    Ptr_Glob->variant.var_1.Int_Comp = 40;
    strcpy(Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING");
    strcpy(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING");

    Arr_2_Glob[8][7] = 10;

    printf("\n");
    printf("Dhrystone Benchmark, Version %s\n", Version);
    printf("Program compiled %s 'register' attribute\n", Reg ? "with" : "without");
    printf("Using %s, HZ=%ld\n", CLOCK_TYPE, (long)HZ);
    printf("\n");

    Done = false;
    while (!Done) {
        printf("Trying %d runs through Dhrystone:\n", Number_Of_Runs);

        /* Start timer */
        struct tms time_info;
        times(&time_info);
        Begin_Time = (long)time_info.tms_utime;

        for (Run_Index = 1; Run_Index <= Number_Of_Runs; ++Run_Index) {
            Proc_5();
            Proc_4();
            Int_1_Loc = 2;
            Int_2_Loc = 3;
            strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
            Enum_Loc = Ident_2;
            Bool_Glob = !Func_2(Str_1_Loc, Str_2_Loc);
            while (Int_1_Loc < Int_2_Loc) {
                Int_3_Loc = 5 * Int_1_Loc - Int_2_Loc;
                Proc_7(Int_1_Loc, Int_2_Loc, &Int_3_Loc);
                Int_1_Loc += 1;
            }
            Proc_8(Arr_1_Glob, Arr_2_Glob, Int_1_Loc, Int_3_Loc);
            Proc_1(Ptr_Glob);
            for (Ch_Index = 'A'; Ch_Index <= Ch_2_Glob; ++Ch_Index) {
                if (Enum_Loc == Func_1(Ch_Index, 'C')) {
                    Proc_6(Ident_1, &Enum_Loc);
                    strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 3'RD STRING");
                    Int_2_Loc = Run_Index;
                    Int_Glob = Run_Index;
                }
            }
            Int_2_Loc = Int_2_Loc * Int_1_Loc;
            Int_1_Loc = Int_2_Loc / Int_3_Loc;
            Int_2_Loc = 7 * (Int_2_Loc - Int_3_Loc) - Int_1_Loc;
            Proc_2(&Int_1_Loc);
        }

        /* Stop timer */
        times(&time_info);
        End_Time = (long)time_info.tms_utime;

        User_Time = End_Time - Begin_Time;

        if (User_Time < Too_Small_Time) {
            printf("Measured time too small to obtain meaningful results\n");
            Number_Of_Runs *= 10;
            printf("\n");
        } else {
            Done = true;
        }
    }

    fprintf(stderr, "Final values of the variables used in the benchmark:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Int_Glob:            %d\n", Int_Glob);
    fprintf(stderr, "        should be:   %d\n", 5);
    fprintf(stderr, "Bool_Glob:           %d\n", Bool_Glob);
    fprintf(stderr, "        should be:   %d\n", 1);
    fprintf(stderr, "Ch_1_Glob:           %c\n", Ch_1_Glob);
    fprintf(stderr, "        should be:   %c\n", 'A');
    fprintf(stderr, "Ch_2_Glob:           %c\n", Ch_2_Glob);
    fprintf(stderr, "        should be:   %c\n", 'B');
    fprintf(stderr, "Arr_1_Glob[8]:       %d\n", Arr_1_Glob[8]);
    fprintf(stderr, "        should be:   %d\n", 7);
    fprintf(stderr, "Arr_2_Glob[8][7]:    %d\n", Arr_2_Glob[8][7]);
    fprintf(stderr, "        should be:   Number_Of_Runs + 10\n");
    fprintf(stderr, "Ptr_Glob->\n");
    fprintf(stderr, "  Ptr_Comp:          %ld\n", (long)Ptr_Glob->Ptr_Comp);
    fprintf(stderr, "        should be:   (implementation-dependent)\n");
    fprintf(stderr, "  Discr:             %d\n", Ptr_Glob->Discr);
    fprintf(stderr, "        should be:   %d\n", 0);
    fprintf(stderr, "  Enum_Comp:         %d\n", Ptr_Glob->variant.var_1.Enum_Comp);
    fprintf(stderr, "        should be:   %d\n", 2);
    fprintf(stderr, "  Int_Comp:          %d\n", Ptr_Glob->variant.var_1.Int_Comp);
    fprintf(stderr, "        should be:   %d\n", 17);
    fprintf(stderr, "  Str_Comp:          %s\n", Ptr_Glob->variant.var_1.Str_Comp);
    fprintf(stderr, "        should be:   DHRYSTONE PROGRAM, SOME STRING\n");
    fprintf(stderr, "Next_Ptr_Glob->\n");
    fprintf(stderr, "  Ptr_Comp:          %ld\n", (long)Next_Ptr_Glob->Ptr_Comp);
    fprintf(stderr, "        should be:   (implementation-dependent), same as above\n");
    fprintf(stderr, "  Discr:             %d\n", Next_Ptr_Glob->Discr);
    fprintf(stderr, "        should be:   %d\n", 0);
    fprintf(stderr, "  Enum_Comp:         %d\n", Next_Ptr_Glob->variant.var_1.Enum_Comp);
    fprintf(stderr, "        should be:   %d\n", 1);
    fprintf(stderr, "  Int_Comp:          %d\n", Next_Ptr_Glob->variant.var_1.Int_Comp);
    fprintf(stderr, "        should be:   %d\n", 18);
    fprintf(stderr, "  Str_Comp:          %s\n", Next_Ptr_Glob->variant.var_1.Str_Comp);
    fprintf(stderr, "        should be:   DHRYSTONE PROGRAM, SOME STRING\n");
    fprintf(stderr, "Int_1_Loc:           %d\n", Int_1_Loc);
    fprintf(stderr, "        should be:   %d\n", 5);
    fprintf(stderr, "Int_2_Loc:           %d\n", Int_2_Loc);
    fprintf(stderr, "        should be:   %d\n", 13);
    fprintf(stderr, "Int_3_Loc:           %d\n", Int_3_Loc);
    fprintf(stderr, "        should be:   %d\n", 7);
    fprintf(stderr, "Enum_Loc:            %d\n", Enum_Loc);
    fprintf(stderr, "        should be:   %d\n", 1);
    fprintf(stderr, "Str_1_Loc:           %s\n", Str_1_Loc);
    fprintf(stderr, "        should be:   DHRYSTONE PROGRAM, 1'ST STRING\n");
    fprintf(stderr, "Str_2_Loc:           %s\n", Str_2_Loc);
    fprintf(stderr, "        should be:   DHRYSTONE PROGRAM, 2'ND STRING\n");
    fprintf(stderr, "\n");

    Microseconds = (float)User_Time * Mic_secs_Per_Second / ((float)HZ * (float)Number_Of_Runs);
    Dhrystones_Per_Second = ((float)HZ * (float)Number_Of_Runs) / (float)User_Time;

    printf("Microseconds for one run through Dhrystone: %10.1f \n", Microseconds);
    printf("Dhrystones per Second:                      %10.0f \n", Dhrystones_Per_Second);
    printf("\n");

    return 0;
}

void Proc_1(REG Rec_Pointer Ptr_Val_Par)
{
    REG Rec_Pointer Next_Record = Ptr_Val_Par->Ptr_Comp;
    structassign(*Ptr_Val_Par->Ptr_Comp, *Ptr_Glob);
    Ptr_Val_Par->variant.var_1.Int_Comp = 5;
    Next_Record->variant.var_1.Int_Comp = Ptr_Val_Par->variant.var_1.Int_Comp;
    Next_Record->Ptr_Comp = Ptr_Val_Par->Ptr_Comp;
    Proc_3(&Next_Record->Ptr_Comp);
    if (Next_Record->Discr == Ident_1) {
        Next_Record->variant.var_1.Int_Comp = 6;
        Proc_6(Ptr_Val_Par->variant.var_1.Enum_Comp, &Next_Record->variant.var_1.Enum_Comp);
        Next_Record->Ptr_Comp = Ptr_Glob->Ptr_Comp;
        Proc_7(Next_Record->variant.var_1.Int_Comp, 10, &Next_Record->variant.var_1.Int_Comp);
    } else {
        structassign(*Ptr_Val_Par, *Ptr_Val_Par->Ptr_Comp);
    }
}

void Proc_2(One_Fifty *Int_Par_Ref)
{
    One_Fifty Int_Loc;
    Enumeration Enum_Loc;
    Int_Loc = *Int_Par_Ref + 10;
    do {
        if (Ch_1_Glob == 'A') {
            Int_Loc -= 1;
            *Int_Par_Ref = Int_Loc - Int_Glob;
            Enum_Loc = Ident_1;
        }
    } while (Enum_Loc != Ident_1);
}

void Proc_3(Rec_Pointer *Ptr_Ref_Par)
{
    if (Ptr_Glob != Null)
        *Ptr_Ref_Par = Ptr_Glob->Ptr_Comp;
    Proc_7(10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp);
}

void Proc_4(void)
{
    Boolean Bool_Loc;
    Bool_Loc = Ch_1_Glob == 'A';
    Bool_Glob = Bool_Loc | Bool_Glob;
    Ch_2_Glob = 'B';
}

void Proc_5(void)
{
    Ch_1_Glob = 'A';
    Bool_Glob = false;
}

#ifdef NOSTRUCTASSIGN
void memcpy(char *d, char *s, int l)
{
    while (l--) *d++ = *s++;
}
#endif

void Proc_6(Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par)
{
    *Enum_Ref_Par = Enum_Val_Par;
    if (!Func_3(Enum_Val_Par))
        *Enum_Ref_Par = Ident_4;
    switch (Enum_Val_Par) {
        case Ident_1: *Enum_Ref_Par = Ident_1; break;
        case Ident_2: *Enum_Ref_Par = (Int_Glob > 100) ? Ident_1 : Ident_4; break;
        case Ident_3: *Enum_Ref_Par = Ident_2; break;
        case Ident_4: break;
        case Ident_5: *Enum_Ref_Par = Ident_3; break;
    }
}

void Proc_7(One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref)
{
    One_Fifty Int_Loc;
    Int_Loc = Int_1_Par_Val + 2;
    *Int_Par_Ref = Int_2_Par_Val + Int_Loc;
}

void Proc_8(int Arr_1_Par_Ref[50], int Arr_2_Par_Ref[50][50], int Int_1_Par_Val, int Int_2_Par_Val)
{
    REG One_Fifty Int_Index;
    REG One_Fifty Int_Loc;
    Int_Loc = Int_1_Par_Val + 5;
    Arr_1_Par_Ref[Int_Loc] = Int_2_Par_Val;
    Arr_1_Par_Ref[Int_Loc+1] = Arr_1_Par_Ref[Int_Loc];
    Arr_1_Par_Ref[Int_Loc+30] = Int_Loc;
    for (Int_Index = Int_Loc; Int_Index <= Int_Loc+1; ++Int_Index)
        Arr_2_Par_Ref[Int_Loc][Int_Index] = Int_Loc;
    Arr_2_Par_Ref[Int_Loc][Int_Loc-1] += 1;
    Arr_2_Par_Ref[Int_Loc+20][Int_Loc] = Arr_1_Par_Ref[Int_Loc];
    Int_Glob = 5;
}

Enumeration Func_1(Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val)
{
    Capital_Letter Ch_1_Loc;
    Capital_Letter Ch_2_Loc;
    Ch_1_Loc = Ch_1_Par_Val;
    Ch_2_Loc = Ch_1_Loc;
    if (Ch_2_Loc != Ch_2_Par_Val)
        return Ident_1;
    else {
        Ch_1_Glob = Ch_1_Loc;
        return Ident_2;
    }
}

Boolean Func_2(Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref)
{
    REG One_Thirty Int_Loc;
    Capital_Letter Ch_Loc;
    Int_Loc = 2;
    while (Int_Loc <= 2) {
        if (Func_1(Str_1_Par_Ref[Int_Loc], Str_2_Par_Ref[Int_Loc+1]) == Ident_1) {
            Ch_Loc = 'A';
            Int_Loc += 1;
        }
    }
    if (Ch_Loc >= 'W' && Ch_Loc < 'Z')
        Int_Loc = 7;
    if (Ch_Loc == 'R')
        return true;
    else {
        if (strcmp(Str_1_Par_Ref, Str_2_Par_Ref) > 0) {
            Int_Loc += 7;
            Int_Glob = Int_Loc;
            return true;
        } else {
            return false;
        }
    }
}

Boolean Func_3(Enumeration Enum_Par_Val)
{
    Enumeration Enum_Loc;
    Enum_Loc = Enum_Par_Val;
    if (Enum_Loc == Ident_3)
        return true;
    else
        return false;
}