"""Narrow signed32 RO-relative switch recognition; emits no guest instructions.

Reference: S2Arm c62634054c runtime/rewriter/rewriter.c classify/jt_targets.
The Linux emitter retains its existing dense TEXT pads and virtual x18 slot.
"""
from collections import deque
import struct
from capstone import CS_AC_WRITE
from capstone.arm64 import ARM64_OP_REG, ARM64_OP_IMM, ARM64_OP_MEM, ARM64_SFT_LSL


def _reg(ins, index):
    if index >= len(ins.operands) or ins.operands[index].type != ARM64_OP_REG:
        return None
    return ins.reg_name(ins.operands[index].reg)


def _number(name):
    return int(name[1:]) if name and name[0] in 'xw' and name[1:].isdigit() else None


def _call(ins):
    return ins.mnemonic in ('bl', 'blr')


def _branch(ins):
    return ins.mnemonic in ('b', 'br', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz') or ins.mnemonic.startswith('b.')


def _writes(ins, number):
    # Check both aliases and an explicit destination: Capstone can omit writes
    # for some W-register instructions. A false refusal is safer than a guessed base.
    if ins.mnemonic in ('cmp', 'cmn', 'tst', 'ccmp', 'ccmn', 'br', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz'):
        return False
    if ins.writeback and any(op.type == ARM64_OP_MEM and _number(ins.reg_name(op.mem.base)) == number
                             for op in ins.operands):
        return True
    if any(_number(ins.reg_name(r)) == number for r in ins.regs_access()[1]):
        return True
    if any(op.type == ARM64_OP_REG and op.access & CS_AC_WRITE
           and _number(ins.reg_name(op.reg)) == number for op in ins.operands):
        return True
    # Pair loads have two explicit destinations even if write reporting is
    # incomplete. Checking only operand zero misses LDP Xn,Xbase,[Xm].
    if ins.mnemonic in ('ldp','ldnp','ldpsw') and _number(_reg(ins,1)) == number:
        return True
    read_first = ins.mnemonic in ('cmp', 'cmn', 'tst', 'br', 'blr', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz')
    read_first |= ins.mnemonic.startswith(('str', 'stur', 'stp', 'stnp'))
    return _number(_reg(ins, 0)) == number and not read_first


def _definition(insl, before, number):
    for i in range(before, -1, -1):
        if _call(insl[i]) or _writes(insl[i], number):
            return i
    return None


def _add_immediate(ins, number):
    return (ins.mnemonic == 'add' and len(ins.operands) == 3
            and _reg(ins, 0) == 'x' + str(number) == _reg(ins, 1)
            and ins.operands[2].type == ARM64_OP_IMM
            and ins.operands[2].shift.value == 0 and 0 <= ins.operands[2].imm <= 4095)


def _other_switch(insl, site, image, text_end, ro_start, ro_end, interiors):
    """Bound another GPR dispatch for CFG edges, without rewriting that branch.

    These companion tables establish their RO base after the guard. Unknown
    indirect edges remain a refusal rather than silently disappearing from CFG.
    """
    register = _reg(insl[site], 0)
    number = _number(register)
    if number is None or not register.startswith('x'):
        return None
    add_at = _definition(insl, site-1, number)
    if add_at is None:
        return None
    add = insl[add_at]
    base_name = _reg(add, 2)
    base = _number(base_name)
    if (add.mnemonic != 'add' or len(add.operands) != 3
            or [_reg(add, i) for i in range(2)] != [register, register]
            or base is None or base == number or not base_name.startswith('x')
            or add.operands[2].shift.value != 0):
        return None
    load_at = _definition(insl, add_at-1, number)
    if load_at is None:
        return None
    load = insl[load_at]
    if (load.mnemonic != 'ldrsw' or len(load.operands) != 2
            or load.operands[1].type != ARM64_OP_MEM or load.writeback):
        return None
    memory = load.operands[1]
    if (load.reg_name(memory.mem.base) != base_name or load.reg_name(memory.mem.index) != register
            or memory.mem.disp or memory.ext or memory.shift.type != ARM64_SFT_LSL or memory.shift.value != 2):
        return None
    if any(_writes(i, base) for i in insl[load_at+1:add_at]):
        return None
    guard_at = next((k for k in range(load_at-1, -1, -1) if _branch(insl[k]) or _call(insl[k])), None)
    if guard_at is None or guard_at == 0 or insl[guard_at].mnemonic != 'b.hi':
        return None
    compare = insl[guard_at-1]
    if (compare.mnemonic != 'cmp' or _reg(compare, 0) != 'w'+str(number)
            or len(compare.operands) != 2 or compare.operands[1].type != ARM64_OP_IMM
            or compare.operands[1].shift.value != 0
            or not 0 <= compare.operands[1].imm < 4096):
        return None
    index_at = _definition(insl, load_at-1, number)
    if index_at is None or _reg(insl[index_at], 0) != 'w'+str(number):
        return None
    if index_at > guard_at:
        # MOV Wn,Wn preserves the checked low value and clears the high half.
        if insl[index_at].mnemonic != 'mov' or _reg(insl[index_at], 1) != 'w'+str(number):
            return None
        protected_from = guard_at
    else:
        if any(_branch(i) or _call(i) for i in insl[index_at+1:guard_at-1]):
            return None
        protected_from = index_at+1
    if any(_writes(i, number) and not (i.mnemonic == 'mov' and _reg(i, 0) == _reg(i, 1) == 'w'+str(number))
           for i in insl[guard_at+1:load_at]):
        return None
    base_add = _definition(insl, load_at-1, base)
    if base_add is None or not _add_immediate(insl[base_add], base):
        return None
    page = _definition(insl, base_add-1, base)
    if page is None or page <= guard_at or insl[page].mnemonic != 'adrp' or _reg(insl[page], 0) != base_name:
        return None
    if any(_call(i) or _branch(i) for i in insl[guard_at+1:site]):
        return None
    table = insl[page].operands[1].imm + insl[base_add].operands[2].imm
    count = compare.operands[1].imm+1
    if table % 4 or table < ro_start or table+4*count > min(ro_end,len(image)):
        return None
    protected = {i.address for i in insl[protected_from:site+1]}
    for ins in insl:
        if (_branch(ins) or _call(ins)) and ins.operands and ins.operands[-1].type == ARM64_OP_IMM:
            if ins.operands[-1].imm in protected:
                return None
    targets = [table+struct.unpack_from('<i',image,table+4*k)[0] for k in range(count)]
    addresses = {i.address for i in insl}
    if any(t % 4 or not 0 <= t < text_end or t not in addresses or t in protected or t in interiors for t in targets):
        return None
    return targets, protected


def _base_at_load(insl, load, site, number, targets, image, text_end, ro_start, ro_end, interiors, main_protected):
    """Prove the base on every reachable path, including switch-loop backedges.

    A backwards textual ADRP search alone accepts a path that bypasses the
    definition. The three-state merge below distinguishes unreachable, one
    constant and unknown. Calls/other indirect dispatches are refused here.
    """
    dispatches = {site:targets}
    protected_regions = [main_protected]
    for k, ins in enumerate(insl):
        if ins.mnemonic == 'br' and k != site:
            outgoing = _other_switch(insl,k,image,text_end,ro_start,ro_end,interiors)
            if outgoing is None:
                return None
            dispatches[k], protected = outgoing
            protected_regions.append(protected)
    # Indirect table edges must obey the same entry invariant as direct
    # branches. A companion must not jump past another dispatch's W write or
    # bound check, even when the table-base value itself remains constant.
    protected = set().union(*protected_regions)
    if any(target in protected for outgoing in dispatches.values() for target in outgoing):
        return None
    by_address = {i.address: k for k, i in enumerate(insl)}
    unseen, unknown = object(), object()
    states = [unseen] * len(insl)
    states[0] = unknown
    pending = deque([0])
    while pending:
        k = pending.popleft()
        ins, value = insl[k], states[k]
        if ins.mnemonic == 'adrp' and _reg(ins, 0) == 'x' + str(number):
            value = ins.operands[1].imm
        elif _add_immediate(ins, number):
            value = unknown if value is unknown else value + ins.operands[2].imm
        elif _call(ins) or _writes(ins, number):
            value = unknown
        if k in dispatches:
            next_addresses = dispatches[k]
        elif ins.mnemonic == 'ret':
            next_addresses = []
        elif ins.mnemonic == 'b':
            next_addresses = [ins.operands[-1].imm]
        elif ins.mnemonic in ('cbz', 'cbnz', 'tbz', 'tbnz') or ins.mnemonic.startswith('b.'):
            next_addresses = [ins.address + 4, ins.operands[-1].imm]
        else:
            next_addresses = [ins.address + 4]
        for address in next_addresses:
            successor = by_address.get(address)
            if successor is None:
                continue
            previous = states[successor]
            merged = value if previous is unseen else (previous if previous == value else unknown)
            if merged is not previous and merged != previous:
                states[successor] = merged
                pending.append(successor)
    result = states[load]
    return result if isinstance(result, int) else None


def relative_table(insl, site, image, text_end, ro_start, ro_end, window_interiors):
    """Return existing (TEXT pad base, count), or (None, None) on any unknown."""
    refuse = (None, None)
    if site < 4 or insl[site].mnemonic != 'br' or _reg(insl[site], 0) != 'x18':
        return refuse
    load, add, compare, guard = insl[site-2], insl[site-1], insl[site-4], insl[site-3]
    if (load.mnemonic != 'ldrsw' or _reg(load, 0) != 'x18' or len(load.operands) != 2
            or load.operands[1].type != ARM64_OP_MEM or load.writeback):
        return refuse
    memory = load.operands[1]
    base_name = load.reg_name(memory.mem.base)
    base = _number(base_name)
    if (base is None or base == 18 or not base_name.startswith('x')
            or load.reg_name(memory.mem.index) != 'x18' or memory.mem.disp != 0
            or memory.ext != 0 or memory.shift.type != ARM64_SFT_LSL or memory.shift.value != 2):
        return refuse
    if (add.mnemonic != 'add' or len(add.operands) != 3
            or [_reg(add, i) for i in range(3)] != ['x18', 'x18', base_name]
            or add.operands[2].shift.value != 0):
        return refuse
    if (compare.mnemonic != 'cmp' or _reg(compare, 0) != 'w18'
            or len(compare.operands) != 2 or compare.operands[1].type != ARM64_OP_IMM
            or compare.operands[1].shift.value != 0
            or guard.mnemonic != 'b.hi'):
        return refuse
    bound = compare.operands[1].imm
    if not 0 <= bound < 4096:
        return refuse
    index = _definition(insl, site - 5, 18)
    if index is None:
        return refuse
    defining = insl[index]
    # CMP W18 bounds the full X18 index only after a proven W write.
    if (defining.mnemonic != 'lsr' or _reg(defining, 0) != 'w18'
            or not (_reg(defining, 1) or '').startswith('w') or len(defining.operands) != 3
            or defining.operands[2].type != ARM64_OP_IMM or not 0 < defining.operands[2].imm < 32
            or any(_branch(i) or _call(i) for i in insl[index+1:site-4])):
        return refuse
    base_add = _definition(insl, site - 3, base)
    if base_add is None or not _add_immediate(insl[base_add], base):
        return refuse
    page = _definition(insl, base_add - 1, base)
    if page is None or insl[page].mnemonic != 'adrp' or _reg(insl[page], 0) != base_name:
        return refuse
    table = insl[page].operands[1].imm + insl[base_add].operands[2].imm
    if table % 4 or not ro_start <= table or table + 4*(bound+1) > min(ro_end, len(image)):
        return refuse
    # An incoming edge after the W write can bypass zero-extension/the bound.
    protected = set(i.address for i in insl[index+1:site+1])
    for ins in insl:
        if (_branch(ins) or _call(ins)) and ins.operands and ins.operands[-1].type == ARM64_OP_IMM:
            if ins.operands[-1].imm in protected:
                return refuse
    by_address = {i.address for i in insl}
    targets = [table + struct.unpack_from('<i', image, table + 4*i)[0] for i in range(bound+1)]
    if any(t % 4 or not 0 <= t < text_end or t not in by_address
           or t in protected or t in window_interiors for t in targets):
        return refuse
    if _base_at_load(insl, site-2, site, base, targets, image, text_end, ro_start, ro_end, window_interiors, protected) != table:
        return refuse
    lo, hi = min(targets), max(targets)
    count = (hi-lo)//4 + 1
    return (lo, count) if count <= 65536 else refuse
