
/* MULSS block dispatcher: gathers independent pitch/formant/amplitude/noise/sibilant functions,
   source-time position and attack state before spectral reconstruction. Default controls are
   neutral ratios 1.0; component paths are optional and evaluated per block. */

void MULSS_prepareAndRenderSpectralBlock
               (longlong param_1,longlong *param_2,undefined4 param_3,undefined4 param_4,
               undefined4 param_5,int param_6,int param_7,undefined4 param_8,undefined4 param_9,
               double param_10,float param_11,longlong param_12,undefined8 param_13,
               longlong param_14,longlong param_15)

{
  float fVar1;
  char cVar2;
  char cVar3;
  char cVar4;
  char cVar5;
  uint uVar6;
  longlong lVar7;
  longlong lVar8;
  longlong *plVar9;
  longlong lVar10;
  longlong lVar11;
  longlong lVar12;
  float *pfVar13;
  ulonglong uVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  bool bVar18;
  undefined4 uVar19;
  float fVar20;
  double dVar21;
  float fVar22;
  float fVar23;
  float fVar24;
  float fVar25;
  float fVar26;
  undefined8 in_stack_fffffffffffffd58;
  undefined4 uVar27;
  undefined8 in_stack_fffffffffffffd60;
  undefined4 uVar28;
  undefined8 in_stack_fffffffffffffd68;
  undefined4 uVar29;
  undefined8 in_stack_fffffffffffffd70;
  undefined4 uVar30;
  undefined8 in_stack_fffffffffffffd78;
  undefined4 uVar31;
  undefined8 in_stack_fffffffffffffd80;
  undefined4 uVar32;
  undefined1 uStack_198;
  float fStack_194;
  float fStack_190;
  longlong lStack_188;
  bool bStack_180;
  undefined4 uStack_178;
  undefined4 uStack_174;
  float fStack_170;
  undefined4 uStack_16c;
  undefined4 uStack_168;
  undefined4 uStack_164;
  undefined8 uStack_160;
  undefined8 uStack_158;
  undefined8 uStack_150;
  undefined8 uStack_148;
  undefined8 uStack_140;
  undefined8 uStack_138;
  undefined8 uStack_130;
  undefined8 uStack_128;
  undefined8 uStack_120;
  undefined8 uStack_118;
  undefined8 uStack_110;
  undefined8 uStack_108;
  undefined8 uStack_100;
  longlong lStack_f8;
  char cStack_f0;
  
  uVar27 = (undefined4)((ulonglong)in_stack_fffffffffffffd58 >> 0x20);
  uVar28 = (undefined4)((ulonglong)in_stack_fffffffffffffd60 >> 0x20);
  uVar29 = (undefined4)((ulonglong)in_stack_fffffffffffffd68 >> 0x20);
  uVar30 = (undefined4)((ulonglong)in_stack_fffffffffffffd70 >> 0x20);
  uVar31 = (undefined4)((ulonglong)in_stack_fffffffffffffd78 >> 0x20);
  uVar32 = (undefined4)((ulonglong)in_stack_fffffffffffffd80 >> 0x20);
  uStack_178 = 0;
  uStack_100 = 0;
  uStack_138 = 0;
  uStack_140 = 0;
  uStack_148 = 0;
  uStack_150 = 0;
  uStack_108 = 0;
  uStack_110 = 0;
  uStack_158 = 0;
  uStack_118 = 0;
  uStack_120 = 0;
  uStack_128 = 0;
  uStack_130 = 0;
  uStack_160 = 0;
  uVar19 = 0x3f800000;
  fStack_190 = 1.0;
  fVar24 = 1.0;
  fStack_194 = 1.0;
  fVar26 = 0.0;
  fVar23 = 0.0;
  fStack_170 = 0.0;
  uStack_174 = 0x3f800000;
  fVar25 = 0.0;
  uStack_16c = 0x41ac4396;
  uStack_164 = 0x42c80000;
  uStack_168 = 0x42c80000;
  uStack_198 = 0;
  lVar8 = *param_2;
  if (lVar8 != 0) {
    cVar2 = FUN_181629ca0();
    if (cVar2 != '\0') {
      lVar8 = *param_2;
      lVar7 = FUN_1815f2440(lVar8);
      if (lVar7 != 0) {
        lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
      }
    }
    uVar32 = (undefined4)((ulonglong)in_stack_fffffffffffffd80 >> 0x20);
    if (*(char *)(lVar8 + 0x7d) == '\0') {
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x40);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x40);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(longlong *)(lVar7 + 0x70) != 0) {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x40);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x40);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_100 = *(undefined8 *)(*(longlong *)(lVar7 + 0x70) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x40);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x40);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(longlong *)(lVar7 + 0x58) != 0) {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x40);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x40);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_138 = *(undefined8 *)(*(longlong *)(lVar7 + 0x58) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x48);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x48);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(longlong *)(lVar7 + 0x58) != 0) {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x48);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x48);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_140 = *(undefined8 *)(*(longlong *)(lVar7 + 0x58) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x50);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x50);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(longlong *)(lVar7 + 0x58) != 0) {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x50);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x50);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_148 = *(undefined8 *)(*(longlong *)(lVar7 + 0x58) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      if (*(longlong *)(lVar8 + 0x88) != 0) {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        uStack_150 = *(undefined8 *)(*(longlong *)(lVar8 + 0x88) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      uStack_16c = *(undefined4 *)(lVar8 + 0xac);
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x48);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x48);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      uStack_164 = *(undefined4 *)(lVar7 + 0x8c);
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x50);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x50);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      uStack_168 = *(undefined4 *)(lVar7 + 0x8c);
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      uStack_174 = *(undefined4 *)(lVar8 + 0xb4);
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(lVar8 + 0x7b) != '\0') {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x48);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x48);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_108 = *(undefined8 *)(*(longlong *)(lVar7 + 0x70) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(lVar8 + 0x7c) != '\0') {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x50);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x50);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_110 = *(undefined8 *)(*(longlong *)(lVar7 + 0x70) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(lVar8 + 0x7a) != '\0') {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        if (*(longlong *)(lVar8 + 0x90) != 0) {
          lVar8 = *param_2;
          cVar2 = FUN_181629ca0();
          if (cVar2 != '\0') {
            lVar8 = *param_2;
            lVar7 = FUN_1815f2440(lVar8);
            if (lVar7 != 0) {
              lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
            }
          }
          uStack_158 = *(undefined8 *)(*(longlong *)(lVar8 + 0x90) + 0x18);
        }
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x40);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x40);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(lVar7 + 0x49) != '\0') {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x40);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x40);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_118 = *(undefined8 *)(*(longlong *)(lVar7 + 0x68) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x48);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x48);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(lVar7 + 0x49) != '\0') {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x48);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x48);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_120 = *(undefined8 *)(*(longlong *)(lVar7 + 0x68) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x50);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x50);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(lVar7 + 0x49) != '\0') {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        lVar7 = *(longlong *)(lVar8 + 0x50);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar7 = *(longlong *)(lVar8 + 0x50);
          lVar8 = FUN_1815f2440(lVar7);
          if (lVar8 != 0) {
            lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
          }
        }
        uStack_128 = *(undefined8 *)(*(longlong *)(lVar7 + 0x68) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x58);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x58);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      if (*(char *)(lVar7 + 0x49) != '\0') {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        if (*(longlong *)(lVar8 + 0x98) != 0) {
          lVar8 = *param_2;
          cVar2 = FUN_181629ca0();
          if (cVar2 != '\0') {
            lVar8 = *param_2;
            lVar7 = FUN_1815f2440(lVar8);
            if (lVar7 != 0) {
              lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
            }
          }
          uStack_130 = *(undefined8 *)(*(longlong *)(lVar8 + 0x98) + 0x18);
        }
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      if (*(longlong *)(lVar8 + 0xa0) != 0) {
        lVar8 = *param_2;
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar8 = *param_2;
          lVar7 = FUN_1815f2440(lVar8);
          if (lVar7 != 0) {
            lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        uStack_160 = *(undefined8 *)(*(longlong *)(lVar8 + 0xa0) + 0x18);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      fVar26 = *(float *)(lVar8 + 0xb8);
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      fVar1 = *(float *)(lVar8 + 0xbc);
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      fVar25 = *(float *)(lVar8 + 0xc0);
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      uVar19 = *(undefined4 *)(lVar8 + 200);
      if ((-1 < *(longlong *)(param_1 + 0x2e0)) && (*(longlong *)(param_1 + 0x2e0) <= param_12)) {
        *(undefined8 *)(param_1 + 0x2d8) = param_13;
        *(undefined8 *)(param_1 + 0x2e0) = 0xffffffffffffffff;
        uStack_198 = 1;
        *(undefined1 *)(param_1 + 0x2e8) = 0;
        *(undefined8 *)(param_1 + 0x2f8) = 0;
        *(double *)(param_1 + 0x2f0) = (double)param_14 * param_10 + 0.1;
        *(undefined8 *)(param_1 + 0x300) = 0;
      }
      plVar9 = (longlong *)FUN_180e75730(*(undefined8 *)(param_1 + 0xf8),&lStack_188);
      if ((*plVar9 == 0) || (*(longlong *)(param_1 + 0x2e0) != -1)) {
        bVar18 = false;
      }
      else {
        bVar18 = true;
      }
      uStack_178 = 0;
      if ((bStack_180 != false) && (lStack_188 != 0)) {
        FUN_1815ef980();
      }
      if (bVar18) {
        plVar9 = (longlong *)FUN_1815f6760(*(undefined8 *)(param_1 + 0xf8),&lStack_f8);
        lVar8 = *plVar9;
        bVar18 = (char)plVar9[1] == '\0';
        if (!bVar18) {
          *(undefined1 *)(plVar9 + 1) = 0;
        }
        bStack_180 = !bVar18;
        uStack_178 = 4;
        lStack_188 = lVar8;
        if ((cStack_f0 != '\0') && (lStack_f8 != 0)) {
          FUN_1815ef980();
        }
        cStack_f0 = 0;
        if ((bVar18) && (lVar8 != 0)) {
          FUN_1815f0610(lVar8);
        }
        cStack_f0 = '\x01';
        lStack_f8 = lVar8;
        cVar2 = FUN_181629ca0();
        lVar7 = lVar8;
        if ((cVar2 != '\0') && (lVar10 = FUN_1815f2440(lVar8), lVar10 != 0)) {
          lVar7 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar10 + 0x154) & 1) * 8);
        }
        lVar10 = *(longlong *)(lVar7 + 0x50);
        cVar2 = FUN_181629ca0();
        if (cVar2 != '\0') {
          lVar10 = *(longlong *)(lVar7 + 0x50);
          lVar7 = FUN_1815f2440(lVar10);
          if (lVar7 != 0) {
            lVar10 = *(longlong *)(lVar10 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
          }
        }
        bStack_180 = false;
        lVar7 = *(longlong *)(lVar10 + 0x48);
        if (lVar7 != 0) {
          FUN_1815f0610(lVar7);
        }
        bStack_180 = true;
        lStack_188 = lVar7;
        cVar2 = FUN_181629ca0();
        lVar10 = lVar7;
        if ((cVar2 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
          lVar10 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
        }
        cVar2 = FUN_181629ca0();
        lVar11 = lVar7;
        if ((cVar2 != '\0') && (lVar12 = FUN_1815f2440(), lVar12 != 0)) {
          lVar11 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar12 + 0x154) & 1) * 8);
        }
        lVar10 = *(longlong *)(lVar10 + 0xd0) + *(longlong *)(lVar11 + 0x58);
        *(longlong *)(param_1 + 0x2e0) = lVar10;
        dVar21 = (double)lVar10 * param_10;
        *(undefined4 *)(param_1 + 0x2f8) = 0x3d4ccccd;
        fVar22 = (float)(dVar21 - (double)*(longlong *)(param_1 + 0x2d8) * param_10) * 0.1;
        fVar20 = 0.05;
        if (fVar22 < 0.05) {
          *(float *)(param_1 + 0x2f8) = fVar22;
          fVar20 = fVar22;
        }
        *(double *)(param_1 + 0x2f0) = dVar21 - (double)fVar20;
        if (lVar7 != 0) {
          FUN_1815ef980(lVar7);
        }
        if (lVar8 != 0) {
          FUN_1815ef980(lVar8);
        }
      }
      if (((fVar26 != 0.0) || (0.0 < fVar1)) || (fVar25 != 0.0)) {
        iVar17 = param_6 + param_7;
        fVar23 = 0.0;
        iVar15 = *(int *)(param_1 + 0x168);
        if (0 < iVar15) {
          lVar8 = 0;
          fVar23 = 0.0;
          do {
            lVar10 = *(longlong *)(*(longlong *)(param_1 + 0x1e8) + lVar8 * 8);
            iVar16 = 0;
            lVar7 = 0;
            if (3 < iVar17) {
              pfVar13 = (float *)(lVar10 + 8);
              uVar6 = (iVar17 - 4U >> 2) + 1;
              uVar14 = (ulonglong)uVar6;
              iVar16 = uVar6 * 4;
              lVar7 = (ulonglong)uVar6 * 4;
              do {
                fVar23 = fVar23 + pfVar13[-2] * pfVar13[-2] + pfVar13[-1] * pfVar13[-1] +
                         *pfVar13 * *pfVar13 + pfVar13[1] * pfVar13[1];
                pfVar13 = pfVar13 + 4;
                uVar14 = uVar14 - 1;
              } while (uVar14 != 0);
            }
            if (iVar16 < iVar17) {
              pfVar13 = (float *)(lVar10 + lVar7 * 4);
              uVar14 = (ulonglong)(uint)(iVar17 - iVar16);
              do {
                fVar23 = fVar23 + *pfVar13 * *pfVar13;
                pfVar13 = pfVar13 + 1;
                uVar14 = uVar14 - 1;
              } while (uVar14 != 0);
            }
            lVar8 = lVar8 + 1;
          } while (lVar8 < iVar15);
        }
        if (1 < iVar15) {
          fVar23 = fVar23 / (float)iVar15;
        }
        if (fVar23 / (float)iVar17 < 0.0) {
          fVar23 = (float)sqrtf();
        }
        else {
          fVar23 = SQRT(fVar23 / (float)iVar17);
        }
        fVar23 = fVar23 * 1.732051;
        fStack_170 = fVar23;
      }
      if ((0.0 < fVar1) && (param_15 != 0)) {
        in_stack_fffffffffffffd80 = 0;
        FUN_1819f2780(param_1,param_15,param_3,param_5,CONCAT44(uVar27,fVar23),
                      CONCAT44(uVar28,fVar1),CONCAT44(uVar29,param_6),CONCAT44(uVar30,param_7),
                      CONCAT44(uVar31,param_8),0,uStack_198);
      }
      lVar8 = *param_2;
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x60);
      cVar2 = FUN_181629ca0();
      if (cVar2 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x60);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      cVar2 = *(char *)(lVar7 + 0x61);
      lVar8 = *param_2;
      cVar3 = FUN_181629ca0();
      if (cVar3 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x70);
      cVar3 = FUN_181629ca0();
      if (cVar3 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x70);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      cVar3 = *(char *)(lVar7 + 0x61);
      lVar8 = *param_2;
      cVar4 = FUN_181629ca0();
      if (cVar4 != '\0') {
        lVar8 = *param_2;
        lVar7 = FUN_1815f2440(lVar8);
        if (lVar7 != 0) {
          lVar8 = *(longlong *)(lVar8 + 0x20 + (ulonglong)(*(uint *)(lVar7 + 0x154) & 1) * 8);
        }
      }
      lVar7 = *(longlong *)(lVar8 + 0x68);
      cVar4 = FUN_181629ca0();
      if (cVar4 != '\0') {
        lVar7 = *(longlong *)(lVar8 + 0x68);
        lVar8 = FUN_1815f2440(lVar7);
        if (lVar8 != 0) {
          lVar7 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar8 + 0x154) & 1) * 8);
        }
      }
      uVar32 = (undefined4)((ulonglong)in_stack_fffffffffffffd80 >> 0x20);
      cVar4 = *(char *)(lVar7 + 0x61);
      if (((cVar2 != '\0') || (cVar3 != '\0')) || (cVar4 != '\0')) {
        dVar21 = (double)param_12 * param_10;
        lVar8 = *(longlong *)(param_1 + 0x2d8);
        fVar23 = -1.0;
        if ((1.1754944e-38 <= *(float *)(param_1 + 0x2f8)) &&
           (*(double *)(param_1 + 0x2f0) < dVar21)) {
          fVar23 = (float)(dVar21 - *(double *)(param_1 + 0x2f0)) / *(float *)(param_1 + 0x2f8);
          fVar24 = 0.0;
          if (0.0 <= fVar23) {
            fVar24 = fVar23;
          }
          fVar23 = 1.0;
          if (fVar24 <= 1.0) {
            fVar23 = fVar24;
          }
        }
        iVar15 = 0;
LAB_1819f1280:
        do {
          uVar32 = (undefined4)((ulonglong)in_stack_fffffffffffffd80 >> 0x20);
          lVar7 = 0;
          lStack_188 = 0;
          bVar18 = false;
          bStack_180 = false;
          if (iVar15 == 0) {
            if (cVar2 == '\0') {
              iVar15 = 1;
              goto LAB_1819f1280;
            }
            lVar10 = *param_2;
            cVar5 = FUN_181629ca0();
            if (cVar5 != '\0') {
              lVar10 = *param_2;
              lVar11 = FUN_1815f2440(lVar10);
              if (lVar11 != 0) {
                lVar10 = *(longlong *)
                          (lVar10 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
              }
            }
            lVar10 = *(longlong *)(lVar10 + 0x60);
LAB_1819f1348:
            if (lVar10 != 0) {
              FUN_1815f0610(lVar10);
              bVar18 = true;
              bStack_180 = true;
              lVar7 = lVar10;
              lStack_188 = lVar10;
            }
          }
          else {
            if (iVar15 == 1) {
              if (cVar3 != '\0') {
                lVar10 = *param_2;
                cVar5 = FUN_181629ca0();
                if (cVar5 != '\0') {
                  lVar10 = *param_2;
                  lVar11 = FUN_1815f2440(lVar10);
                  if (lVar11 != 0) {
                    lVar10 = *(longlong *)
                              (lVar10 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
                  }
                }
                lVar10 = *(longlong *)(lVar10 + 0x70);
                goto LAB_1819f1348;
              }
              iVar15 = 2;
              goto LAB_1819f1280;
            }
            if (iVar15 == 2) {
              if (cVar4 != '\0') {
                lVar10 = *param_2;
                cVar5 = FUN_181629ca0();
                if (cVar5 != '\0') {
                  lVar10 = *param_2;
                  lVar11 = FUN_1815f2440(lVar10);
                  if (lVar11 != 0) {
                    lVar10 = *(longlong *)
                              (lVar10 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
                  }
                }
                lVar10 = *(longlong *)(lVar10 + 0x68);
                goto LAB_1819f1348;
              }
              break;
            }
          }
          if (*(longlong *)(param_1 + 0x2d8) < 0) {
            cVar5 = FUN_181629ca0();
            lVar10 = lVar7;
            if ((cVar5 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
              lVar10 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
            }
            cVar5 = FUN_181629ca0();
            lVar11 = lVar7;
            if ((cVar5 != '\0') && (lVar12 = FUN_1815f2440(lVar7), lVar12 != 0)) {
              lVar11 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar12 + 0x154) & 1) * 8);
            }
            if (*(float *)(lVar10 + 0x44) < *(float *)(lVar11 + 0x5c) ||
                *(float *)(lVar10 + 0x44) == *(float *)(lVar11 + 0x5c)) {
              cVar5 = FUN_181629ca0();
              if ((cVar5 == '\0') || (lVar10 = FUN_1815f2440(lVar7), lVar10 == 0)) {
                fVar24 = *(float *)(lVar7 + 0x44);
              }
              else {
                fVar24 = *(float *)(*(longlong *)
                                     (lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar10 + 0x154) & 1) * 8)
                                   + 0x44);
              }
            }
            else {
              cVar5 = FUN_181629ca0();
              if ((cVar5 == '\0') || (lVar10 = FUN_1815f2440(lVar7), lVar10 == 0)) {
                fVar24 = *(float *)(lVar7 + 0x5c);
              }
              else {
                fVar24 = *(float *)(*(longlong *)
                                     (lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar10 + 0x154) & 1) * 8)
                                   + 0x5c);
              }
            }
          }
          else {
            cVar5 = FUN_181629ca0();
            lVar10 = lVar7;
            if ((cVar5 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
              lVar10 = *(longlong *)(lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
            }
            fVar24 = (float)FUN_181e1caa0(lVar10,(float)(dVar21 - (double)lVar8 * param_10));
          }
          if (iVar15 == 0) {
            if (0.0 <= fVar23) {
              if (*(char *)(param_1 + 0x2e8) == '\0') {
                cVar5 = FUN_181629ca0();
                lVar10 = lVar7;
                if ((cVar5 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
                  lVar10 = *(longlong *)
                            (lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
                }
                if (*(float *)(lVar10 + 0x44) <= fVar24 && fVar24 != *(float *)(lVar10 + 0x44)) {
                  *(float *)(param_1 + 0x2fc) = fVar24;
                }
              }
              if (0.0 < *(float *)(param_1 + 0x2fc)) {
                cVar5 = FUN_181629ca0();
                lVar10 = lVar7;
                if ((cVar5 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
                  lVar10 = *(longlong *)
                            (lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
                }
                fVar24 = (*(float *)(lVar10 + 0x44) - *(float *)(param_1 + 0x2fc)) * fVar23 +
                         *(float *)(param_1 + 0x2fc);
              }
            }
            fStack_190 = fStack_190 * fVar24 * fVar24;
          }
          else if (iVar15 == 1) {
            if (0.0 <= fVar23) {
              if (*(char *)(param_1 + 0x2e8) == '\0') {
                cVar5 = FUN_181629ca0();
                lVar10 = lVar7;
                if ((cVar5 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
                  lVar10 = *(longlong *)
                            (lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
                }
                if (*(float *)(lVar10 + 0x44) <= fVar24 && fVar24 != *(float *)(lVar10 + 0x44)) {
                  *(float *)(param_1 + 0x300) = fVar24;
                }
              }
              if ((0.0 < *(float *)(param_1 + 0x300)) && (cVar5 = FUN_181629ca0(), cVar5 != '\0')) {
                FUN_1815f2440(lVar7);
              }
            }
            fVar24 = (float)powf();
            param_11 = param_11 * fVar24;
          }
          else if ((iVar15 == 2) && (fStack_194 = fVar24, 0.0 <= fVar23)) {
            if (*(char *)(param_1 + 0x2e8) == '\0') {
              cVar5 = FUN_181629ca0();
              lVar10 = lVar7;
              if ((cVar5 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
                lVar10 = *(longlong *)
                          (lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
              }
              if (*(float *)(lVar10 + 0x44) <= fVar24 && fVar24 != *(float *)(lVar10 + 0x44)) {
                *(float *)(param_1 + 0x304) = fVar24;
              }
            }
            if (0.0 < *(float *)(param_1 + 0x304)) {
              cVar5 = FUN_181629ca0();
              lVar10 = lVar7;
              if ((cVar5 != '\0') && (lVar11 = FUN_1815f2440(lVar7), lVar11 != 0)) {
                lVar10 = *(longlong *)
                          (lVar7 + 0x20 + (ulonglong)(*(uint *)(lVar11 + 0x154) & 1) * 8);
              }
              fStack_194 = (*(float *)(lVar10 + 0x44) - *(float *)(param_1 + 0x304)) * fVar23 +
                           *(float *)(param_1 + 0x304);
            }
          }
          if ((bVar18) && (lVar7 != 0)) {
            FUN_1815ef980(lVar7);
          }
          uVar32 = (undefined4)((ulonglong)in_stack_fffffffffffffd80 >> 0x20);
          iVar15 = iVar15 + 1;
        } while (iVar15 < 3);
        fVar24 = fStack_194;
        if ((0.0 <= fVar23) && (*(char *)(param_1 + 0x2e8) == '\0')) {
          *(undefined1 *)(param_1 + 0x2e8) = 1;
        }
      }
    }
  }
  MULSS_reconstructSpectralComponents
            (param_1,*(undefined8 *)(param_1 + 0x1c0),*(undefined8 *)(param_1 + 0x1c8),uStack_100,
             uStack_108,uStack_110,*(undefined8 *)(param_1 + 0x1d8),*(undefined8 *)(param_1 + 0x2c8)
             ,*(undefined8 *)(param_1 + 0x2d0),CONCAT44(uVar32,param_8),
             *(undefined4 *)(param_1 + 0x168),param_3,uStack_164,uStack_168,0,param_4,param_11,
             uStack_118,uStack_120,uStack_128,uStack_130,param_9,uStack_16c,fVar26,fStack_170,fVar25
             ,uStack_138,uStack_140,uStack_148,uStack_150,uStack_158,uStack_160,fStack_190,uVar19,
             uStack_174,0x3f800000,fVar24,0x3f800000);
  if (((char)param_2[1] != '\0') && (*param_2 != 0)) {
    FUN_1815ef980();
  }
  return;
}


