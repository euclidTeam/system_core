/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _LIBANDROID_UNWIND_MACHINE_H
#define _LIBANDROID_UNWIND_MACHINE_H

#include "DwarfStructs.h"
#include "Regs.h"

class MachineType {
 public:
  MachineType() = default;
  virtual ~MachineType() = default;

  virtual void InitLocationRegs(dwarf_loc_regs_t*) = 0;
  virtual uint16_t PcReg() = 0;
  virtual uint16_t SpReg() = 0;
};

class Arm : public MachineType {
 public:
  Arm() = default;
  virtual ~Arm() = default;

  void InitLocationRegs(dwarf_loc_regs_t*) override;

  uint16_t PcReg() override { return ARM_REG_PC; }
  uint16_t SpReg() override { return ARM_REG_SP; }
};

class Arm64 : public MachineType {
 public:
  Arm64() = default;
  virtual ~Arm64() = default;

  void InitLocationRegs(dwarf_loc_regs_t*) override;

  uint16_t PcReg() override { return ARM64_REG_PC; }
  uint16_t SpReg() override { return ARM64_REG_SP; }
};

class X86 : public MachineType {
 public:
  X86() = default;
  virtual ~X86() = default;

  void InitLocationRegs(dwarf_loc_regs_t*) override;

  uint16_t PcReg() override { return X86_REG_PC; }
  uint16_t SpReg() override { return X86_REG_SP; }
};

class X86_64 : public MachineType {
 public:
  X86_64() = default;
  virtual ~X86_64() = default;

  void InitLocationRegs(dwarf_loc_regs_t*) override;

  uint16_t PcReg() override { return X86_64_REG_PC; }
  uint16_t SpReg() override { return X86_64_REG_SP; }
};

#endif  // _LIBANDROID_UNWIND_MACHINE_H
