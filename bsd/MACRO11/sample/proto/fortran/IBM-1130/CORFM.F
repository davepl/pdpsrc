// JOB
// FOR
*IOCS (TYPEWRITER)
*LIST SOURCE PROGRAM
C
C =========================================
C COREFORMER - A TRANSFORMER IN FORTRAN IV
C PAPER TAPE IS ALL YOU NEED
C DAMIEN BOUREILLE, 2026
C MIT LICENSE
C =========================================
C
C ENCODER-ONLY TRANSFORMER, 1216 PARAMS
C LEARNS TO REVERSE 8 DIGITS (0-9)
C
      INTEGER TOKNS(8), TARGT(8), INPT(8)
      INTEGER I, J, K, STEP, PRED, BEST
      INTEGER NHIT, NTOT, SEQOK, I500, NTEST
      REAL LREMB, LRATN, LROUT
      REAL LOSS, PMAX, RSEED, ACC
C WEIGHTS
      REAL TKEM(10,16), PSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16)
      REAL WOUT(16,10)
C GRADIENTS
      REAL DTKEM(10,16), DPSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16)
      REAL DWOUT(16,10)
C FORWARD CACHE
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LGIT(8,10)
C BACKWARD TEMPS
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
C COMMON
      COMMON TKEM, PSEM, WQ, WK, WV, WOUT,
     $ DTKEM, DPSEM, DWQ, DWK, DWV, DWOUT,
     $ XX, QQ, KK, VV, AA, YY, LGIT,
     $ DY, DA, DQ, DK, DVV, DX
C
C CONSTANTS
      RSEED = 887.0
      LRATN = 0.08
      LREMB = 0.01
      LROUT = 0.0025
      NTEST = 20
C
C FORMATS
 9000 FORMAT(' COREFORMER: PAPER TAPE IS ALL YOU NEED')
 9001 FORMAT(' D=16 SEQ=8 V=10 PER-LAYER LR PARAMS=1216')
 9002 FORMAT(' ')
 9003 FORMAT(' STEP',I5,' LOSS=',F7.4,' ACC=',F5.3)
 9004 FORMAT(' ',8I2,' - ',8I2,'  OK')
 9007 FORMAT(' ',8I2,' - ',8I2,'  FAIL')
 9005 FORMAT(' ACCURACY  ',I3,'/',I3)
 9006 FORMAT(' TRAINING...')
C
      WRITE(1, 9000)
      WRITE(1, 9001)
      WRITE(1, 9002)
C
C INIT WEIGHTS
      CALL INIT(RSEED)
C
C --- TRAINING LOOP ---
C
      WRITE(1, 9006)
      NHIT = 0
      NTOT = 0
      DO 100 STEP = 1, 600
          CALL GENSM(TOKNS, TARGT, RSEED)
          CALL ZEROG
          CALL FORWD(TOKNS)
          CALL BKWRD(TOKNS, TARGT)
          CALL UPDAT(LREMB, LRATN, LROUT)
C
C COUNT CORRECT
          DO 110 I = 1, 8
              BEST = 1
              PMAX = LGIT(I, 1)
              DO 120 J = 2, 10
                  IF (LGIT(I,J) - PMAX) 120, 120, 121
 121              PMAX = LGIT(I, J)
                  BEST = J
 120          CONTINUE
              PRED = BEST - 1
              K = PRED - TARGT(I)
              IF (K) 110, 122, 110
 122          NHIT = NHIT + 1
 110      CONTINUE
          NTOT = NTOT + 8
C
C PRINT EVERY 100 STEPS
          I500 = IMOD(STEP, 100)
          IF (I500) 100, 130, 100
 130      CALL CELSS(LGIT, TARGT, LOSS)
          ACC = FLOAT(NHIT)/FLOAT(NTOT)
          WRITE(1, 9003) STEP, LOSS, ACC
          NHIT = 0
          NTOT = 0
 100  CONTINUE
C
C --- FINAL TEST ---
C
      WRITE(1, 9002)
      SEQOK = 0
      DO 200 I = 1, NTEST
          CALL GENSM(TOKNS, TARGT, RSEED)
          CALL FORWD(TOKNS)
C SAVE INPUT BEFORE OVERWRITING
          DO 205 J = 1, 8
              INPT(J) = TOKNS(J)
 205      CONTINUE
C FIND PREDICTIONS
          DO 210 J = 1, 8
              BEST = 1
              PMAX = LGIT(J, 1)
              DO 220 K = 2, 10
                  IF (LGIT(J,K) - PMAX) 220, 220, 221
 221              PMAX = LGIT(J, K)
                  BEST = K
 220          CONTINUE
              TOKNS(J) = BEST - 1
 210      CONTINUE
C CHECK SEQUENCE
          DO 230 J = 1, 8
              K = TOKNS(J) - TARGT(J)
              IF (K) 240, 230, 240
 230      CONTINUE
C ALL CORRECT
          SEQOK = SEQOK + 1
          WRITE(1, 9004) INPT, TOKNS
          GOTO 200
 240      WRITE(1, 9007) INPT, TOKNS
 200  CONTINUE
      WRITE(1, 9002)
      WRITE(1, 9005) SEQOK, NTEST
      STOP
      END
// DUP
*DELETE     WS  UA  CORFM
*STORE      WS  UA  CORFM
C
// FOR
*LIST SOURCE PROGRAM
C
C === INIT WEIGHTS WITH SEEDED RNG ===
C
      SUBROUTINE INIT(RSEED)
      REAL RSEED, RND, SCD, SCV
      INTEGER I, J
      REAL TKEM(10,16), PSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16)
      REAL WOUT(16,10)
      REAL DTKEM(10,16), DPSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16)
      REAL DWOUT(16,10)
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LGIT(8,10)
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
      COMMON TKEM, PSEM, WQ, WK, WV, WOUT,
     $ DTKEM, DPSEM, DWQ, DWK, DWV, DWOUT,
     $ XX, QQ, KK, VV, AA, YY, LGIT,
     $ DY, DA, DQ, DK, DVV, DX
C
      SCD = SQRT(2.0 / 32.0)
      SCV = SQRT(2.0 / 26.0)
C TOKEN EMBEDDINGS
      DO 10 I = 1, 10
          DO 11 J = 1, 16
              CALL RNGAU(RSEED, RND)
              TKEM(I, J) = RND * SCV
 11       CONTINUE
 10   CONTINUE
C POSITION EMBEDDINGS
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              CALL RNGAU(RSEED, RND)
              PSEM(I, J) = RND * SCD
 21       CONTINUE
 20   CONTINUE
C WQ
      DO 30 I = 1, 16
          DO 31 J = 1, 16
              CALL RNGAU(RSEED, RND)
              WQ(I, J) = RND * SCD
 31       CONTINUE
 30   CONTINUE
C WK
      DO 40 I = 1, 16
          DO 41 J = 1, 16
              CALL RNGAU(RSEED, RND)
              WK(I, J) = RND * SCD
 41       CONTINUE
 40   CONTINUE
C WV
      DO 50 I = 1, 16
          DO 51 J = 1, 16
              CALL RNGAU(RSEED, RND)
              WV(I, J) = RND * SCD
 51       CONTINUE
 50   CONTINUE
C WOUT
      DO 60 I = 1, 16
          DO 61 J = 1, 10
              CALL RNGAU(RSEED, RND)
              WOUT(I, J) = RND * SCV
 61       CONTINUE
 60   CONTINUE
      RETURN
      END
// DUP
*DELETE     WS  UA  INIT
*STORE      WS  UA  INIT
C
// FOR
*LIST SOURCE PROGRAM
C
C === FORWARD PASS ===
C
      SUBROUTINE FORWD(TOKNS)
      INTEGER TOKNS(8)
      INTEGER I, J, K, TK
      REAL S, SCALE
      REAL SM(10)
      REAL TKEM(10,16), PSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16)
      REAL WOUT(16,10)
      REAL DTKEM(10,16), DPSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16)
      REAL DWOUT(16,10)
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LGIT(8,10)
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
      COMMON TKEM, PSEM, WQ, WK, WV, WOUT,
     $ DTKEM, DPSEM, DWQ, DWK, DWV, DWOUT,
     $ XX, QQ, KK, VV, AA, YY, LGIT,
     $ DY, DA, DQ, DK, DVV, DX
C
      SCALE = 1.0 / SQRT(16.0)
C
C EMBEDDING
      DO 10 I = 1, 8
          TK = TOKNS(I) + 1
          DO 11 J = 1, 16
              XX(I,J) = TKEM(TK,J) + PSEM(I,J)
 11       CONTINUE
 10   CONTINUE
C
C Q, K, V PROJECTIONS
      DO 20 I = 1, 8
          DO 21 K = 1, 16
              S = 0.0
              DO 22 J = 1, 16
                  S = S + WQ(J,K) * XX(I,J)
 22           CONTINUE
              QQ(I,K) = S
 21       CONTINUE
          DO 23 K = 1, 16
              S = 0.0
              DO 24 J = 1, 16
                  S = S + WK(J,K) * XX(I,J)
 24           CONTINUE
              KK(I,K) = S
 23       CONTINUE
          DO 25 K = 1, 16
              S = 0.0
              DO 26 J = 1, 16
                  S = S + WV(J,K) * XX(I,J)
 26           CONTINUE
              VV(I,K) = S
 25       CONTINUE
 20   CONTINUE
C
C ATTENTION SCORES + SOFTMAX
      DO 30 I = 1, 8
          DO 31 J = 1, 8
              S = 0.0
              DO 32 K = 1, 16
                  S = S + QQ(I,K) * KK(J,K)
 32           CONTINUE
              AA(I,J) = S * SCALE
 31       CONTINUE
C EXTRACT ROW FOR SOFTMAX
          DO 33 J = 1, 8
              SM(J) = AA(I,J)
 33       CONTINUE
          CALL SFTMX(SM, 8, SM)
          DO 34 J = 1, 8
              AA(I,J) = SM(J)
 34       CONTINUE
 30   CONTINUE
C
C WEIGHTED SUM + RESIDUAL
      DO 40 I = 1, 8
          DO 41 K = 1, 16
              S = 0.0
              DO 42 J = 1, 8
                  S = S + AA(I,J) * VV(J,K)
 42           CONTINUE
              YY(I,K) = S + XX(I,K)
 41       CONTINUE
 40   CONTINUE
C
C OUTPUT PROJECTION
      DO 50 I = 1, 8
          DO 51 K = 1, 10
              S = 0.0
              DO 52 J = 1, 16
                  S = S + WOUT(J,K) * YY(I,J)
 52           CONTINUE
              LGIT(I,K) = S
 51       CONTINUE
 50   CONTINUE
      RETURN
      END
// DUP
*DELETE     WS  UA  FORWD
*STORE      WS  UA  FORWD
C
// FOR
*LIST SOURCE PROGRAM
C
C === BACKWARD PASS ===
C
      SUBROUTINE BKWRD(TOKNS, TARGT)
      INTEGER TOKNS(8), TARGT(8)
      INTEGER I, J, K, TK
      REAL DL(10), SM(10), S, SCALE, DOT
      REAL TKEM(10,16), PSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16)
      REAL WOUT(16,10)
      REAL DTKEM(10,16), DPSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16)
      REAL DWOUT(16,10)
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LGIT(8,10)
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
      COMMON TKEM, PSEM, WQ, WK, WV, WOUT,
     $ DTKEM, DPSEM, DWQ, DWK, DWV, DWOUT,
     $ XX, QQ, KK, VV, AA, YY, LGIT,
     $ DY, DA, DQ, DK, DVV, DX
C
      SCALE = 1.0 / SQRT(16.0)
C
C DL = SOFTMAX(LOGITS) - ONE_HOT
      DO 10 I = 1, 8
          DO 11 J = 1, 10
              DL(J) = LGIT(I,J)
 11       CONTINUE
          CALL SFTMX(DL, 10, SM)
          DO 14 J = 1, 10
              DL(J) = SM(J)
 14       CONTINUE
          TK = TARGT(I) + 1
          DL(TK) = DL(TK) - 1.0
C
C DWOUT += Y * DL^T, DY = WOUT * DL
          DO 12 J = 1, 16
              S = 0.0
              DO 13 K = 1, 10
                  DWOUT(J,K) = DWOUT(J,K) + YY(I,J)*DL(K)
                  S = S + WOUT(J,K) * DL(K)
 13           CONTINUE
              DY(I,J) = S
 12       CONTINUE
 10   CONTINUE
C
C INIT DVV TO ZERO
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              DVV(I,J) = 0.0
 21       CONTINUE
 20   CONTINUE
C
C BACKWARD O = A . V
      DO 30 I = 1, 8
          DO 31 J = 1, 8
              S = 0.0
              DO 32 K = 1, 16
                  S = S + DY(I,K) * VV(J,K)
                  DVV(J,K) = DVV(J,K) + AA(I,J)*DY(I,K)
 32           CONTINUE
              DA(I,J) = S
 31       CONTINUE
 30   CONTINUE
C
C BACKWARD SOFTMAX
      DO 40 I = 1, 8
          DOT = 0.0
          DO 41 J = 1, 8
              DOT = DOT + AA(I,J) * DA(I,J)
 41       CONTINUE
          DO 42 J = 1, 8
              DA(I,J) = AA(I,J) * (DA(I,J) - DOT)
 42       CONTINUE
 40   CONTINUE
C
C BACKWARD SCALING
      DO 45 I = 1, 8
          DO 46 J = 1, 8
              DA(I,J) = DA(I,J) * SCALE
 46       CONTINUE
 45   CONTINUE
C
C BACKWARD Q . K^T
      DO 50 I = 1, 8
          DO 51 K = 1, 16
              DQ(I,K) = 0.0
 51       CONTINUE
 50   CONTINUE
      DO 55 J = 1, 8
          DO 56 K = 1, 16
              DK(J,K) = 0.0
 56       CONTINUE
 55   CONTINUE
      DO 60 I = 1, 8
          DO 61 J = 1, 8
              DO 62 K = 1, 16
                  DQ(I,K) = DQ(I,K) + DA(I,J)*KK(J,K)
                  DK(J,K) = DK(J,K) + DA(I,J)*QQ(I,K)
 62           CONTINUE
 61       CONTINUE
 60   CONTINUE
C
C BACKWARD PROJECTIONS
      DO 70 I = 1, 8
          DO 71 J = 1, 16
              DX(I,J) = DY(I,J)
 71       CONTINUE
 70   CONTINUE
      DO 80 I = 1, 8
          DO 81 J = 1, 16
              DO 82 K = 1, 16
                  DWQ(J,K) = DWQ(J,K) + XX(I,J)*DQ(I,K)
                  DX(I,J) = DX(I,J) + WQ(J,K)*DQ(I,K)
                  DWK(J,K) = DWK(J,K) + XX(I,J)*DK(I,K)
                  DX(I,J) = DX(I,J) + WK(J,K)*DK(I,K)
                  DWV(J,K) = DWV(J,K) + XX(I,J)*DVV(I,K)
                  DX(I,J) = DX(I,J) + WV(J,K)*DVV(I,K)
 82           CONTINUE
 81       CONTINUE
 80   CONTINUE
C
C BACKWARD EMBEDDING
      DO 90 I = 1, 8
          TK = TOKNS(I) + 1
          DO 91 J = 1, 16
              DTKEM(TK,J) = DTKEM(TK,J) + DX(I,J)
              DPSEM(I,J) = DPSEM(I,J) + DX(I,J)
 91       CONTINUE
 90   CONTINUE
      RETURN
      END
// DUP
*DELETE     WS  UA  BKWRD
*STORE      WS  UA  BKWRD
C
// FOR
*LIST SOURCE PROGRAM
C
C === SGD WEIGHT UPDATE ===
C
      SUBROUTINE UPDAT(LREMB, LRATN, LROUT)
      REAL LREMB, LRATN, LROUT
      INTEGER I, J
      REAL TKEM(10,16), PSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16)
      REAL WOUT(16,10)
      REAL DTKEM(10,16), DPSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16)
      REAL DWOUT(16,10)
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LGIT(8,10)
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
      COMMON TKEM, PSEM, WQ, WK, WV, WOUT,
     $ DTKEM, DPSEM, DWQ, DWK, DWV, DWOUT,
     $ XX, QQ, KK, VV, AA, YY, LGIT,
     $ DY, DA, DQ, DK, DVV, DX
C
      DO 10 I = 1, 10
          DO 11 J = 1, 16
              TKEM(I,J) = TKEM(I,J) - LREMB*DTKEM(I,J)
 11       CONTINUE
 10   CONTINUE
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              PSEM(I,J) = PSEM(I,J) - LREMB*DPSEM(I,J)
 21       CONTINUE
 20   CONTINUE
      DO 30 I = 1, 16
          DO 31 J = 1, 16
              WQ(I,J) = WQ(I,J) - LRATN*DWQ(I,J)
              WK(I,J) = WK(I,J) - LRATN*DWK(I,J)
              WV(I,J) = WV(I,J) - LRATN*DWV(I,J)
 31       CONTINUE
 30   CONTINUE
      DO 40 I = 1, 16
          DO 41 J = 1, 10
              WOUT(I,J) = WOUT(I,J) - LROUT*DWOUT(I,J)
 41       CONTINUE
 40   CONTINUE
      RETURN
      END
// DUP
*DELETE     WS  UA  UPDAT
*STORE      WS  UA  UPDAT
C
// FOR
*LIST SOURCE PROGRAM
C
C === ZERO ALL GRADIENTS ===
C
      SUBROUTINE ZEROG
      INTEGER I, J
      REAL TKEM(10,16), PSEM(8,16)
      REAL WQ(16,16), WK(16,16), WV(16,16)
      REAL WOUT(16,10)
      REAL DTKEM(10,16), DPSEM(8,16)
      REAL DWQ(16,16), DWK(16,16), DWV(16,16)
      REAL DWOUT(16,10)
      REAL XX(8,16), QQ(8,16), KK(8,16), VV(8,16)
      REAL AA(8,8), YY(8,16), LGIT(8,10)
      REAL DY(8,16), DA(8,8), DQ(8,16), DK(8,16)
      REAL DVV(8,16), DX(8,16)
      COMMON TKEM, PSEM, WQ, WK, WV, WOUT,
     $ DTKEM, DPSEM, DWQ, DWK, DWV, DWOUT,
     $ XX, QQ, KK, VV, AA, YY, LGIT,
     $ DY, DA, DQ, DK, DVV, DX
C
      DO 10 I = 1, 10
          DO 11 J = 1, 16
              DTKEM(I,J) = 0.0
 11       CONTINUE
 10   CONTINUE
      DO 20 I = 1, 8
          DO 21 J = 1, 16
              DPSEM(I,J) = 0.0
 21       CONTINUE
 20   CONTINUE
      DO 30 I = 1, 16
          DO 31 J = 1, 16
              DWQ(I,J) = 0.0
              DWK(I,J) = 0.0
              DWV(I,J) = 0.0
 31       CONTINUE
 30   CONTINUE
      DO 40 I = 1, 16
          DO 41 J = 1, 10
              DWOUT(I,J) = 0.0
 41       CONTINUE
 40   CONTINUE
      RETURN
      END
// DUP
*DELETE     WS  UA  ZEROG
*STORE      WS  UA  ZEROG
*STORECI    WS  UA  CORFM
C
// XEQ CORFM
