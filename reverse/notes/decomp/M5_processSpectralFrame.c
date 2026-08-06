
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_1819f9970(longlong param_1,char param_2,char param_3,char param_4,undefined4 param_5,
                  float param_6,char param_7,ulonglong *param_8,undefined8 param_9,
                  undefined8 param_10,undefined8 param_11,int param_12,int param_13,
                  undefined4 param_14,undefined4 param_15,undefined4 param_16,double param_17,
                  undefined8 param_18,int *param_19,char *param_20,char *param_21,char *param_22,
                  char *param_23)

{
  undefined8 *puVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  char cVar5;
  uint uVar6;
  longlong *plVar7;
  longlong lVar8;
  int iVar9;
  longlong lVar10;
  ulonglong uVar11;
  int iVar12;
  uint uVar13;
  int iVar14;
  longlong lVar15;
  int iVar16;
  longlong lVar17;
  undefined8 *puVar18;
  float *pfVar19;
  uint uVar20;
  int iVar21;
  ulonglong uVar22;
  ulonglong uVar23;
  uint uVar24;
  longlong lVar25;
  ulonglong uVar26;
  longlong lVar27;
  longlong lVar28;
  longlong lVar29;
  float fVar30;
  float fVar31;
  undefined1 auVar32 [16];
  float fVar33;
  float fVar34;
  float fVar35;
  float fVar36;
  int iStackX_10;
  ulonglong uStack_118;
  undefined1 uStack_110;
  longlong lStack_108;
  longlong lStack_100;
  ulonglong uStack_f8;
  char cStack_f0;
  
  lVar29 = DAT_18368af08;
  iStackX_10 = param_12 + param_13;
  uVar23 = 0;
  if ((param_2 == '\0') && (lVar10 = _DAT_18368af00, param_4 == '\0')) goto LAB_1819fb131;
  lStack_100 = DAT_18368af08;
  if (0 < *(int *)(param_1 + 0x168)) {
    uVar22 = uVar23;
    uVar26 = uVar23;
    do {
      pfVar19 = *(float **)(uVar22 + *(longlong *)(param_1 + 0x1e8));
      fVar30 = 0.0;
      for (iVar16 = param_12; iVar16 != 0; iVar16 = iVar16 + -1) {
        *pfVar19 = *(float *)(lVar29 + (longlong)(int)fVar30 * 4) * *pfVar19;
        pfVar19 = pfVar19 + 1;
        fVar30 = fVar30 + 8192.0 / (float)param_12;
      }
      pfVar19 = (float *)(*(longlong *)(uVar22 + *(longlong *)(param_1 + 0x1e8)) +
                         (longlong)param_12 * 4);
      fVar30 = 0.0;
      for (iVar16 = param_13; iVar16 != 0; iVar16 = iVar16 + -1) {
        *pfVar19 = *(float *)(lVar29 + 0x8000 + (longlong)(int)fVar30 * 4) * *pfVar19;
        pfVar19 = pfVar19 + 1;
        fVar30 = fVar30 + 8192.0 / (float)param_13;
      }
      uVar24 = (int)uVar26 + 1;
      uVar26 = (ulonglong)uVar24;
      uVar22 = uVar22 + 8;
    } while ((int)uVar24 < *(int *)(param_1 + 0x168));
  }
  if (param_2 != '\0') {
    iVar16 = 0x10;
    if (0x10 < iStackX_10) {
      do {
        iVar16 = iVar16 * 2;
      } while (iVar16 < iStackX_10);
      if (0x1000 < iVar16) {
        iVar16 = 0x1000;
      }
    }
    if (iVar16 != *param_19) {
      plVar7 = (longlong *)FUN_1819f70b0(param_1,&uStack_f8,iVar16);
      if (*(longlong *)(param_1 + 0x1b8) != *plVar7) {
        if ((char)plVar7[1] == '\0') {
          if (*plVar7 != 0) {
            FUN_1815f0610();
          }
        }
        else {
          *(undefined1 *)(plVar7 + 1) = 0;
        }
        lVar29 = *(longlong *)(param_1 + 0x1b8);
        *(longlong *)(param_1 + 0x1b8) = *plVar7;
        if (lVar29 != 0) {
          FUN_1815ef980();
        }
      }
      if ((cStack_f0 != '\0') && (uStack_f8 != 0)) {
        FUN_1815ef980();
      }
      *param_19 = iVar16;
    }
    uVar24 = iVar16 / 2;
    lVar29 = (longlong)(int)uVar24;
    iVar14 = *(int *)(param_1 + 0x16c);
    uVar22 = uVar23;
    uVar26 = uVar23;
    if (0 < *(int *)(param_1 + 0x168)) {
      do {
        FUN_1816f6ae0(*(undefined8 *)(param_1 + 0x1b8),
                      *(undefined8 *)(uVar26 + *(longlong *)(param_1 + 0x1f0)),
                      *(undefined8 *)(uVar26 + *(longlong *)(param_1 + 0x1e8)));
        uVar13 = (int)uVar22 + 1;
        uVar22 = (ulonglong)uVar13;
        uVar26 = uVar26 + 8;
      } while ((int)uVar13 < *(int *)(param_1 + 0x168));
    }
    lStack_108 = lVar29;
    memset(*(undefined8 *)(param_1 + 0x1c0),0,lVar29 * 4);
    iVar12 = *(int *)(param_1 + 0x168);
    uVar22 = uVar23;
    if (0 < iVar12) {
      do {
        lVar10 = *(longlong *)(uVar22 + *(longlong *)(param_1 + 0x1f0));
        lVar17 = 1;
        if (1 < lVar29) {
          if (4 < lVar29) {
            pfVar19 = (float *)(lVar10 + 0x1c);
            do {
              fVar36 = ABS(pfVar19[-5]);
              fVar35 = ABS(pfVar19[-4]);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              *(float *)(*(longlong *)(param_1 + 0x1c0) + lVar17 * 4) =
                   fVar30 + *(float *)(*(longlong *)(param_1 + 0x1c0) + lVar17 * 4);
              fVar36 = ABS(pfVar19[-3]);
              fVar35 = ABS(pfVar19[-2]);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              *(float *)(*(longlong *)(param_1 + 0x1c0) + 4 + lVar17 * 4) =
                   fVar30 + *(float *)(*(longlong *)(param_1 + 0x1c0) + 4 + lVar17 * 4);
              fVar36 = ABS(pfVar19[-1]);
              fVar35 = ABS(*pfVar19);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              *(float *)(*(longlong *)(param_1 + 0x1c0) + 8 + lVar17 * 4) =
                   fVar30 + *(float *)(*(longlong *)(param_1 + 0x1c0) + 8 + lVar17 * 4);
              fVar36 = ABS(pfVar19[1]);
              fVar35 = ABS(pfVar19[2]);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              *(float *)(*(longlong *)(param_1 + 0x1c0) + 0xc + lVar17 * 4) =
                   fVar30 + *(float *)(*(longlong *)(param_1 + 0x1c0) + 0xc + lVar17 * 4);
              lVar17 = lVar17 + 4;
              pfVar19 = pfVar19 + 8;
            } while (lVar17 < lVar29 + -3);
            if (lVar29 <= lVar17) goto LAB_1819f9fc3;
          }
          do {
            fVar36 = ABS(*(float *)(lVar10 + lVar17 * 8));
            fVar35 = ABS(*(float *)(lVar10 + 4 + lVar17 * 8));
            fVar30 = fVar35;
            if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
              if (fVar36 <= fVar35) {
                fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                if (fVar30 < 0.0) {
                  fVar30 = (float)sqrtf();
                }
                else {
                  fVar30 = SQRT(fVar30);
                }
                fVar30 = fVar30 * fVar35;
              }
              else {
                fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                if (fVar30 < 0.0) {
                  fVar30 = (float)sqrtf();
                  fVar30 = fVar30 * fVar36;
                }
                else {
                  fVar30 = SQRT(fVar30) * fVar36;
                }
              }
            }
            *(float *)(*(longlong *)(param_1 + 0x1c0) + lVar17 * 4) =
                 fVar30 + *(float *)(*(longlong *)(param_1 + 0x1c0) + lVar17 * 4);
            lVar17 = lVar17 + 1;
          } while (lVar17 < lVar29);
        }
LAB_1819f9fc3:
        uVar13 = (int)uVar23 + 1;
        uVar23 = (ulonglong)uVar13;
        iVar12 = *(int *)(param_1 + 0x168);
        uVar22 = uVar22 + 8;
      } while ((int)uVar13 < iVar12);
    }
    uVar23 = 0;
    if (1 < iVar12) {
      (**(code **)(DAT_18357ff90 + 0x20))
                (*(undefined8 *)(param_1 + 0x1c0),1.0 / (float)iVar12,uVar24);
    }
    uVar13 = 0;
    uVar22 = uVar23;
    if ((0 < (int)uVar24) && (uVar22 = 0, 3 < uVar24)) {
      puVar1 = (undefined8 *)(param_1 + 0x1c8);
      puVar18 = (undefined8 *)*puVar1;
      if ((puVar1 < puVar18) ||
         ((undefined8 *)((longlong)puVar18 + (longlong)(int)(uVar24 - 1) * 4) < puVar1)) {
        uVar6 = uVar24 & 0x80000003;
        if ((int)uVar6 < 0) {
          uVar6 = (uVar6 - 1 | 0xfffffffc) + 1;
        }
        uVar22 = uVar23;
        do {
          uVar20 = (int)uVar22 + 4;
          uVar22 = (ulonglong)uVar20;
        } while ((int)uVar20 < (int)(uVar24 - uVar6));
        iVar12 = (uVar24 - uVar6) + 3;
        for (lVar10 = ((longlong)((int)(iVar12 + (iVar12 >> 0x1f & 3U)) >> 2) & 0xfffffffffffffffU)
                      << 2; lVar10 != 0; lVar10 = lVar10 + -1) {
          *(undefined4 *)puVar18 = 0x3f800000;
          puVar18 = (undefined8 *)((longlong)puVar18 + 4);
        }
      }
    }
    lVar10 = (longlong)(int)uVar22;
    if (lVar10 < lVar29) {
      plVar7 = (longlong *)(param_1 + 0x1c8);
      if (3 < lVar29 - lVar10) {
        do {
          *(undefined4 *)(*plVar7 + lVar10 * 4) = 0x3f800000;
          *(undefined4 *)(*plVar7 + 4 + lVar10 * 4) = 0x3f800000;
          *(undefined4 *)(*plVar7 + 8 + lVar10 * 4) = 0x3f800000;
          *(undefined4 *)(*plVar7 + 0xc + lVar10 * 4) = 0x3f800000;
          lVar10 = lVar10 + 4;
        } while (lVar10 < lVar29 + -3);
        if (lVar29 <= lVar10) goto LAB_1819fa0eb;
      }
      do {
        *(undefined4 *)(*plVar7 + lVar10 * 4) = 0x3f800000;
        lVar10 = lVar10 + 1;
      } while (lVar10 < lVar29);
    }
LAB_1819fa0eb:
    uStack_f8 = 0;
    if (((param_7 == '\0') || (*param_8 == 0)) && (param_3 == '\0')) goto LAB_1819fa34b;
    uVar22 = uVar23;
    if (*(longlong *)(param_1 + 0x2c0) != 0) {
      uVar26 = *param_8;
      cVar5 = FUN_181629ca0();
      if (cVar5 != '\0') {
        uVar26 = *param_8;
        lVar10 = FUN_1815f2440(uVar26);
        if (lVar10 != 0) {
          uVar26 = *(ulonglong *)(uVar26 + 0x20 + (ulonglong)(*(uint *)(lVar10 + 0x154) & 1) * 8);
        }
      }
      if (0.0 < *(float *)(uVar26 + 0xbc)) {
        uVar22 = *(ulonglong *)(param_1 + 0x178);
        uStack_f8 = uVar22;
      }
    }
    uStack_118 = *param_8;
    uStack_110 = 0;
    MULSS_prepareAndRenderSpectralBlock
              (param_1,&uStack_118,param_14,param_15,param_16,param_12,param_13,uVar24,
               ((float)param_17 / (float)iVar14) / (float)iVar16,param_18,param_5,param_9,param_10,
               param_11,uVar22);
    uVar26 = *param_8;
    if (uVar26 == 0) {
LAB_1819fa269:
      if (param_3 != '\0') goto LAB_1819fa475;
      if ((0 < (int)uVar24) && (3 < uVar24)) {
        puVar1 = (undefined8 *)(param_1 + 0x1c8);
        puVar18 = (undefined8 *)*puVar1;
        if ((puVar1 < puVar18) ||
           ((undefined8 *)((longlong)puVar18 + (longlong)(int)(uVar24 - 1) * 4) < puVar1)) {
          uVar6 = uVar24 & 0x80000003;
          if ((int)uVar6 < 0) {
            uVar6 = (uVar6 - 1 | 0xfffffffc) + 1;
          }
          uVar22 = uVar23;
          do {
            uVar13 = (int)uVar22 + 4;
            uVar22 = (ulonglong)uVar13;
          } while ((int)uVar13 < (int)(uVar24 - uVar6));
          iVar16 = (uVar24 - uVar6) + 3;
          for (lVar10 = ((longlong)((int)(iVar16 + (iVar16 >> 0x1f & 3U)) >> 2) & 0xfffffffffffffffU
                        ) << 2; lVar10 != 0; lVar10 = lVar10 + -1) {
            *(undefined4 *)puVar18 = 0x3f800000;
            puVar18 = (undefined8 *)((longlong)puVar18 + 4);
          }
        }
      }
      lVar10 = (longlong)(int)uVar13;
      if (lVar10 < lVar29) {
        plVar7 = (longlong *)(param_1 + 0x1c8);
        if (3 < lVar29 - lVar10) {
          do {
            *(undefined4 *)(*plVar7 + lVar10 * 4) = 0x3f800000;
            *(undefined4 *)(*plVar7 + 4 + lVar10 * 4) = 0x3f800000;
            *(undefined4 *)(*plVar7 + 8 + lVar10 * 4) = 0x3f800000;
            *(undefined4 *)(*plVar7 + 0xc + lVar10 * 4) = 0x3f800000;
            lVar10 = lVar10 + 4;
          } while (lVar10 < lVar29 + -3);
          if (lVar29 <= lVar10) goto LAB_1819fa34b;
        }
        do {
          *(undefined4 *)(*plVar7 + lVar10 * 4) = 0x3f800000;
          lVar10 = lVar10 + 1;
        } while (lVar10 < lVar29);
      }
LAB_1819fa34b:
      iVar16 = *(int *)(param_1 + 0x168);
      uVar22 = uVar23;
      uVar26 = uVar23;
      if (0 < iVar16) {
        do {
          plVar7 = (longlong *)(param_1 + 0x1c8);
          lVar10 = *(longlong *)(*(longlong *)(param_1 + 0x1f0) + uVar22);
          uVar11 = uVar23;
          if (3 < lVar29) {
            pfVar19 = (float *)(lVar10 + 8);
            do {
              pfVar19[-2] = *(float *)(*plVar7 + uVar11 * 4) * pfVar19[-2];
              pfVar19[-1] = *(float *)(*plVar7 + uVar11 * 4) * pfVar19[-1];
              *pfVar19 = *(float *)(*plVar7 + 4 + uVar11 * 4) * *pfVar19;
              pfVar19[1] = *(float *)(*plVar7 + 4 + uVar11 * 4) * pfVar19[1];
              pfVar19[2] = *(float *)(*plVar7 + 8 + uVar11 * 4) * pfVar19[2];
              pfVar19[3] = *(float *)(*plVar7 + 8 + uVar11 * 4) * pfVar19[3];
              pfVar19[4] = *(float *)(*plVar7 + 0xc + uVar11 * 4) * pfVar19[4];
              pfVar19[5] = *(float *)(*plVar7 + 0xc + uVar11 * 4) * pfVar19[5];
              uVar11 = uVar11 + 4;
              pfVar19 = pfVar19 + 8;
            } while ((longlong)uVar11 < lVar29 + -3);
          }
          for (; (longlong)uVar11 < lVar29; uVar11 = uVar11 + 1) {
            *(float *)(lVar10 + uVar11 * 8) =
                 *(float *)(*plVar7 + uVar11 * 4) * *(float *)(lVar10 + uVar11 * 8);
            *(float *)(lVar10 + 4 + uVar11 * 8) =
                 *(float *)(*plVar7 + uVar11 * 4) * *(float *)(lVar10 + 4 + uVar11 * 8);
          }
          uVar24 = (int)uVar26 + 1;
          iVar16 = *(int *)(param_1 + 0x168);
          uVar22 = uVar22 + 8;
          uVar26 = (ulonglong)uVar24;
        } while ((int)uVar24 < iVar16);
      }
    }
    else {
      cVar5 = FUN_181629ca0();
      if (cVar5 != '\0') {
        uVar26 = *param_8;
        lVar10 = FUN_1815f2440(uVar26);
        if (lVar10 != 0) {
          uVar26 = *(ulonglong *)(uVar26 + 0x20 + (ulonglong)(*(uint *)(lVar10 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(uVar26 + 0x78) == '\0') goto LAB_1819fa269;
LAB_1819fa475:
      if (uVar22 == 0) goto LAB_1819fa34b;
      uStack_118 = uStack_118 & 0xffffffff00000000;
      iVar16 = *(int *)(param_1 + 0x168);
      lVar29 = lStack_108;
      uVar26 = uVar23;
      if (0 < iVar16) {
        do {
          plVar7 = (longlong *)(param_1 + 0x1c8);
          lVar10 = *(longlong *)(uVar26 + *(longlong *)(param_1 + 0x1f0));
          lVar17 = 0;
          lVar15 = *(longlong *)(uVar26 + uVar22) - lVar10;
          iVar16 = (int)uVar23;
          if (3 < lVar29) {
            pfVar19 = (float *)(lVar10 + 0x14);
            do {
              fVar36 = ABS(pfVar19[-5]);
              fVar35 = ABS(pfVar19[-4]);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              fVar30 = fVar30 * *(float *)(*plVar7 + lVar17 * 4);
              pfVar19[-5] = fVar30 * *(float *)(lVar15 + -0x14 + (longlong)pfVar19);
              pfVar19[-4] = -(fVar30 * *(float *)(lVar15 + -0x10 + (longlong)pfVar19));
              fVar36 = ABS(pfVar19[-3]);
              fVar35 = ABS(pfVar19[-2]);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              fVar30 = fVar30 * *(float *)(*plVar7 + 4 + lVar17 * 4);
              pfVar19[-3] = fVar30 * *(float *)(lVar15 + -0xc + (longlong)pfVar19);
              pfVar19[-2] = -(fVar30 * *(float *)(lVar15 + -8 + (longlong)pfVar19));
              fVar36 = ABS(pfVar19[-1]);
              fVar35 = ABS(*pfVar19);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              fVar30 = fVar30 * *(float *)(*plVar7 + 8 + lVar17 * 4);
              pfVar19[-1] = fVar30 * *(float *)(lVar15 + -4 + (longlong)pfVar19);
              *pfVar19 = -(fVar30 * *(float *)((longlong)pfVar19 + lVar15));
              fVar36 = ABS(pfVar19[1]);
              fVar35 = ABS(pfVar19[2]);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              fVar30 = fVar30 * *(float *)(*plVar7 + 0xc + lVar17 * 4);
              pfVar19[1] = fVar30 * *(float *)(lVar15 + 4 + (longlong)pfVar19);
              pfVar19[2] = -(fVar30 * *(float *)(lVar15 + 8 + (longlong)pfVar19));
              lVar17 = lVar17 + 4;
              pfVar19 = pfVar19 + 8;
            } while (lVar17 < lVar29 + -3);
            lVar29 = lStack_108;
            iVar16 = (int)uStack_118;
          }
          if (lVar17 < lVar29) {
            pfVar19 = (float *)(lVar17 * 8 + 4 + lVar10);
            do {
              fVar36 = ABS(pfVar19[-1]);
              fVar35 = ABS(*pfVar19);
              fVar30 = fVar35;
              if ((fVar36 != 0.0) && (fVar30 = fVar36, fVar35 != 0.0)) {
                if (fVar36 <= fVar35) {
                  fVar30 = (fVar36 / fVar35) * (fVar36 / fVar35) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    lVar29 = lStack_108;
                  }
                  else {
                    fVar30 = SQRT(fVar30);
                  }
                  fVar30 = fVar30 * fVar35;
                }
                else {
                  fVar30 = (fVar35 / fVar36) * (fVar35 / fVar36) + 1.0;
                  if (fVar30 < 0.0) {
                    fVar30 = (float)sqrtf();
                    lVar29 = lStack_108;
                    fVar30 = fVar30 * fVar36;
                  }
                  else {
                    fVar30 = SQRT(fVar30) * fVar36;
                  }
                }
              }
              fVar30 = fVar30 * *(float *)(*plVar7 + lVar17 * 4);
              pfVar19[-1] = fVar30 * *(float *)((longlong)pfVar19 + lVar15 + -4);
              *pfVar19 = -(fVar30 * *(float *)((longlong)pfVar19 + lVar15));
              lVar17 = lVar17 + 1;
              pfVar19 = pfVar19 + 2;
            } while (lVar17 < lVar29);
          }
          uVar24 = iVar16 + 1;
          uVar23 = (ulonglong)uVar24;
          uStack_118 = CONCAT44(uStack_118._4_4_,uVar24);
          iVar16 = *(int *)(param_1 + 0x168);
          uVar22 = uStack_f8;
          uVar26 = uVar26 + 8;
        } while ((int)uVar24 < iVar16);
      }
    }
    uVar22 = 0;
    uVar23 = uVar22;
    if (0 < iVar16) {
      do {
        FUN_1816f6bd0(*(undefined8 *)(param_1 + 0x1b8),
                      *(undefined8 *)(*(longlong *)(param_1 + 0x1f0) + uVar23),
                      *(undefined8 *)(*(longlong *)(param_1 + 0x1e8) + uVar23));
        uVar24 = (int)uVar22 + 1;
        uVar22 = (ulonglong)uVar24;
        uVar23 = uVar23 + 8;
      } while ((int)uVar24 < *(int *)(param_1 + 0x168));
    }
  }
  iVar16 = 0;
  lVar10 = lStack_100;
  if (param_4 != '\0') {
    lVar29 = *(longlong *)(param_1 + 0x1d0);
    fVar30 = 1.0 / param_6;
    fVar4 = (float)(*(int *)(param_1 + 0x164) + -2);
    fVar35 = (float)param_12;
    fVar36 = fVar35 - fVar35 * param_6;
    if (1.0 <= param_6) {
      iVar14 = 0;
    }
    else {
      iVar14 = (int)fVar36 + 1;
      iStackX_10 = (int)((float)param_13 * param_6 + fVar35) + 1;
      *param_23 = '\x01';
      *param_22 = '\x01';
    }
    lVar17 = (longlong)iStackX_10;
    if (0 < *(int *)(param_1 + 0x168)) {
      lVar15 = (longlong)iVar14;
      lVar28 = 0;
      do {
        lVar10 = *(longlong *)(*(longlong *)(param_1 + 0x1e8) + lVar28);
        memset(lVar29 + -4,0,(longlong)(*(int *)(param_1 + 0x164) + 4) << 2);
        memcpy(lVar29,lVar10,(longlong)*(int *)(param_1 + 0x164) << 2);
        memset(lVar10,0,(longlong)*(int *)(param_1 + 0x164) << 2);
        if (*(char *)(param_1 + 0x261) == '\0') {
          if (lVar15 < lVar17) {
            lVar27 = lVar15;
            iVar12 = iVar14;
            if (3 < lVar17 - lVar15) {
              iVar21 = iVar14 + 2;
              pfVar19 = (float *)(lVar10 + (lVar15 + 2) * 4);
              lVar25 = ((lVar17 - lVar15) - 4U >> 2) + 1;
              lVar27 = lVar15 + lVar25 * 4;
              do {
                fVar35 = ((float)iVar12 - fVar36) * fVar30;
                if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                  lVar8 = (longlong)(int)fVar35;
                  pfVar19[-2] = (*(float *)(lVar29 + 4 + lVar8 * 4) - *(float *)(lVar29 + lVar8 * 4)
                                ) * (fVar35 - (float)(int)fVar35) + *(float *)(lVar29 + lVar8 * 4);
                }
                fVar35 = ((float)(iVar21 + -1) - fVar36) * fVar30;
                if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                  lVar8 = (longlong)(int)fVar35;
                  pfVar19[-1] = (*(float *)(lVar29 + 4 + lVar8 * 4) - *(float *)(lVar29 + lVar8 * 4)
                                ) * (fVar35 - (float)(int)fVar35) + *(float *)(lVar29 + lVar8 * 4);
                }
                fVar35 = ((float)iVar21 - fVar36) * fVar30;
                if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                  lVar8 = (longlong)(int)fVar35;
                  *pfVar19 = (*(float *)(lVar29 + 4 + lVar8 * 4) - *(float *)(lVar29 + lVar8 * 4)) *
                             (fVar35 - (float)(int)fVar35) + *(float *)(lVar29 + lVar8 * 4);
                }
                fVar35 = ((float)(iVar21 + 1) - fVar36) * fVar30;
                if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                  lVar8 = (longlong)(int)fVar35;
                  pfVar19[1] = (*(float *)(lVar29 + 4 + lVar8 * 4) - *(float *)(lVar29 + lVar8 * 4))
                               * (fVar35 - (float)(int)fVar35) + *(float *)(lVar29 + lVar8 * 4);
                }
                iVar12 = iVar12 + 4;
                iVar21 = iVar21 + 4;
                pfVar19 = pfVar19 + 4;
                lVar25 = lVar25 + -1;
              } while (lVar25 != 0);
              if (lVar17 <= lVar27) goto LAB_1819fb088;
            }
            do {
              fVar35 = ((float)iVar12 - fVar36) * fVar30;
              if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                lVar25 = (longlong)(int)fVar35;
                *(float *)(lVar10 + lVar27 * 4) =
                     (*(float *)(lVar29 + 4 + lVar25 * 4) - *(float *)(lVar29 + lVar25 * 4)) *
                     (fVar35 - (float)(int)fVar35) + *(float *)(lVar29 + lVar25 * 4);
              }
              iVar12 = iVar12 + 1;
              lVar27 = lVar27 + 1;
            } while (lVar27 < lVar17);
          }
        }
        else if (lVar15 < lVar17) {
          lVar27 = lVar15;
          iVar12 = iVar14;
          if (3 < lVar17 - lVar15) {
            iVar21 = iVar14 + 2;
            pfVar19 = (float *)(lVar10 + (lVar15 + 2) * 4);
            lVar25 = ((lVar17 - lVar15) - 4U >> 2) + 1;
            lVar27 = lVar15 + lVar25 * 4;
            do {
              fVar35 = ((float)iVar12 - fVar36) * fVar30;
              if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                iVar9 = (int)fVar35;
                fVar35 = fVar35 - (float)iVar9;
                lVar8 = (longlong)iVar9;
                fVar2 = *(float *)(lVar29 + -4 + lVar8 * 4);
                fVar3 = *(float *)(lVar29 + lVar8 * 4);
                fVar34 = *(float *)(lVar29 + 4 + lVar8 * 4);
                fVar33 = (*(float *)(lVar29 + (longlong)(iVar9 + 2) * 4) - fVar2) * 0.16666667;
                fVar31 = (fVar3 - fVar34) * 0.5;
                fVar34 = (fVar34 + fVar2) * 0.5;
                pfVar19[-2] = (((fVar31 + fVar33) * fVar35 + (fVar34 - fVar3)) * fVar35 +
                              (((fVar34 - fVar33) - fVar31) - fVar2)) * fVar35 + fVar3;
              }
              fVar35 = ((float)(iVar21 + -1) - fVar36) * fVar30;
              if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                iVar9 = (int)fVar35;
                fVar35 = fVar35 - (float)iVar9;
                lVar8 = (longlong)iVar9;
                fVar2 = *(float *)(lVar29 + -4 + lVar8 * 4);
                fVar3 = *(float *)(lVar29 + lVar8 * 4);
                fVar34 = *(float *)(lVar29 + 4 + lVar8 * 4);
                fVar33 = (*(float *)(lVar29 + (longlong)(iVar9 + 2) * 4) - fVar2) * 0.16666667;
                fVar31 = (fVar3 - fVar34) * 0.5;
                fVar34 = (fVar34 + fVar2) * 0.5;
                pfVar19[-1] = (((fVar31 + fVar33) * fVar35 + (fVar34 - fVar3)) * fVar35 +
                              (((fVar34 - fVar33) - fVar31) - fVar2)) * fVar35 + fVar3;
              }
              fVar35 = ((float)iVar21 - fVar36) * fVar30;
              if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                iVar9 = (int)fVar35;
                fVar35 = fVar35 - (float)iVar9;
                lVar8 = (longlong)iVar9;
                fVar2 = *(float *)(lVar29 + -4 + lVar8 * 4);
                fVar3 = *(float *)(lVar29 + lVar8 * 4);
                fVar34 = *(float *)(lVar29 + 4 + lVar8 * 4);
                fVar33 = (*(float *)(lVar29 + (longlong)(iVar9 + 2) * 4) - fVar2) * 0.16666667;
                fVar31 = (fVar3 - fVar34) * 0.5;
                fVar34 = (fVar34 + fVar2) * 0.5;
                *pfVar19 = (((fVar31 + fVar33) * fVar35 + (fVar34 - fVar3)) * fVar35 +
                           (((fVar34 - fVar33) - fVar31) - fVar2)) * fVar35 + fVar3;
              }
              fVar35 = ((float)(iVar21 + 1) - fVar36) * fVar30;
              if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
                iVar9 = (int)fVar35;
                fVar35 = fVar35 - (float)iVar9;
                lVar8 = (longlong)iVar9;
                fVar2 = *(float *)(lVar29 + -4 + lVar8 * 4);
                fVar3 = *(float *)(lVar29 + lVar8 * 4);
                fVar34 = *(float *)(lVar29 + 4 + lVar8 * 4);
                fVar33 = (*(float *)(lVar29 + (longlong)(iVar9 + 2) * 4) - fVar2) * 0.16666667;
                fVar31 = (fVar3 - fVar34) * 0.5;
                fVar34 = (fVar34 + fVar2) * 0.5;
                pfVar19[1] = (((fVar31 + fVar33) * fVar35 + (fVar34 - fVar3)) * fVar35 +
                             (((fVar34 - fVar33) - fVar31) - fVar2)) * fVar35 + fVar3;
              }
              iVar12 = iVar12 + 4;
              iVar21 = iVar21 + 4;
              pfVar19 = pfVar19 + 4;
              lVar25 = lVar25 + -1;
            } while (lVar25 != 0);
            if (lVar17 <= lVar27) goto LAB_1819fb088;
          }
          do {
            fVar35 = ((float)iVar12 - fVar36) * fVar30;
            if ((0.0 <= fVar35) && (fVar35 <= fVar4)) {
              iVar21 = (int)fVar35;
              fVar35 = fVar35 - (float)iVar21;
              lVar25 = (longlong)iVar21;
              fVar2 = *(float *)(lVar29 + -4 + lVar25 * 4);
              fVar3 = *(float *)(lVar29 + lVar25 * 4);
              fVar34 = *(float *)(lVar29 + 4 + lVar25 * 4);
              fVar33 = (*(float *)(lVar29 + (longlong)(iVar21 + 2) * 4) - fVar2) * 0.16666667;
              fVar31 = (fVar3 - fVar34) * 0.5;
              fVar34 = (fVar34 + fVar2) * 0.5;
              *(float *)(lVar10 + lVar27 * 4) =
                   (((fVar31 + fVar33) * fVar35 + (fVar34 - fVar3)) * fVar35 +
                   (((fVar34 - fVar33) - fVar31) - fVar2)) * fVar35 + fVar3;
            }
            iVar12 = iVar12 + 1;
            lVar27 = lVar27 + 1;
          } while (lVar27 < lVar17);
        }
LAB_1819fb088:
        if (param_6 < 1.0) {
          iVar12 = iStackX_10 - iVar14;
          pfVar19 = (float *)(lVar10 + lVar15 * 4);
          fVar35 = (float)iVar12;
          auVar32 = ZEXT816(0);
          for (; iVar12 != 0; iVar12 = iVar12 + -1) {
            *pfVar19 = *(float *)(lStack_100 + (longlong)(int)auVar32._0_4_ * 4) * *pfVar19;
            pfVar19 = pfVar19 + 1;
            auVar32._0_4_ = auVar32._0_4_ + 16384.0 / fVar35;
          }
        }
        iVar16 = iVar16 + 1;
        lVar28 = lVar28 + 8;
        lVar10 = lStack_100;
      } while (iVar16 < *(int *)(param_1 + 0x168));
    }
  }
LAB_1819fb131:
  uVar23 = 0;
  if (((*param_20 != '\0') && (*param_22 == '\0')) && (0 < *(int *)(param_1 + 0x168))) {
    uVar22 = uVar23;
    uVar26 = uVar23;
    do {
      pfVar19 = *(float **)(*(longlong *)(param_1 + 0x1e8) + uVar26);
      fVar30 = 0.0;
      for (iVar16 = param_12; iVar16 != 0; iVar16 = iVar16 + -1) {
        *pfVar19 = *(float *)(lVar10 + (longlong)(int)fVar30 * 4) * *pfVar19;
        pfVar19 = pfVar19 + 1;
        fVar30 = fVar30 + 8192.0 / (float)param_12;
      }
      uVar24 = (int)uVar22 + 1;
      uVar22 = (ulonglong)uVar24;
      uVar26 = uVar26 + 8;
    } while ((int)uVar24 < *(int *)(param_1 + 0x168));
  }
  if ((*param_21 != '\0') && (*param_23 == '\0')) {
    if (0 < *(int *)(param_1 + 0x168)) {
      uVar22 = uVar23;
      do {
        pfVar19 = (float *)(*(longlong *)(*(longlong *)(param_1 + 0x1e8) + uVar23) +
                           (longlong)param_12 * 4);
        fVar30 = 0.0;
        for (iVar16 = param_13; iVar16 != 0; iVar16 = iVar16 + -1) {
          *pfVar19 = *(float *)(lVar10 + 0x8000 + (longlong)(int)fVar30 * 4) * *pfVar19;
          pfVar19 = pfVar19 + 1;
          fVar30 = fVar30 + 8192.0 / (float)param_13;
        }
        uVar24 = (int)uVar22 + 1;
        uVar22 = (ulonglong)uVar24;
        uVar23 = uVar23 + 8;
      } while ((int)uVar24 < *(int *)(param_1 + 0x168));
    }
    *param_23 = '\x01';
  }
  if (((char)param_8[1] != '\0') && (*param_8 != 0)) {
    FUN_1815ef980();
  }
  return;
}


