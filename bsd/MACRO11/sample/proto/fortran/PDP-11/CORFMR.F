C =========================================================
C COREFORMER - A TRANSFORMER IN FORTRAN IV
C PAPER TAPE IS ALL YOU NEED
C DAMIEN BOUREILLE, 2026
C MIT LICENSE
C =========================================================
C ENCODER-ONLY TRANSFORMER (1 LAYER, 1 HEAD), 1216 PARAMS
C LEARNS TO REVERSE A SEQUENCE OF 8 DIGITS (0-9)
C TARGETS: IBM 1130 (1965), PDP-11 (1970s), ANY FORTRAN IV
C =========================================================
C
C --- MAIN PROGRAM ---
C
      PROGRAM CORFMR
      INTEGER TOKENS(8), TARGET(8)
      INTEGER I, J, STEP, NSTEP, PRED, BEST
      INTEGER NHIT, NTOT, SEQOK, NTEST
      REAL LREMB, LRATN, LROUT, LOSS, PMAX
      REAL RSEED
      INTEGER INPT(8)
C
C WEIGHTS
      REAL TOKEM(10,16), POSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16), WOUT(16,10)
C GRADIENTS
      REAL DTOKEM(10,16), DPOSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16), DWOUT(16,10)
C FORWARD CACHE
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LOGIT(8,10)
C BACKWARD TEMPS
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
C
      COMMON /WGTS/ TOKEM, POSEM, WQ, WK, WV, WOUT
      COMMON /GRAD/ DTOKEM, DPOSEM, DWQ, DWK, DWV, DWOUT
      COMMON /CASH/ XX, QQ, KK, VV, AA, YY, LOGIT
      COMMON /TEMP/ DY, DA, DQ, DK, DVV, DX
C
C CONSTANTS
      NSTEP = 1500
      LRATN = 0.08
      LREMB = 0.01
      LROUT = 0.0025
      RSEED = 887.0
      NTEST = 20
C
C FORMATS
 9000 FORMAT(' COREFORMER: PAPER TAPE IS ALL YOU NEED')
 9001 FORMAT(' D=16 SEQ=8 VOCAB=10 PARAMS=1216 PER-LAYER LR')
 9002 FORMAT(' ')
 9003 FORMAT(' STEP',I6,'  LOSS=',F8.4,'  ACC=',F6.3)
 9004 FORMAT(' ',8I2,' ->',8I2,2X,A4)
 9005 FORMAT(' SEQUENCE ACCURACY: ',I3,'/',I3)
 9006 FORMAT(' TRAINING...')
C
      WRITE(5, 9000)
      WRITE(5, 9001)
      WRITE(5, 9002)
C
C INIT WEIGHTS
      CALL INIT(RSEED)
C
C --- TRAINING LOOP ---
C
      WRITE(5, 9006)
      NHIT = 0
      NTOT = 0
      DO 100 STEP = 1, NSTEP
          CALL GENSMP(TOKENS, TARGET, RSEED)
          CALL ZEROG
          CALL FORWD(TOKENS)
          CALL BKWRD(TOKENS, TARGET)
          CALL UPDAT(LREMB, LRATN, LROUT)
C
C COUNT CORRECT PREDICTIONS
          DO 110 I = 1, 8
              BEST = 1
              PMAX = LOGIT(I, 1)
              DO 120 J = 2, 10
                  IF (LOGIT(I, J) .LE. PMAX) GOTO 120
                  PMAX = LOGIT(I, J)
                  BEST = J
  120         CONTINUE
              PRED = BEST - 1
              IF (PRED .EQ. TARGET(I)) NHIT = NHIT + 1
              NTOT = NTOT + 1
  110     CONTINUE
C
C PRINT PROGRESS EVERY 100 STEPS
          IF (MOD(STEP, 100) .NE. 0) GOTO 100
          CALL CELOSS(LOGIT, TARGET, 8, 10, LOSS)
          WRITE(5, 9003) STEP, LOSS, FLOAT(NHIT)/FLOAT(NTOT)
          NHIT = 0
          NTOT = 0
  100 CONTINUE
C
C --- FINAL TEST ---
C
      WRITE(5, 9002)
      SEQOK = 0
      DO 200 I = 1, NTEST
          CALL GENSMP(TOKENS, TARGET, RSEED)
          CALL FORWD(TOKENS)
C SAVE INPUT BEFORE OVERWRITING WITH PREDICTIONS
          DO 205 J = 1, 8
              INPT(J) = TOKENS(J)
  205     CONTINUE
C CHECK ALL 8 POSITIONS
          DO 210 J = 1, 8
              BEST = 1
              PMAX = LOGIT(J, 1)
              DO 220 K = 2, 10
                  IF (LOGIT(J, K) .LE. PMAX) GOTO 220
                  PMAX = LOGIT(J, K)
                  BEST = K
  220         CONTINUE
              TOKENS(J) = BEST - 1
  210     CONTINUE
C CHECK IF ENTIRE SEQUENCE CORRECT
          DO 230 J = 1, 8
              IF (TOKENS(J) .NE. TARGET(J)) GOTO 240
  230     CONTINUE
C ALL CORRECT
          SEQOK = SEQOK + 1
          WRITE(5, 9004) (INPT(J), J=1,8),
     $                   (TOKENS(J), J=1,8), ' OK '
          GOTO 200
  240     WRITE(5, 9004) (INPT(J), J=1,8),
     $                   (TOKENS(J), J=1,8), 'FAIL'
  200 CONTINUE
      WRITE(5, 9002)
      WRITE(5, 9005) SEQOK, NTEST
      STOP
      END
C
C =========================================================
C INIT - INITIALIZE WEIGHTS WITH SEEDED RNG
C =========================================================
C
      SUBROUTINE INIT(RSEED)
      REAL TOKEM(10,16), POSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16), WOUT(16,10)
      REAL DTOKEM(10,16), DPOSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16), DWOUT(16,10)
      COMMON /WGTS/ TOKEM, POSEM, WQ, WK, WV, WOUT
      COMMON /GRAD/ DTOKEM, DPOSEM, DWQ, DWK, DWV, DWOUT
      REAL RSEED, RND, SCD, SCV
      INTEGER I, J
C
C XAVIER-ISH SCALING
      SCD = SQRT(2.0 / 32.0)
      SCV = SQRT(2.0 / 26.0)
C
C TOKEN EMBEDDINGS (10 x 16)
      DO 10 I = 1, 10
          DO 11 J = 1, 16
              CALL RNGAUS(RSEED, RND)
              TOKEM(I, J) = RND * SCV
   11     CONTINUE
   10 CONTINUE
C POSITION EMBEDDINGS (8 x 16)
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              CALL RNGAUS(RSEED, RND)
              POSEM(I, J) = RND * SCD
   21     CONTINUE
   20 CONTINUE
C WQ (16 x 16)
      DO 30 I = 1, 16
          DO 31 J = 1, 16
              CALL RNGAUS(RSEED, RND)
              WQ(I, J) = RND * SCD
   31     CONTINUE
   30 CONTINUE
C WK (16 x 16)
      DO 40 I = 1, 16
          DO 41 J = 1, 16
              CALL RNGAUS(RSEED, RND)
              WK(I, J) = RND * SCD
   41     CONTINUE
   40 CONTINUE
C WV (16 x 16)
      DO 50 I = 1, 16
          DO 51 J = 1, 16
              CALL RNGAUS(RSEED, RND)
              WV(I, J) = RND * SCD
   51     CONTINUE
   50 CONTINUE
C WOUT (16 x 10)
      DO 60 I = 1, 16
          DO 61 J = 1, 10
              CALL RNGAUS(RSEED, RND)
              WOUT(I, J) = RND * SCV
   61     CONTINUE
   60 CONTINUE
      RETURN
      END
C
C =========================================================
C FORWD - FORWARD PASS
C =========================================================
C
      SUBROUTINE FORWD(TOKENS)
      INTEGER TOKENS(8)
      REAL TOKEM(10,16), POSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16), WOUT(16,10)
      REAL DTOKEM(10,16), DPOSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16), DWOUT(16,10)
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LOGIT(8,10)
      COMMON /WGTS/ TOKEM, POSEM, WQ, WK, WV, WOUT
      COMMON /CASH/ XX, QQ, KK, VV, AA, YY, LOGIT
      INTEGER I, J, K, TK
      REAL S, SCALE
      REAL SM(8)
C
      SCALE = 1.0 / SQRT(16.0)
C
C --- EMBEDDING: X = TOK_EMB[TOKEN] + POS_EMB ---
      DO 10 I = 1, 8
          TK = TOKENS(I) + 1
          DO 11 J = 1, 16
              XX(I, J) = TOKEM(TK, J) + POSEM(I, J)
   11     CONTINUE
   10 CONTINUE
C
C --- Q, K, V PROJECTIONS: Q[I] = WQ^T * X[I] ---
      DO 20 I = 1, 8
          DO 21 K = 1, 16
              S = 0.0
              DO 22 J = 1, 16
                  S = S + WQ(J, K) * XX(I, J)
   22         CONTINUE
              QQ(I, K) = S
   21     CONTINUE
          DO 23 K = 1, 16
              S = 0.0
              DO 24 J = 1, 16
                  S = S + WK(J, K) * XX(I, J)
   24         CONTINUE
              KK(I, K) = S
   23     CONTINUE
          DO 25 K = 1, 16
              S = 0.0
              DO 26 J = 1, 16
                  S = S + WV(J, K) * XX(I, J)
   26         CONTINUE
              VV(I, K) = S
   25     CONTINUE
   20 CONTINUE
C
C --- ATTENTION SCORES: SC[I,J] = Q[I] . K[J] * SCALE ---
C     STORED IN AA TEMPORARILY (OVERWRITTEN BY SOFTMAX)
      DO 30 I = 1, 8
          DO 31 J = 1, 8
              S = 0.0
              DO 32 K = 1, 16
                  S = S + QQ(I, K) * KK(J, K)
   32         CONTINUE
              AA(I, J) = S * SCALE
   31     CONTINUE
C
C --- SOFTMAX PER ROW ---
C     EXTRACT ROW (FORTRAN IS COLUMN-MAJOR)
          DO 33 J = 1, 8
              SM(J) = AA(I, J)
   33     CONTINUE
          CALL SOFTMX(SM, 8, SM)
          DO 34 J = 1, 8
              AA(I, J) = SM(J)
   34     CONTINUE
   30 CONTINUE
C
C --- WEIGHTED SUM: O[I] = SUM_J A[I,J] * V[J] ---
C --- PLUS RESIDUAL: Y = O + X ---
      DO 40 I = 1, 8
          DO 41 K = 1, 16
              S = 0.0
              DO 42 J = 1, 8
                  S = S + AA(I, J) * VV(J, K)
   42         CONTINUE
              YY(I, K) = S + XX(I, K)
   41     CONTINUE
   40 CONTINUE
C
C --- OUTPUT PROJECTION: LOGIT[I] = WOUT^T * Y[I] ---
      DO 50 I = 1, 8
          DO 51 K = 1, 10
              S = 0.0
              DO 52 J = 1, 16
                  S = S + WOUT(J, K) * YY(I, J)
   52         CONTINUE
              LOGIT(I, K) = S
   51     CONTINUE
   50 CONTINUE
      RETURN
      END
C
C =========================================================
C BKWRD - BACKWARD PASS
C =========================================================
C
      SUBROUTINE BKWRD(TOKENS, TARGET)
      INTEGER TOKENS(8), TARGET(8)
      REAL TOKEM(10,16), POSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16), WOUT(16,10)
      REAL DTOKEM(10,16), DPOSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16), DWOUT(16,10)
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LOGIT(8,10)
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
      COMMON /WGTS/ TOKEM, POSEM, WQ, WK, WV, WOUT
      COMMON /GRAD/ DTOKEM, DPOSEM, DWQ, DWK, DWV, DWOUT
      COMMON /CASH/ XX, QQ, KK, VV, AA, YY, LOGIT
      COMMON /TEMP/ DY, DA, DQ, DK, DVV, DX
      INTEGER I, J, K, TK
      REAL DL(10), SM(10), S, SCALE, DOTAD
C
      SCALE = 1.0 / SQRT(16.0)
C
C --- DL = SOFTMAX(LOGITS) - ONE_HOT(TARGET) ---
      DO 10 I = 1, 8
C     EXTRACT ROW (COLUMN-MAJOR)
          DO 11 J = 1, 10
              DL(J) = LOGIT(I, J)
   11     CONTINUE
          CALL SOFTMX(DL, 10, SM)
          DO 14 J = 1, 10
              DL(J) = SM(J)
   14     CONTINUE
          DL(TARGET(I) + 1) = DL(TARGET(I) + 1) - 1.0
C
C --- DWOUT += Y * DL^T, DY = WOUT * DL ---
          DO 12 J = 1, 16
              S = 0.0
              DO 13 K = 1, 10
                  DWOUT(J, K) = DWOUT(J, K) + YY(I, J) * DL(K)
                  S = S + WOUT(J, K) * DL(K)
   13         CONTINUE
              DY(I, J) = S
   12     CONTINUE
   10 CONTINUE
C
C --- DO = DY (RESIDUAL), INIT DVV TO ZERO ---
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              DVV(I, J) = 0.0
   21     CONTINUE
   20 CONTINUE
C
C --- BACKWARD THROUGH O = A . V ---
C     DA[I,J] = DY[I] . V[J]
C     DVV[J] += A[I,J] * DY[I]
      DO 30 I = 1, 8
          DO 31 J = 1, 8
              S = 0.0
              DO 32 K = 1, 16
                  S = S + DY(I, K) * VV(J, K)
                  DVV(J, K) = DVV(J, K) + AA(I, J) * DY(I, K)
   32         CONTINUE
              DA(I, J) = S
   31     CONTINUE
   30 CONTINUE
C
C --- BACKWARD SOFTMAX ---
C     DSC[I,J] = A[I,J] * (DA[I,J] - SUM_K A[I,K]*DA[I,K])
C     REUSE DA TO STORE DSC
      DO 40 I = 1, 8
          DOTAD = 0.0
          DO 41 J = 1, 8
              DOTAD = DOTAD + AA(I, J) * DA(I, J)
   41     CONTINUE
          DO 42 J = 1, 8
              DA(I, J) = AA(I, J) * (DA(I, J) - DOTAD)
   42     CONTINUE
   40 CONTINUE
C
C --- BACKWARD SCALING ---
      DO 45 I = 1, 8
          DO 46 J = 1, 8
              DA(I, J) = DA(I, J) * SCALE
   46     CONTINUE
   45 CONTINUE
C
C --- BACKWARD Q . K^T ---
C     DQ[I,K] = SUM_J DSC[I,J] * K[J,K]
C     DK[J,K] += SUM_I DSC[I,J] * Q[I,K]
      DO 50 I = 1, 8
          DO 51 K = 1, 16
              DQ(I, K) = 0.0
   51     CONTINUE
   50 CONTINUE
      DO 55 J = 1, 8
          DO 56 K = 1, 16
              DK(J, K) = 0.0
   56     CONTINUE
   55 CONTINUE
      DO 60 I = 1, 8
          DO 61 J = 1, 8
              DO 62 K = 1, 16
                  DQ(I, K) = DQ(I, K) + DA(I, J) * KK(J, K)
                  DK(J, K) = DK(J, K) + DA(I, J) * QQ(I, K)
   62         CONTINUE
   61     CONTINUE
   60 CONTINUE
C
C --- BACKWARD PROJECTIONS ---
C     DX = DY (RESIDUAL)
      DO 70 I = 1, 8
          DO 71 J = 1, 16
              DX(I, J) = DY(I, J)
   71     CONTINUE
   70 CONTINUE
C
C     DWQ += X^T * DQ, DX += WQ * DQ
C     DWK += X^T * DK, DX += WK * DK
C     DWV += X^T * DV, DX += WV * DV
      DO 80 I = 1, 8
          DO 81 J = 1, 16
              DO 82 K = 1, 16
                  DWQ(J, K) = DWQ(J, K) + XX(I, J) * DQ(I, K)
                  DX(I, J) = DX(I, J) + WQ(J, K) * DQ(I, K)
                  DWK(J, K) = DWK(J, K) + XX(I, J) * DK(I, K)
                  DX(I, J) = DX(I, J) + WK(J, K) * DK(I, K)
                  DWV(J, K) = DWV(J, K) + XX(I, J) * DVV(I, K)
                  DX(I, J) = DX(I, J) + WV(J, K) * DVV(I, K)
   82         CONTINUE
   81     CONTINUE
   80 CONTINUE
C
C --- BACKWARD EMBEDDING ---
      DO 90 I = 1, 8
          TK = TOKENS(I) + 1
          DO 91 J = 1, 16
              DTOKEM(TK, J) = DTOKEM(TK, J) + DX(I, J)
              DPOSEM(I, J) = DPOSEM(I, J) + DX(I, J)
   91     CONTINUE
   90 CONTINUE
      RETURN
      END
C
C =========================================================
C UPDAT - SGD WEIGHT UPDATE
C =========================================================
C
      SUBROUTINE UPDAT(LREMB, LRATN, LROUT)
      REAL LREMB, LRATN, LROUT
      REAL TOKEM(10,16), POSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16), WOUT(16,10)
      REAL DTOKEM(10,16), DPOSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16), DWOUT(16,10)
      COMMON /WGTS/ TOKEM, POSEM, WQ, WK, WV, WOUT
      COMMON /GRAD/ DTOKEM, DPOSEM, DWQ, DWK, DWV, DWOUT
      INTEGER I, J
C
      DO 10 I = 1, 10
          DO 11 J = 1, 16
              TOKEM(I, J) = TOKEM(I, J) - LREMB * DTOKEM(I, J)
   11     CONTINUE
   10 CONTINUE
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              POSEM(I, J) = POSEM(I, J) - LREMB * DPOSEM(I, J)
   21     CONTINUE
   20 CONTINUE
      DO 30 I = 1, 16
          DO 31 J = 1, 16
              WQ(I, J) = WQ(I, J) - LRATN * DWQ(I, J)
              WK(I, J) = WK(I, J) - LRATN * DWK(I, J)
              WV(I, J) = WV(I, J) - LRATN * DWV(I, J)
   31     CONTINUE
   30 CONTINUE
      DO 40 I = 1, 16
          DO 41 J = 1, 10
              WOUT(I, J) = WOUT(I, J) - LROUT * DWOUT(I, J)
   41     CONTINUE
   40 CONTINUE
      RETURN
      END
C
C =========================================================
C ZEROG - ZERO ALL GRADIENTS
C =========================================================
C
      SUBROUTINE ZEROG
      REAL DTOKEM(10,16), DPOSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16), DWOUT(16,10)
      COMMON /GRAD/ DTOKEM, DPOSEM, DWQ, DWK, DWV, DWOUT
      INTEGER I, J
C
      DO 10 I = 1, 10
          DO 11 J = 1, 16
              DTOKEM(I, J) = 0.0
   11     CONTINUE
   10 CONTINUE
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              DPOSEM(I, J) = 0.0
   21     CONTINUE
   20 CONTINUE
      DO 30 I = 1, 16
          DO 31 J = 1, 16
              DWQ(I, J) = 0.0
              DWK(I, J) = 0.0
              DWV(I, J) = 0.0
   31     CONTINUE
   30 CONTINUE
      DO 40 I = 1, 16
          DO 41 J = 1, 10
              DWOUT(I, J) = 0.0
   41     CONTINUE
   40 CONTINUE
      RETURN
      END
C
C =========================================================
C SOFTMX - SOFTMAX ON A VECTOR
C =========================================================
C
      SUBROUTINE SOFTMX(VEC, N, OUT)
      INTEGER N
      REAL VEC(N), OUT(N)
      INTEGER I
      REAL VMAX, ESUM
C
C FIND MAX
      VMAX = VEC(1)
      DO 10 I = 2, N
          IF (VEC(I) .GT. VMAX) VMAX = VEC(I)
   10 CONTINUE
C EXP AND SUM
      ESUM = 0.0
      DO 20 I = 1, N
          OUT(I) = EXP(VEC(I) - VMAX)
          ESUM = ESUM + OUT(I)
   20 CONTINUE
C NORMALIZE
      DO 30 I = 1, N
          OUT(I) = OUT(I) / ESUM
   30 CONTINUE
      RETURN
      END
C
C =========================================================
C GENSMP - GENERATE RANDOM TRAINING SAMPLE
C =========================================================
C
      SUBROUTINE GENSMP(TOKENS, TARGET, RSEED)
      INTEGER TOKENS(8), TARGET(8)
      REAL RSEED
      INTEGER I
C
      DO 10 I = 1, 8
          RSEED = AMOD(RSEED * 8191.0 + 1.0, 32749.0)
          TOKENS(I) = INT(AMOD(RSEED, 10.0))
   10 CONTINUE
      DO 20 I = 1, 8
          TARGET(I) = TOKENS(9 - I)
   20 CONTINUE
      RETURN
      END
C
C =========================================================
C RNGAUS - PSEUDO-GAUSSIAN RNG (BOX-MULLER APPROX)
C         USES SIMPLE UNIFORM LCG + CHEAP APPROXIMATION
C =========================================================
C
      SUBROUTINE RNGAUS(RSEED, GAUSS)
      REAL RSEED, GAUSS, U1, U2
C
C TWO UNIFORM SAMPLES
      RSEED = AMOD(RSEED * 8191.0 + 1.0, 32749.0)
      U1 = RSEED / 32749.0
      RSEED = AMOD(RSEED * 8191.0 + 1.0, 32749.0)
      U2 = RSEED / 32749.0
C
C BOX-MULLER
      IF (U1 .LT. 1.0E-10) U1 = 1.0E-10
      GAUSS = SQRT(-2.0 * ALOG(U1)) * COS(6.2832 * U2)
      RETURN
      END
C
C =========================================================
C CELOSS - CROSS-ENTROPY LOSS (MONITORING ONLY)
C =========================================================
C
      SUBROUTINE CELOSS(LOGIT, TARGET, NSEQ, NVOC, LOSS)
      INTEGER NSEQ, NVOC, TARGET(NSEQ)
      REAL LOGIT(NSEQ, NVOC), LOSS
      INTEGER I, J, TK
      REAL VMAX, ESUM, P
C
      LOSS = 0.0
      DO 10 I = 1, NSEQ
          VMAX = LOGIT(I, 1)
          DO 11 J = 2, NVOC
              IF (LOGIT(I, J) .GT. VMAX) VMAX = LOGIT(I, J)
   11     CONTINUE
          ESUM = 0.0
          DO 12 J = 1, NVOC
              ESUM = ESUM + EXP(LOGIT(I, J) - VMAX)
   12     CONTINUE
          TK = TARGET(I) + 1
          P = EXP(LOGIT(I, TK) - VMAX) / ESUM
          IF (P .LT. 1.0E-10) P = 1.0E-10
          LOSS = LOSS - ALOG(P)
   10 CONTINUE
      LOSS = LOSS / FLOAT(NSEQ)
      RETURN
      END
