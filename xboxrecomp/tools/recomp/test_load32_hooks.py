import pytest

from tools.recomp.disasm import Instruction, Operand
from tools.recomp.lifter import Lifter


def load(pc, address=0x2000, size=4):
    return Instruction(pc, 6, "mov", "", "", operands=[
        Operand(type="reg", reg="edx"),
        Operand(type="mem", mem_disp=address, mem_size=size),
    ])


def test_hook_applies_to_the_selected_instruction_only():
    lifter = Lifter(load32_hooks={0x1000: (0x2000, "present_interval")})
    assert lifter.lift_instruction(load(0x1000)) == [
        "{ extern uint32_t present_interval(uint32_t); "
        "edx = present_interval(MEM32(0x2000)); }"
    ]
    assert lifter.lift_instruction(load(0x1010)) == ["edx = MEM32(0x2000);"]
    assert lifter.used_load32_hooks == {0x1000}


@pytest.mark.parametrize("instruction", [
    load(0x1000, address=0x2004),
    load(0x1000, size=2),
    Instruction(0x1000, 6, "mov", "", "", operands=[
        Operand(type="mem", mem_disp=0x2000, mem_size=4),
        Operand(type="reg", reg="edx"),
    ]),
])
def test_hook_rejects_an_unexpected_instruction(instruction):
    lifter = Lifter(load32_hooks={0x1000: (0x2000, "present_interval")})
    with pytest.raises(ValueError, match="0x00001000"):
        lifter.lift_instruction(instruction)
    assert not lifter.used_load32_hooks
