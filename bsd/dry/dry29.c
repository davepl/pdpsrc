/*
 ****************************************************************************
 *
 *                   "DHRYSTONE" Benchmark Program
 *                   -----------------------------
 *
 * Version:      C, Version 2.2 (2.9BSD Edition)
 * File:         dry29.c
 * Date:         July 1, 2025
 * Authors:      Reinhold P. Weicker, CACM Vol 27, No 10, 10/84 pg. 1013.
 *               Rick Richardson, PC Research. Inc.
 *               Dave Cheney, modernization and portability
 *               Dave Plummer, 29BSD and 211BSD port
 * 
 * Description:  Dhrystone 2.2 benchmark specifically for 2.9BSD on PDP-11.
 *               This version uses only integer arithmetic, K&R C style,
 *               and is optimized for 2.9BSD while remaining compilable 
 *               on modern systems for comparison.
 *
 * Compilation:  cc -O -o dry29 dry29.c    (with optimization)
 *               cc -o dry29 dry29.c       (safe for all PDP-11 models)
 *
 * Usage:        ./dry29 [iterations]
 *
 ****************************************************************************
 */

#define Version "C, Version 2.2 (2.9BSD Edition)"

/* Standard headers - only what's available on 2.9BSD */
#include <stdio.h>
#include <sys/types.h>
#include <sys/times.h>

/* Functions malloc, strcpy, strcmp, atol, exit are implicitly declared on 2.9BSD */
/* But for modern compilation, include headers for better optimization */
#ifndef BSD29
#include <stdlib.h>  /* for malloc, exit, atol */
#include <string.h>  /* for strcpy, strcmp */
#endif

/* 2.9BSD PDP-11 system configuration */
#define HZ 60   /* 2.9BSD standard: 60 Hz (1/60 second ticks) */
#define CLOCK_TYPE "times()"
#define Too_Small_Time (3 * HZ)  /* Require at least 3 seconds */

/* Type definitions using #defines instead of enum for maximum compatibility */
#define Ident_1 0
#define Ident_2 1
#define Ident_3 2
#define Ident_4 3
#define Ident_5 4
typedef int Enumeration;

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

/* Simple struct assignment using memcpy for 2.9BSD compatibility */
void
my_memcpy(d, s, l)
char *d, *s;
int l;
{
    while (l--) *d++ = *s++;
}
/* Use my_memcpy for K&R C compatibility, direct assignment for modern compilers */
#ifdef BSD29
#define structassign(d, s) my_memcpy((char*)&(d), (char*)&(s), sizeof(d))
#else
#define structassign(d, s) d = s  /* Direct assignment on modern systems */
#endif

#ifndef REG
#define REG
#else
#define REG register
#endif

/* Global Variables */
Rec_Pointer Ptr_Glob, Next_Ptr_Glob;
int Int_Glob;
Boolean Bool_Glob;
char Ch_1_Glob, Ch_2_Glob;
int Arr_1_Glob[50];        /* Original Dhrystone size */
int Arr_2_Glob[50][50];    /* Original Dhrystone size */

#ifndef REG
Boolean Reg = false;
#else
Boolean Reg = true;
#endif

Boolean Done;
long Begin_Time, End_Time, User_Time;
long Microseconds, Dhrystones_Per_Second;  /* Integer-only arithmetic */
#define NUMBER_OF_RUNS 100

/* Function definitions in proper order for K&R C */

Boolean Func_3(Enum_Par_Val)
Enumeration Enum_Par_Val;
{
    Enumeration Enum_Loc;
    Enum_Loc = Enum_Par_Val;
    if (Enum_Loc == Ident_3)
        return true;
    else
        return false;
}

Enumeration Func_1(Ch_1_Par_Val, Ch_2_Par_Val)
Capital_Letter Ch_1_Par_Val;
Capital_Letter Ch_2_Par_Val;
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

void
Proc_7(Int_1_Par_Val, Int_2_Par_Val, Int_Par_Ref)
One_Fifty Int_1_Par_Val;
One_Fifty Int_2_Par_Val;
One_Fifty *Int_Par_Ref;
{
    One_Fifty Int_Loc;
    Int_Loc = Int_1_Par_Val + 2;
    *Int_Par_Ref = Int_2_Par_Val + Int_Loc;
}

void
Proc_3(Ptr_Ref_Par)
Rec_Pointer *Ptr_Ref_Par;
{
    if (Ptr_Glob != Null)
        *Ptr_Ref_Par = Ptr_Glob->Ptr_Comp;
    Proc_7(10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp);
}

void
Proc_6(Enum_Val_Par, Enum_Ref_Par)
Enumeration Enum_Val_Par;
Enumeration *Enum_Ref_Par;
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

void
Proc_5()
{
    Ch_1_Glob = 'A';
    Bool_Glob = false;
}

void
Proc_4()
{
    Boolean Bool_Loc;
    Bool_Loc = Ch_1_Glob == 'A';
    Bool_Glob = Bool_Loc | Bool_Glob;
    Ch_2_Glob = 'B';
}

Boolean Func_2(Str_1_Par_Ref, Str_2_Par_Ref)
Str_30 Str_1_Par_Ref;
Str_30 Str_2_Par_Ref;
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

void
Proc_8(Arr_1_Par_Ref, Arr_2_Par_Ref, Int_1_Par_Val, Int_2_Par_Val)
int Arr_1_Par_Ref[50];
int Arr_2_Par_Ref[50][50];
int Int_1_Par_Val;
int Int_2_Par_Val;
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

void
Proc_1(Ptr_Val_Par)
REG Rec_Pointer Ptr_Val_Par;
{
    REG Rec_Pointer Next_Record;
    Next_Record = Ptr_Val_Par->Ptr_Comp;
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

void
Proc_2(Int_Par_Ref)
One_Fifty *Int_Par_Ref;
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

int
main(argc, argv)
int argc;
char *argv[];
{
    One_Fifty Int_1_Loc;
    REG One_Fifty Int_2_Loc;
    One_Fifty Int_3_Loc;
    REG char Ch_Index;
    Enumeration Enum_Loc;
    Str_30 Str_1_Loc;
    Str_30 Str_2_Loc;
    REG long Run_Index;
    REG long Number_Of_Runs;
    struct tms time_buffer;
    /* Performance calculation variables - must be at top for K&R C */
    long numerator;
    long denominator;
    long scaled_runs;
    long scaled_num;
    long scaled_denom;

    /* Arguments */
    if (argc > 2) {
        printf("Usage: %s [number of loops]\n", argv[0]);
        exit(1);
    }
    if (argc == 2) {
        Number_Of_Runs = atol(argv[1]);
    } else {
        Number_Of_Runs = NUMBER_OF_RUNS;
    }
    if (Number_Of_Runs <= 0) {
        Number_Of_Runs = NUMBER_OF_RUNS;
    }

    /* Memory allocation with error checking */
    Next_Ptr_Glob = (Rec_Pointer)malloc(sizeof(Rec_Type));
    if (Next_Ptr_Glob == (Rec_Pointer)0) {
        printf("Error: malloc failed for Next_Ptr_Glob\n");
        printf("Not enough memory available (need %d bytes)\n", (int)sizeof(Rec_Type));
        exit(1);
    }
    
    Ptr_Glob = (Rec_Pointer)malloc(sizeof(Rec_Type));
    if (Ptr_Glob == (Rec_Pointer)0) {
        printf("Error: malloc failed for Ptr_Glob\n");
        printf("Not enough memory available (need %d bytes)\n", (int)sizeof(Rec_Type));
        exit(1);
    }

    /* Initialization */
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
    printf("Memory usage: Arr_1_Glob=%d bytes, Arr_2_Glob=%d bytes, Records=%d bytes\n",
           (int)sizeof(Arr_1_Glob), (int)sizeof(Arr_2_Glob), 2 * (int)sizeof(Rec_Type));
    printf("\n");

    Done = false;
    while (!Done) {
        printf("Trying %ld runs through Dhrystone:\n", Number_Of_Runs);

        /* Start timer */
        times(&time_buffer);
        Begin_Time = (long)time_buffer.tms_utime;

        /* Main benchmark loop */
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
        times(&time_buffer);
        End_Time = (long)time_buffer.tms_utime;

        User_Time = End_Time - Begin_Time;

        if (User_Time < Too_Small_Time) {
            printf("Measured time too small to obtain meaningful results\n");
            /* Use simple assignment - multiply by 10 by adding 10 copies */
            {
                long old_runs;
                old_runs = Number_Of_Runs;
                Number_Of_Runs = old_runs + old_runs;  /* *2 */
                Number_Of_Runs = Number_Of_Runs + Number_Of_Runs + Number_Of_Runs + Number_Of_Runs;  /* *2 -> *8 */
                Number_Of_Runs = Number_Of_Runs + old_runs + old_runs;  /* +2 more = *10 */
            }
            printf("\n");
        } else {
            Done = true;
        }
    }

    /* Print final variable values for verification */
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

    /* Integer arithmetic for performance calculations */
    if (User_Time > 0 && Number_Of_Runs > 0) {
        /* Calculate (Number_Of_Runs * HZ) / User_Time by reordering to avoid large intermediate values */
        /* Instead of (Number_Of_Runs * HZ) / User_Time, use Number_Of_Runs / User_Time * HZ */
        if (User_Time > 0) {
            {
                long quotient, remainder, final_result;
                /* First do the division: Number_Of_Runs / User_Time */
                quotient = Number_Of_Runs / User_Time;
                /* Calculate remainder manually: remainder = Number_Of_Runs - (quotient * User_Time) */
                remainder = Number_Of_Runs - (quotient * User_Time);
                
                /* Now multiply quotient by HZ */
                {
                    long temp_result;
                    temp_result = quotient * HZ;
                        
                    /* Add contribution from remainder: (remainder * HZ) / User_Time */
                    if (remainder > 0) {
                        long remainder_contribution;
                        remainder_contribution = (remainder * HZ) / User_Time;
                        final_result = temp_result + remainder_contribution;
                    } else {
                        final_result = temp_result;
                    }
                }
                
                Dhrystones_Per_Second = final_result;
            }
        } else {
            Dhrystones_Per_Second = 0L;
        }
        
        /* Now calculate microseconds using the computed Dhrystones_Per_Second */
        if (Dhrystones_Per_Second > 0) {
            /* Simple calculation: microseconds = 1000000 / Dhrystones_Per_Second */
            /* For values around 23 million, this gives reasonable precision */
            Microseconds = 1000000L / Dhrystones_Per_Second;
            /* Add 1 if there's a significant remainder for better accuracy */
            if ((1000000L * 10L / Dhrystones_Per_Second) % 10L >= 5L) {
                Microseconds += 1L;
            }
        } else {
            Microseconds = 0L;
        }
    } else {
        Microseconds = 0;
        Dhrystones_Per_Second = 0;
    }

    printf("Microseconds for one run through Dhrystone: %10ld \n", Microseconds);
    printf("Dhrystones per Second:                      %10ld \n", Dhrystones_Per_Second);
    printf("\n");

    return 0;
}
