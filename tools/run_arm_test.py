"""Run a bare-metal ARM ELF test function using Unicorn and pyelftools.

Build tests/beam_test.c with arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb
-mfloat-abi=soft -O2 -Iinclude -nostartfiles -Wl,-e,beam_test
-Wl,-Ttext=0x10000000 --specs=nosys.specs -o tmp/beam-test.elf -lm.
Install unicorn and pyelftools, then run this tool with that ELF path.
"""
import sys
import argparse
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tmp/beam-test-runtime'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf')
parser.add_argument('--dump-symbol')
parser.add_argument('--dump-size',type=int)
parser.add_argument('--dump-output',type=Path)
args=parser.parse_args()
with open(args.elf,'rb') as source:
    elf=ELFFile(source)
    cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    cpu.mem_map(0x10000000,0x200000)
    cpu.mem_map(0x20000000,0x200000)
    cpu.mem_map(0x30000000,0x1000)
    for segment in elf.iter_segments():
        if segment['p_type']=='PT_LOAD':
            cpu.mem_write(segment['p_vaddr'],segment.data())
    cpu.reg_write(UC_ARM_REG_SP,0x201ff000)
    cpu.reg_write(UC_ARM_REG_LR,0x30000001)
    cpu.emu_start(elf.header['e_entry']|1,0x30000000,count=2000000000)
    if cpu.reg_read(UC_ARM_REG_PC)!=0x30000000:
        raise RuntimeError('ARM test exceeded instruction budget')
    result=cpu.reg_read(UC_ARM_REG_R0)
    print(Path(args.elf).stem + ' compiled ARM tests:', 'PASS' if result==0 else f'FAIL check {result}')
    if result==0 and args.dump_symbol:
        symbol=elf.get_section_by_name('.symtab').get_symbol_by_name(args.dump_symbol)[0]
        args.dump_output.write_bytes(cpu.mem_read(symbol['st_value'],args.dump_size))
    sys.exit(bool(result))
