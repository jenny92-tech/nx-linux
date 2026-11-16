"""Explicit build-time community writes, applied BEFORE native instruction rewriting.

Plans are local reviewed inputs, never auto-enabled downloads. Full NSO hash,
exact title/build/module and expected bytes prevent cross-version application;
overlap with native built-in cuts fails rather than silently choosing a winner.
Code is reprocessed by the existing rewriter, not patched into rewritten code.
"""
import hashlib
import re


def _hex(value, size=None):
    if not isinstance(value, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2})+', value):
        raise ValueError('Expected nonempty hexadecimal bytes')
    result = bytes.fromhex(value)
    if size is not None and len(result) != size:
        raise ValueError('Identity length differs')
    return result


def apply_plan(image, nso, plan, *, title_id, build_id, module, text_end, reserved=()):
    if not isinstance(plan, dict) or plan.get('module') != module:
        raise ValueError('Community patch module differs')
    if _hex(plan.get('title_id'), 8) != _hex(title_id, 8):
        raise ValueError('Community patch title differs')
    if _hex(plan.get('build_id'), 16) != _hex(build_id, 16):
        raise ValueError('Community patch build differs')
    digest = hashlib.sha256(nso).hexdigest()
    if _hex(plan.get('nso_sha256'), 32).hex() != digest:
        raise ValueError('Community patch original NSO hash differs')
    patches = plan.get('patches')
    if not isinstance(patches, list) or len(patches) > 256:
        raise ValueError('Community patch list is invalid')
    pending, identifiers, selected, total = [], set(), [], 0
    for patch in patches:
        if not isinstance(patch, dict):
            raise ValueError('Community patch entry is invalid')
        name = patch.get('id')
        if not isinstance(name, str) or not name or len(name) > 160 or name in identifiers:
            raise ValueError('Community patch id is invalid or duplicated')
        identifiers.add(name)
        enabled = patch.get('enabled', False)
        if type(enabled) is not bool:
            raise ValueError('Community patch enabled must be boolean')
        if patch.get('category') not in ('fps', 'graphics', 'aspect'):
            raise ValueError('Unsupported community patch category')
        if not enabled:
            continue
        writes = patch.get('writes')
        if not isinstance(writes, list) or not writes or len(writes) > 1024:
            raise ValueError('Community writes are empty or exceed limits')
        for write in writes:
            if not isinstance(write, dict):
                raise ValueError('Community write is invalid')
            offset = write.get('offset')
            before, after = _hex(write.get('expected')), _hex(write.get('replace'))
            if type(offset) is not int or offset < 0 or offset > len(image) - len(before):
                raise ValueError('Community write outside original image')
            if len(before) != len(after) or len(before) > 4096:
                raise ValueError('Community write size differs or exceeds limits')
            if offset < text_end and (offset % 4 or len(before) % 4 or offset + len(before) > text_end):
                raise ValueError('Community code write is not whole AArch64 instructions')
            if any(offset < start + size and start < offset + len(before) for start,size in reserved):
                raise ValueError('Community write overlaps a reserved built-in cut')
            if bytes(image[offset:offset + len(before)]) != before:
                raise ValueError('Community original bytes differ or conflict with built-in cuts')
            pending.append((offset, before, after, name))
            total += len(before)
            if total > 1024 * 1024:
                raise ValueError('Community total write budget exceeded')
        selected.append(name)
    pending.sort(key=lambda row: row[0])
    for a, b in zip(pending, pending[1:]):
        if a[0] + len(a[1]) > b[0]:
            raise ValueError('Community patch writes overlap')
    # All bounds, original bytes and conflicts were checked before mutation.
    for offset, before, after, name in pending:
        image[offset:offset + len(after)] = after
    return dict(title_id=title_id.lower(), build_id=build_id.lower(), module=module,
                original_nso_sha256=digest, selected=selected, written_bytes=total,
                writes=[dict(offset=at, expected=old.hex(), replace=new.hex(), patch=name)
                        for at, old, new, name in pending])
