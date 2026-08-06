
/* WARNING: Removing unreachable block (ram,0x000181a064b9) */
/* WARNING: Removing unreachable block (ram,0x000181a064cb) */
/* WARNING: Removing unreachable block (ram,0x000181a0644c) */
/* WARNING: Removing unreachable block (ram,0x000181a06471) */
/* WARNING: Removing unreachable block (ram,0x000181a05f18) */
/* WARNING: Removing unreachable block (ram,0x000181a060a4) */
/* WARNING: Removing unreachable block (ram,0x000181a05f1e) */
/* WARNING: Removing unreachable block (ram,0x000181a060a6) */
/* WARNING: Removing unreachable block (ram,0x000181a0644e) */
/* WARNING: Removing unreachable block (ram,0x000181a064c1) */
/* WARNING: Removing unreachable block (ram,0x000181a064cf) */
/* WARNING: Removing unreachable block (ram,0x000181a0625b) */
/* WARNING: Removing unreachable block (ram,0x000181a0626e) */
/* WARNING: Removing unreachable block (ram,0x000181a06263) */
/* WARNING: Removing unreachable block (ram,0x000181a06274) */
/* Spectral component reconstruction: preserves each complex component direction while changing
   magnitude, remaps harmonic fine structure by pitch ratio, rebuilds target/source vocal-tract
   envelopes independently, keeps noise/sibilance on separate paths with 1e-7 floor, then performs
   block power normalisation. */

void MULSS_reconstructSpectralComponents
               (longlong param_1,float *param_2,float *param_3,longlong param_4,longlong param_5,
               longlong param_6,float *param_7,float *param_8,longlong param_9,uint param_10,
               undefined8 param_11,float param_12,float param_13,float param_14,float param_15,
               float param_16,float param_17,longlong param_18,float *param_19,longlong param_20,
               float *param_21,float param_22,float param_23,float param_24,float param_25,
               float param_26,longlong param_27,longlong param_28,longlong param_29,
               longlong param_30,float *param_31,float *param_32,float param_33,float param_34,
               float param_35,float param_36,float param_37,float param_38)

{
  undefined1 (*pauVar1) [16];
  undefined1 auVar2 [16];
  longlong lVar3;
  undefined8 uVar4;
  bool bVar5;
  bool bVar6;
  int iVar7;
  int iVar8;
  uint uVar9;
  longlong *plVar10;
  longlong lVar11;
  float *pfVar12;
  longlong lVar13;
  int iVar14;
  longlong lVar15;
  float *pfVar16;
  ulonglong uVar17;
  int iVar18;
  longlong lVar19;
  longlong lVar20;
  ulonglong uVar21;
  longlong lVar22;
  longlong lVar23;
  longlong lVar24;
  longlong lVar25;
  uint uVar26;
  uint uVar27;
  uint uVar28;
  float fVar29;
  float fVar30;
  undefined4 uVar31;
  float fVar32;
  float fVar33;
  float fVar34;
  float fVar35;
  undefined1 auVar36 [16];
  float fVar37;
  float fVar38;
  float fVar39;
  float fVar40;
  float fStack_1e8;
  float fStack_1e4;
  longlong lStack_1b8;
  longlong lStack_180;
  ulonglong uStack_178;
  longlong lStack_148;
  char cStack_140;
  longlong lStack_138;
  undefined1 uStack_130;
  longlong lStack_128;
  char cStack_120;
  longlong lStack_118;
  char cStack_110;
  
  FUN_181c7bed0(*(undefined8 *)(param_1 + 0xa8),&lStack_148);
  plVar10 = (longlong *)FUN_1814d5ec0(lStack_148,&lStack_128);
  lVar3 = *plVar10;
  if ((char)plVar10[1] == '\0') {
    if (lVar3 != 0) {
      FUN_1815f0610(lVar3);
    }
  }
  else {
    *(undefined1 *)(plVar10 + 1) = 0;
  }
  if ((cStack_120 != '\0') && (lStack_128 != 0)) {
    FUN_1815ef980();
  }
  if ((cStack_140 != '\0') && (lStack_148 != 0)) {
    FUN_1815ef980();
  }
  fVar29 = 1.0 / param_23;
  param_22 = param_12 / param_22;
  fVar33 = 1.0 / param_22;
  lVar24 = (longlong)(int)param_10;
  iVar7 = FUN_1815e0aa0((float)(int)param_10 * fVar33,2);
  if (0x3ff < iVar7) {
    iVar7 = 0x3ff;
  }
  param_16 = param_12 * param_16;
  if (((((param_17 < 0.999) || (1.001 < param_17)) || (param_18 != 0)) ||
      ((param_19 != (float *)0x0 || (param_20 != 0)))) || (bVar5 = false, param_21 != (float *)0x0))
  {
    bVar5 = true;
  }
  fStack_1e8 = 0.5;
  fStack_1e4 = 0.5;
  if (1.1754944e-38 <= param_14 - param_13) {
    fStack_1e4 = (float)FUN_18298fda0((param_16 - param_13) / (param_14 - param_13));
    fStack_1e8 = 1.0 - fStack_1e4;
  }
  bVar6 = false;
  param_14 = 1.0;
  lVar23 = (longlong)iVar7;
  if ((0.0 < param_24) && (param_27 != 0)) {
    memset(param_7,0,lVar24 * 4);
    memset(param_2,0,lVar24 * 4);
    bVar6 = true;
    fVar30 = (float)powf();
    fVar32 = 1.0;
    if (fVar30 <= 1.0) {
      fVar32 = fVar30;
    }
    param_14 = 1.0 - fVar32;
    iVar18 = 1;
    if (0 < lVar23) {
      lVar20 = 1;
      do {
        lVar15 = lVar20;
        if (0x1ff < lVar20) {
          lVar15 = 0x1ff;
        }
        iVar8 = FUN_1815e0aa0(0x1ff,1);
        if ((int)(param_10 - 1) <= iVar8) break;
        fVar37 = (float)iVar18 * param_22 - (float)iVar8;
        fVar30 = (fStack_1e8 * *(float *)(param_28 + lVar15 * 4) + *(float *)(param_27 + lVar15 * 4)
                 + fStack_1e4 * *(float *)(param_29 + lVar15 * 4)) * param_25 * 0.6 * 0.5;
        param_7[iVar8] = (1.0 - fVar37) * fVar30 + param_7[iVar8];
        param_7[(longlong)iVar8 + 1] = fVar30 * fVar37 + param_7[(longlong)iVar8 + 1];
        iVar18 = iVar18 + 1;
        lVar20 = lVar20 + 1;
      } while (lVar20 <= lVar23);
    }
    param_23 = 0.0;
    iVar18 = *(int *)(param_1 + 0x168);
    if (0 < iVar18) {
      lVar20 = 0;
      lVar15 = (longlong)param_7 - (longlong)param_2;
      do {
        lVar13 = *(longlong *)(lVar20 + *(longlong *)(param_1 + 0x1f0));
        lVar19 = 0;
        if (3 < lVar24) {
          pfVar12 = param_2 + 1;
          pfVar16 = (float *)(lVar13 + 4);
          lVar25 = (lVar24 - 4U >> 2) + 1;
          lVar19 = lVar25 * 4;
          do {
            fVar30 = pfVar16[-1];
            fVar37 = *pfVar16;
            fVar40 = ABS(fVar30);
            fVar38 = ABS(fVar37);
            fVar39 = fVar38;
            if ((fVar40 != 0.0) && (fVar39 = fVar40, fVar38 != 0.0)) {
              if (fVar40 <= fVar38) {
                fVar39 = (fVar40 / fVar38) * (fVar40 / fVar38) + 1.0;
                if (fVar39 < 0.0) {
                  fVar39 = (float)sqrtf();
                }
                else {
                  fVar39 = SQRT(fVar39);
                }
                fVar39 = fVar39 * fVar38;
              }
              else {
                fVar39 = (fVar38 / fVar40) * (fVar38 / fVar40) + 1.0;
                if (fVar39 < 0.0) {
                  fVar39 = (float)sqrtf();
                  fVar39 = fVar39 * fVar40;
                }
                else {
                  fVar39 = SQRT(fVar39) * fVar40;
                }
              }
            }
            fVar38 = fVar32 * *(float *)(lVar15 + -4 + (longlong)pfVar12) + fVar39 * param_14;
            if (fVar39 <= 1e-05) {
              pfVar16[-1] = fVar38;
              *pfVar16 = 0.0;
            }
            else {
              pfVar16[-1] = fVar30 * (fVar38 / fVar39);
              *pfVar16 = fVar37 * (fVar38 / fVar39);
            }
            pfVar12[-1] = fVar38 + pfVar12[-1];
            fVar30 = pfVar16[1];
            fVar38 = ABS(fVar30);
            fVar39 = ABS(pfVar16[2]);
            fVar37 = fVar39;
            if ((fVar38 != 0.0) && (fVar37 = fVar38, fVar39 != 0.0)) {
              if (fVar38 <= fVar39) {
                fVar37 = (fVar38 / fVar39) * (fVar38 / fVar39) + 1.0;
                if (fVar37 < 0.0) {
                  fVar37 = (float)sqrtf();
                }
                else {
                  fVar37 = SQRT(fVar37);
                }
                fVar37 = fVar37 * fVar39;
              }
              else {
                fVar37 = (fVar39 / fVar38) * (fVar39 / fVar38) + 1.0;
                if (fVar37 < 0.0) {
                  fVar37 = (float)sqrtf();
                  fVar37 = fVar37 * fVar38;
                }
                else {
                  fVar37 = SQRT(fVar37) * fVar38;
                }
              }
            }
            fVar39 = fVar32 * *(float *)(lVar15 + (longlong)pfVar12) + fVar37 * param_14;
            if (fVar37 <= 1e-05) {
              pfVar16[1] = fVar39;
              pfVar16[2] = 0.0;
            }
            else {
              pfVar16[1] = fVar30 * (fVar39 / fVar37);
              pfVar16[2] = (fVar39 / fVar37) * pfVar16[2];
            }
            *pfVar12 = fVar39 + *pfVar12;
            fVar30 = pfVar16[3];
            fVar37 = pfVar16[4];
            fVar40 = ABS(fVar30);
            fVar38 = ABS(fVar37);
            fVar39 = fVar38;
            if ((fVar40 != 0.0) && (fVar39 = fVar40, fVar38 != 0.0)) {
              if (fVar40 <= fVar38) {
                fVar39 = (fVar40 / fVar38) * (fVar40 / fVar38) + 1.0;
                if (fVar39 < 0.0) {
                  fVar39 = (float)sqrtf();
                }
                else {
                  fVar39 = SQRT(fVar39);
                }
                fVar39 = fVar39 * fVar38;
              }
              else {
                fVar39 = (fVar38 / fVar40) * (fVar38 / fVar40) + 1.0;
                if (fVar39 < 0.0) {
                  fVar39 = (float)sqrtf();
                  fVar39 = fVar39 * fVar40;
                }
                else {
                  fVar39 = SQRT(fVar39) * fVar40;
                }
              }
            }
            fVar38 = fVar32 * *(float *)(lVar15 + 4 + (longlong)pfVar12) + fVar39 * param_14;
            if (fVar39 <= 1e-05) {
              pfVar16[3] = fVar38;
              pfVar16[4] = 0.0;
            }
            else {
              pfVar16[3] = fVar30 * (fVar38 / fVar39);
              pfVar16[4] = fVar37 * (fVar38 / fVar39);
            }
            pfVar12[1] = fVar38 + pfVar12[1];
            fVar30 = pfVar16[5];
            fVar38 = ABS(fVar30);
            fVar39 = ABS(pfVar16[6]);
            fVar37 = fVar39;
            if ((fVar38 != 0.0) && (fVar37 = fVar38, fVar39 != 0.0)) {
              if (fVar38 <= fVar39) {
                fVar37 = (fVar38 / fVar39) * (fVar38 / fVar39) + 1.0;
                if (fVar37 < 0.0) {
                  fVar37 = (float)sqrtf();
                }
                else {
                  fVar37 = SQRT(fVar37);
                }
                fVar37 = fVar37 * fVar39;
              }
              else {
                fVar37 = (fVar39 / fVar38) * (fVar39 / fVar38) + 1.0;
                if (fVar37 < 0.0) {
                  fVar37 = (float)sqrtf();
                  fVar37 = fVar37 * fVar38;
                }
                else {
                  fVar37 = SQRT(fVar37) * fVar38;
                }
              }
            }
            fVar39 = fVar32 * *(float *)(lVar15 + 8 + (longlong)pfVar12) + fVar37 * param_14;
            if (fVar37 <= 1e-05) {
              pfVar16[5] = fVar39;
              pfVar16[6] = 0.0;
            }
            else {
              pfVar16[5] = fVar30 * (fVar39 / fVar37);
              pfVar16[6] = (fVar39 / fVar37) * pfVar16[6];
            }
            pfVar12[2] = fVar39 + pfVar12[2];
            pfVar12 = pfVar12 + 4;
            pfVar16 = pfVar16 + 8;
            lVar25 = lVar25 + -1;
          } while (lVar25 != 0);
        }
        if (lVar19 < lVar24) {
          pfVar12 = param_2 + lVar19;
          do {
            fVar30 = *(float *)(lVar13 + lVar19 * 8);
            fVar37 = *(float *)(lVar13 + 4 + lVar19 * 8);
            fVar40 = ABS(fVar30);
            fVar38 = ABS(fVar37);
            fVar39 = fVar38;
            if ((fVar40 != 0.0) && (fVar39 = fVar40, fVar38 != 0.0)) {
              if (fVar40 <= fVar38) {
                fVar39 = (fVar40 / fVar38) * (fVar40 / fVar38) + 1.0;
                if (fVar39 < 0.0) {
                  fVar39 = (float)sqrtf();
                }
                else {
                  fVar39 = SQRT(fVar39);
                }
                fVar39 = fVar39 * fVar38;
              }
              else {
                fVar39 = (fVar38 / fVar40) * (fVar38 / fVar40) + 1.0;
                if (fVar39 < 0.0) {
                  fVar39 = (float)sqrtf();
                  fVar39 = fVar39 * fVar40;
                }
                else {
                  fVar39 = SQRT(fVar39) * fVar40;
                }
              }
            }
            fVar38 = fVar32 * *(float *)(lVar15 + (longlong)pfVar12) + fVar39 * param_14;
            if (fVar39 <= 1e-05) {
              *(float *)(lVar13 + lVar19 * 8) = fVar38;
              *(undefined4 *)(lVar13 + 4 + lVar19 * 8) = 0;
            }
            else {
              *(float *)(lVar13 + lVar19 * 8) = fVar30 * (fVar38 / fVar39);
              *(float *)(lVar13 + 4 + lVar19 * 8) = fVar37 * (fVar38 / fVar39);
            }
            *pfVar12 = fVar38 + *pfVar12;
            lVar19 = lVar19 + 1;
            pfVar12 = pfVar12 + 1;
          } while (lVar19 < lVar24);
        }
        else {
          lVar15 = (longlong)param_7 - (longlong)param_2;
        }
        param_23 = (float)((int)param_23 + 1);
        lVar20 = lVar20 + 8;
        iVar18 = *(int *)(param_1 + 0x168);
      } while ((int)param_23 < iVar18);
    }
    if (1 < iVar18) {
      (**(code **)(DAT_18357ff90 + 0x20))(param_2,1.0 / (float)iVar18,param_10);
    }
  }
  uVar21 = 0;
  lStack_1b8 = param_4;
  if ((param_4 != 0) && (((param_5 != 0 || (param_6 != 0)) && (lStack_1b8 = param_9, -1 < lVar23))))
  {
    do {
      uVar17 = uVar21;
      if (0x1ff < uVar21) {
        uVar17 = 0x1ff;
      }
      powf();
      if (param_5 != 0) {
        powf();
      }
      if (param_6 != 0) {
        powf();
      }
      uVar31 = powf();
      *(undefined4 *)(param_9 + uVar17 * 4) = uVar31;
      uVar21 = uVar21 + 1;
    } while ((longlong)uVar21 <= lVar23);
  }
  lStack_180 = param_18;
  if (((param_19 != (float *)0x0) || (param_20 != 0)) || (param_21 != (float *)0x0)) {
    lStack_180 = param_9 + (longlong)(*(int *)(param_1 + 0x164) / 2) * 4;
    iVar18 = 0;
    if (-1 < lVar23) {
      uVar21 = 0;
      do {
        uVar17 = uVar21;
        if (0x1ff < uVar21) {
          uVar17 = 0x1ff;
        }
        if (param_18 != 0) {
          powf();
        }
        if (param_19 != (float *)0x0) {
          powf();
        }
        if (param_20 != 0) {
          powf();
        }
        fVar32 = (float)powf();
        if (param_21 != (float *)0x0) {
          iVar8 = FUN_1815e0aa0();
          if (iVar8 < 0) {
            fVar30 = *param_21;
          }
          else if (iVar8 < 0x7ff) {
            fVar30 = (float)iVar18 * param_12 * fVar29 - (float)iVar8;
            fVar30 = (1.0 - fVar30) * param_21[iVar8] + fVar30 * param_21[(longlong)iVar8 + 1];
          }
          else {
            fVar30 = param_21[0x7ff];
          }
          fVar32 = fVar32 * fVar30;
        }
        *(float *)(uVar17 * 4 + lStack_180) = fVar32;
        iVar18 = iVar18 + 1;
        uVar21 = uVar21 + 1;
      } while ((longlong)uVar21 <= lVar23);
    }
  }
  iVar18 = FUN_1815e0aa0();
  fVar32 = 0.0;
  if (1 < lVar24) {
    lVar20 = 1;
    if (4 < (int)param_10) {
      pfVar12 = param_2 + 3;
      lVar15 = (lVar24 - 5U >> 2) + 1;
      lVar20 = lVar15 * 4 + 1;
      do {
        fVar32 = fVar32 + pfVar12[-2] + pfVar12[-1] + *pfVar12 + pfVar12[1];
        pfVar12 = pfVar12 + 4;
        lVar15 = lVar15 + -1;
      } while (lVar15 != 0);
      if (lVar24 <= lVar20) goto LAB_181a05696;
    }
    do {
      fVar32 = fVar32 + param_2[lVar20];
      lVar20 = lVar20 + 1;
    } while (lVar20 < lVar24);
  }
LAB_181a05696:
  param_19 = (float *)0x0;
  if ((param_26 != 0.0) && (param_30 != 0)) {
    if (param_26 <= 0.0) {
      param_26 = (float)powf();
      param_26 = param_26 + param_26;
      if (1.0 < param_26) {
        param_26 = (float)powf();
      }
    }
    else {
      fVar30 = (float)powf();
      param_26 = -fVar30 + -fVar30;
    }
    param_19 = param_8 + *(int *)(param_1 + 0x164) / 2;
    memset(param_19,0);
    *param_19 = 0.0;
    iVar8 = 1;
    if (0 < lVar23) {
      uVar21 = 1;
      do {
        uVar17 = uVar21;
        if (0x1ff < uVar21) {
          uVar17 = 0x1ff;
        }
        uVar9 = FUN_1815e0aa0((float)iVar8 * param_12 * fVar29,0);
        fVar30 = 0.0;
        if (uVar9 < 0x800) {
          fVar30 = *(float *)(param_30 + (longlong)(int)uVar9 * 4);
        }
        param_19[uVar17] = fVar30;
        iVar8 = iVar8 + 1;
        uVar21 = uVar21 + 1;
      } while ((longlong)uVar21 <= lVar23);
    }
    lVar15 = 0;
    lVar19 = 2;
    fVar30 = 0.0;
    lVar20 = 1;
    if (3 < lVar23) {
      lVar13 = 3;
      do {
        lVar25 = lVar20;
        if (0x1ff < lVar20) {
          lVar25 = 0x1ff;
        }
        if (lVar13 + -1 < 0x200) {
          fVar30 = fVar30 + param_19[lVar25] + param_19[lVar13 + -1];
          lVar25 = lVar13;
          if (0x1ff < lVar13) goto LAB_181a05858;
        }
        else {
          fVar30 = fVar30 + param_19[lVar25] + param_19[0x1ff];
LAB_181a05858:
          lVar25 = 0x1ff;
        }
        lVar11 = lVar13 + 1;
        if (0x1ff < lVar11) {
          lVar11 = 0x1ff;
        }
        fVar30 = fVar30 + param_19[lVar25] + param_19[lVar11];
        lVar20 = lVar20 + 4;
        lVar13 = lVar13 + 4;
      } while (lVar20 <= lVar23 + -3);
    }
    for (; lVar20 <= lVar23; lVar20 = lVar20 + 1) {
      lVar13 = lVar20;
      if (0x1ff < lVar20) {
        lVar13 = 0x1ff;
      }
      fVar30 = fVar30 + param_19[lVar13];
    }
    if (1.1754944e-38 <= fVar30) {
      (**(code **)(DAT_18357ff90 + 0x20))(param_19,*(code **)(DAT_18357ff90 + 0x20),iVar7 + 1);
    }
    if (-1 < lVar23) {
      if (2 < lVar23) {
        do {
          lVar20 = lVar15;
          if (0x1ff < lVar15) {
            lVar20 = 0x1ff;
          }
          if (param_19[lVar20] <= 1e-07 && param_19[lVar20] != 1e-07) {
            param_19[lVar20] = 1e-07;
          }
          lVar20 = lVar19 + -1;
          if (0x1ff < lVar20) {
            lVar20 = 0x1ff;
          }
          if (param_19[lVar20] <= 1e-07 && param_19[lVar20] != 1e-07) {
            param_19[lVar20] = 1e-07;
          }
          lVar20 = lVar19;
          if (0x1ff < lVar19) {
            lVar20 = 0x1ff;
          }
          if (param_19[lVar20] <= 1e-07 && param_19[lVar20] != 1e-07) {
            param_19[lVar20] = 1e-07;
          }
          lVar20 = lVar19 + 1;
          if (0x1ff < lVar20) {
            lVar20 = 0x1ff;
          }
          if (param_19[lVar20] <= 1e-07 && param_19[lVar20] != 1e-07) {
            param_19[lVar20] = 1e-07;
          }
          lVar15 = lVar15 + 4;
          lVar19 = lVar19 + 4;
        } while (lVar15 <= lVar23 + -3);
        if (lVar23 < lVar15) goto LAB_181a0599d;
      }
      do {
        lVar20 = lVar15;
        if (0x1ff < lVar15) {
          lVar20 = 0x1ff;
        }
        if (param_19[lVar20] <= 1e-07 && param_19[lVar20] != 1e-07) {
          param_19[lVar20] = 1e-07;
        }
        lVar15 = lVar15 + 1;
      } while (lVar15 <= lVar23);
    }
  }
LAB_181a0599d:
  memset(param_7,0,lVar24 * 4);
  param_13 = 0.0;
  lVar20 = (longlong)(iVar18 * 2 + 1);
  if (-1 < lVar23) {
    uStack_178 = 0;
    do {
      lVar15 = 0;
      uVar21 = uStack_178;
      if (0x1ff < uStack_178) {
        uVar21 = 0x1ff;
      }
      fVar30 = 1.0;
      if (lStack_1b8 != 0) {
        fVar30 = *(float *)(lStack_1b8 + uVar21 * 4);
      }
      fVar38 = (float)(int)param_13;
      iVar7 = FUN_1815e0aa0();
      iVar8 = FUN_1815e0aa0();
      iVar8 = iVar8 - iVar18;
      fVar37 = 0.0;
      fVar39 = 0.0;
      lVar19 = (longlong)iVar8;
      if (0 < lVar20) {
        pfVar12 = param_8;
        iVar14 = iVar8;
        do {
          fVar40 = 0.0;
          if (((0 < iVar14) && (iVar14 < (int)param_10)) &&
             (fVar34 = ABS((float)iVar14 - fVar38 * param_22), fVar34 < param_22)) {
            fVar40 = (float)FUN_18298fda0(1.0 - fVar34 * fVar33);
            fVar40 = fVar40 * *(float *)((lVar19 * 4 - (longlong)param_8) + (longlong)param_2 +
                                        (longlong)pfVar12);
          }
          if ((lVar15 + lVar19 == (longlong)iVar7) || (iVar14 == iVar7 + 1)) {
            fVar39 = fVar39 + fVar40;
          }
          *pfVar12 = fVar40;
          fVar37 = fVar37 + fVar40;
          iVar14 = iVar14 + 1;
          lVar15 = lVar15 + 1;
          pfVar12 = pfVar12 + 1;
        } while (lVar15 < lVar20);
      }
      if (param_31 != (float *)0x0) {
        iVar14 = FUN_1815e0aa0();
        if (iVar14 < 0) {
          fVar40 = *param_31;
        }
        else if (iVar14 < 0x7ff) {
          fVar40 = fVar38 * param_16 * fVar29 - (float)iVar14;
          fVar40 = (1.0 - fVar40) * param_31[iVar14] + fVar40 * param_31[(longlong)iVar14 + 1];
        }
        else {
          fVar40 = param_31[0x7ff];
        }
        fVar30 = fVar30 * fVar40;
      }
      if (param_37 != 1.0) {
        fVar30 = (float)powf();
      }
      if ((param_36 != 1.0) && (param_13 != 0.0)) {
        fVar40 = (float)powf();
        fVar30 = fVar30 * fVar40 * fVar38;
      }
      if ((bVar5) && (1.1754944e-38 <= fVar37)) {
        fVar40 = param_17;
        if (lStack_180 != 0) {
          fVar40 = param_17 * *(float *)(lStack_180 + uVar21 * 4);
        }
        iVar14 = FUN_1815e0aa0();
        iVar14 = iVar14 - iVar18;
        fVar34 = 0.0;
        if (0 < lVar20) {
          pfVar12 = param_2 + iVar14;
          lVar15 = lVar20;
          do {
            if (((0 < iVar14) && (iVar14 < (int)param_10)) &&
               (fVar35 = ABS((float)iVar14 - fVar40 * fVar38 * param_22), fVar35 < param_22)) {
              fVar35 = (float)FUN_18298fda0(1.0 - fVar35 * fVar33);
              fVar34 = fVar34 + fVar35 * *pfVar12;
            }
            iVar14 = iVar14 + 1;
            pfVar12 = pfVar12 + 1;
            lVar15 = lVar15 + -1;
          } while (lVar15 != 0);
        }
        fVar30 = fVar30 * (fVar34 / fVar37);
      }
      if (param_19 != (float *)0x0) {
        if (param_13 == 0.0) {
          fVar40 = 0.0;
          if (0.0 <= 1.0 - ABS(param_26)) {
            fVar40 = 1.0 - ABS(param_26);
          }
          fVar30 = fVar30 * fVar40;
        }
        else if (fVar37 < 1e-09) {
          fVar30 = 0.0;
        }
        else {
          fVar40 = (float)powf();
          fVar30 = fVar30 * fVar40;
        }
      }
      if (0 < lVar20) {
        if (bVar6) {
          pfVar12 = param_7 + lVar19;
          lVar13 = (longlong)param_8 + (lVar19 * -4 - (longlong)param_7);
          lVar15 = lVar20;
          do {
            if ((-1 < lVar19) && (lVar19 < lVar24)) {
              if ((lVar19 == iVar7) || (iVar8 == iVar7 + 1)) {
                fVar40 = fVar30 * *(float *)(lVar13 + (longlong)pfVar12);
              }
              else {
                fVar40 = fVar30 * *(float *)(lVar13 + (longlong)pfVar12) * param_14;
              }
              *pfVar12 = fVar40 + *pfVar12;
            }
            iVar8 = iVar8 + 1;
            lVar19 = lVar19 + 1;
            pfVar12 = pfVar12 + 1;
            lVar15 = lVar15 + -1;
          } while (lVar15 != 0);
        }
        else {
          lVar15 = 0;
          if (3 < lVar20) {
            lVar13 = lVar19 + 2;
            pfVar12 = param_7 + lVar13;
            lVar22 = (longlong)param_8 + (lVar19 * -4 - (longlong)param_7);
            lVar25 = lVar13 * -4;
            lVar11 = (lVar20 - 4U >> 2) + 1;
            lVar15 = lVar11 * 4;
            do {
              if ((-1 < lVar13 + -2) && (lVar13 + -2 < lVar24)) {
                pfVar12[-2] = fVar30 * *(float *)((longlong)param_8 + (lVar25 - (longlong)param_7) +
                                                 (longlong)pfVar12) + pfVar12[-2];
              }
              if ((-1 < lVar13 + -1) && (lVar13 + -1 < lVar24)) {
                pfVar12[-1] = fVar30 * *(float *)(lVar22 + -4 + (longlong)pfVar12) + pfVar12[-1];
              }
              if ((-1 < lVar13) && (lVar13 < lVar24)) {
                *pfVar12 = fVar30 * *(float *)(lVar22 + (longlong)pfVar12) + *pfVar12;
              }
              if ((-1 < lVar13 + 1) && (lVar13 + 1 < lVar24)) {
                pfVar12[1] = fVar30 * *(float *)(lVar22 + 4 + (longlong)pfVar12) + pfVar12[1];
              }
              pfVar12 = pfVar12 + 4;
              lVar13 = lVar13 + 4;
              lVar11 = lVar11 + -1;
            } while (lVar11 != 0);
          }
          if (lVar15 < lVar20) {
            lVar13 = lVar15 + lVar19;
            pfVar12 = param_7 + lVar13;
            lVar15 = lVar20 - lVar15;
            do {
              if ((-1 < lVar13) && (lVar13 < lVar24)) {
                *pfVar12 = fVar30 * *(float *)((longlong)pfVar12 +
                                              (longlong)param_8 + (lVar19 * -4 - (longlong)param_7))
                           + *pfVar12;
              }
              lVar13 = lVar13 + 1;
              pfVar12 = pfVar12 + 1;
              lVar15 = lVar15 + -1;
            } while (lVar15 != 0);
          }
        }
      }
      if ((lVar3 != 0) && (param_13 != 0.0)) {
        uStack_130 = 0;
        iVar7 = -1;
        lStack_138 = lVar3;
        while (iVar7 = iVar7 + 1, iVar7 < *(int *)(lVar3 + 0x10)) {
          uVar4 = *(undefined8 *)(*(longlong *)(lVar3 + 0x18) + (longlong)iVar7 * 8);
          plVar10 = (longlong *)FUN_180f13710(uVar4,&lStack_118);
          lVar15 = *plVar10;
          if ((char)plVar10[1] == '\0') {
            if (lVar15 != 0) {
              FUN_1815f0610(lVar15);
            }
          }
          else {
            *(undefined1 *)(plVar10 + 1) = 0;
          }
          if ((cStack_110 != '\0') && (lStack_118 != 0)) {
            FUN_1815ef980();
          }
          iVar8 = FUN_181712230(uVar4);
          if (iVar8 == 3) {
            fVar40 = (float)logf(fVar38 * param_16 * 0.1223122);
            fVar40 = (fVar40 * 1731.234) / 100.0 - 35.0;
            if (param_32 != (float *)0x0) {
              iVar8 = FUN_1815e0aa0();
              if (iVar8 < 0) {
                fVar34 = *param_32;
              }
              else if (iVar8 < 0x65) {
                fVar34 = (param_32[(longlong)iVar8 + 1] - param_32[iVar8]) * (fVar40 - (float)iVar8)
                         + param_32[iVar8];
              }
              else {
                fVar34 = param_32[0x65];
              }
              fVar40 = fVar40 - fVar34 * 0.01;
            }
            iVar8 = FUN_1815e0aa0();
            fVar34 = fVar37 * fVar30;
            if (-1 < iVar8) {
              lVar19 = *(longlong *)(lVar15 + 0x18);
              if (iVar8 < 0x65) {
                lVar13 = (longlong)iVar8;
                *(float *)(lVar19 + lVar13 * 4) =
                     (1.0 - (fVar40 - (float)iVar8)) * fVar34 + *(float *)(lVar19 + lVar13 * 4);
                *(float *)(*(longlong *)(lVar15 + 0x18) + 4 + lVar13 * 4) =
                     (fVar40 - (float)iVar8) * fVar34 +
                     *(float *)(*(longlong *)(lVar15 + 0x18) + 4 + lVar13 * 4);
              }
              else {
                *(float *)(lVar19 + 0x194) = fVar34 + *(float *)(lVar19 + 0x194);
              }
            }
          }
          else {
            *(float *)(*(longlong *)(lVar15 + 0x18) + uVar21 * 4) =
                 fVar39 * fVar30 + *(float *)(*(longlong *)(lVar15 + 0x18) + uVar21 * 4);
          }
          if (lVar15 != 0) {
            FUN_1815ef980(lVar15);
          }
        }
      }
      param_13 = (float)((int)param_13 + 1);
      uStack_178 = uStack_178 + 1;
    } while ((longlong)uStack_178 <= lVar23);
  }
  iVar7 = -1;
  iVar18 = 0;
  fVar29 = 0.0;
  lVar23 = 1;
  if (1 < lVar24) {
    lVar20 = 1;
    if (4 < (int)param_10) {
      pfVar12 = param_7 + 3;
      lVar15 = (lVar24 - 5U >> 2) + 1;
      lVar20 = lVar15 * 4 + 1;
      do {
        fVar29 = fVar29 + pfVar12[-2] + pfVar12[-1] + *pfVar12 + pfVar12[1];
        pfVar12 = pfVar12 + 4;
        lVar15 = lVar15 + -1;
      } while (lVar15 != 0);
      if (lVar24 <= lVar20) goto LAB_181a06350;
    }
    do {
      fVar29 = fVar29 + param_7[lVar20];
      lVar20 = lVar20 + 1;
    } while (lVar20 < lVar24);
  }
LAB_181a06350:
  if (param_34 != 1.0) {
    if (1.1754944e-38 <= param_35) {
      fVar33 = 1.0 / (param_35 * 1.5);
      fVar32 = fVar32 * fVar33;
      fVar29 = fVar29 * fVar33;
    }
    if (1.0 < fVar32) {
      fVar29 = fVar29 / fVar32;
    }
    if (1.0 < param_34) {
      powf();
    }
    fVar32 = (float)powf();
  }
  fVar33 = 1.0;
  if (1.1754944e-38 <= fVar29) {
    fVar33 = fVar32 / fVar29;
  }
  fVar29 = 100.0;
  if (fVar33 <= 100.0) {
    fVar29 = fVar33;
  }
  uStack_130 = 0;
  lStack_138 = lVar3;
  if (lVar3 != 0) {
    while (iVar7 = iVar7 + 1, iVar7 < *(int *)(lVar3 + 0x10)) {
      uVar4 = *(undefined8 *)(*(longlong *)(lVar3 + 0x18) + (longlong)iVar7 * 8);
      FUN_181e24450(uVar4);
      FUN_181e23bf0(uVar4);
    }
  }
  if ((param_38 < 1.0) && (1.1754944e-38 <= fVar29)) {
    fVar33 = (float)powf();
    if (1 < lVar24) {
      do {
        if (fVar33 / fVar29 < param_7[lVar23]) {
          param_7[lVar23] = fVar33 / fVar29;
        }
        lVar23 = lVar23 + 1;
      } while (lVar23 < lVar24);
    }
  }
  fVar29 = fVar29 * param_33;
  if (0.0001 < ABS(param_15)) {
    fVar29 = fVar29 * ((2.0 - (param_15 + 1.0)) * fStack_1e8 + fStack_1e4 * (param_15 + 1.0));
  }
  lVar24 = (longlong)(int)param_10;
  if (0 < (int)param_10) {
    if (((3 < param_10) && ((param_7 + lVar24 + -1 < param_3 || (param_3 + lVar24 + -1 < param_7))))
       && ((param_2 + lVar24 + -1 < param_3 || (param_3 + lVar24 + -1 < param_2)))) {
      pfVar12 = param_3;
      do {
        pauVar1 = (undefined1 (*) [16])(((longlong)param_7 - (longlong)param_3) + (longlong)pfVar12)
        ;
        auVar36 = *(undefined1 (*) [16])
                   ((longlong)pauVar1 + ((longlong)param_2 - (longlong)param_7));
        uVar9 = -(uint)(1.1754944e-38 <= auVar36._0_4_);
        uVar26 = -(uint)(1.1754944e-38 <= auVar36._4_4_);
        uVar27 = -(uint)(1.1754944e-38 <= auVar36._8_4_);
        uVar28 = -(uint)(1.1754944e-38 <= auVar36._12_4_);
        auVar2 = *pauVar1;
        auVar36 = divps(auVar2,auVar36);
        *pfVar12 = (float)(~-(uint)(auVar2._0_4_ < 1.1754944e-38) &
                          (auVar36._0_4_ & uVar9 | ~uVar9 & (uint)*pfVar12)) * fVar29;
        pfVar12[1] = (float)(~-(uint)(auVar2._4_4_ < 1.1754944e-38) &
                            (auVar36._4_4_ & uVar26 | ~uVar26 & (uint)pfVar12[1])) * fVar29;
        pfVar12[2] = (float)(~-(uint)(auVar2._8_4_ < 1.1754944e-38) &
                            (auVar36._8_4_ & uVar27 | ~uVar27 & (uint)pfVar12[2])) * fVar29;
        pfVar12[3] = (float)(~-(uint)(auVar2._12_4_ < 1.1754944e-38) &
                            (auVar36._12_4_ & uVar28 | ~uVar28 & (uint)pfVar12[3])) * fVar29;
        iVar18 = iVar18 + 4;
        pfVar12 = pfVar12 + 4;
      } while (iVar18 < (int)(param_10 & 0xfffffffc));
      if ((int)param_10 <= iVar18) goto LAB_181a066a0;
    }
    pfVar12 = param_3 + iVar18;
    uVar21 = (ulonglong)(param_10 - iVar18);
    do {
      pfVar16 = (float *)((longlong)pfVar12 + ((longlong)param_7 - (longlong)param_3));
      fVar33 = *(float *)((longlong)pfVar16 + ((longlong)param_2 - (longlong)param_7));
      if (fVar33 < 1.1754944e-38) {
        fVar33 = *pfVar12;
      }
      else {
        fVar33 = *pfVar16 / fVar33;
        *pfVar12 = fVar33;
      }
      if (*pfVar16 <= 1.1754944e-38 && *pfVar16 != 1.1754944e-38) {
        fVar33 = 0.0;
      }
      *pfVar12 = fVar29 * fVar33;
      pfVar12 = pfVar12 + 1;
      uVar21 = uVar21 - 1;
    } while (uVar21 != 0);
  }
LAB_181a066a0:
  if (lVar3 != 0) {
    FUN_1815ef980(lVar3);
  }
  return;
}


