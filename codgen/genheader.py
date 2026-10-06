import sys
import argparse
from pathlib import Path
import re

from cmsis_svd import SVDParser
from cmsis_svd.model import (
    SVDAccessType,
    SVDRegister,
    SVDRegisterArray,
    SVDRegisterCluster,
    SVDRegisterClusterArray,
    SVDDevice,
    SVDPeripheral,
    SVDField
)

infer_size_arg = False

# I'm sure there's more, but these are the ones I ran into from the CMSIS-SVD
# data repo (https://github.com/cmsis-svd/cmsis-svd-data)
reserved_keywords = [
    "if",
    "else"
    "try",
    "catch",
    "new",
    "class",
    "struct",
    "union",
    "EOF",
    "LITTLE_ENDIAN"
]

# a dict for names used already, per peripheral. This way we can avoid duplicates
# but get fast access time
names_used = {}

def add_name_if_not_used(periph, regname):
    global names_used

    # strip out "[]" in the register name
    if "[" in regname:
        regname = regname.replace("[","").replace("]","")

    if regname in reserved_keywords:
        regname = self.name.upper()

    if regname[0].isdigit():
        while regname[0].isdigit():
            regname = regname[1:]

    if names_used.get(periph) is None:
        names_used[periph] = {}

    if names_used[periph].get(regname) is not None:
        regname = increment_end_number(regname)
        # short circuit this to try and add it again
        return add_name_if_not_used(periph, regname)


    names_used[periph][regname] = True

    return regname


def increment_end_number(string):
    # Match the digits exactly at the end of the string ($)
    ret = re.sub(r'\d+$', lambda match: str(int(match.group()) + 1).zfill(len(match.group())), string)

    # if we still match after incrementing, it means there was no number to increment, so add one
    if ret == string:
        ret += "1"

    return ret

# indent helper
def idt(num):
    return " "*num

def int_type(bit_size):
    match(bit_size):
        case 8: return "uint8_t"
        case 16: return "uint16_t"
        case 32: return "uint32_t"
        case 64: return "uint64_t"
        case _: return "uint8_t" # just default to a byte if an incorrect size is specified

def access_policy(access):
    match access:
        case SVDAccessType.READ_WRITE: return "{}"
        case SVDAccessType.READ_ONLY: return "{MMIO::AccessPolicy::ReadOnly}"
        case SVDAccessType.WRITE_ONLY: return "{MMIO::AccessPolicy::WriteOnly}"
        case SVDAccessType.WRITE_ONCE: return "{MMIO::AccessPolicy::WriteOnly}"
        # there's not a good, reliable, way to enforce this at the programatic level
        # so instead, we just explicitly name it "ReadWrite" in the generated header
        # _actual_ read/write registers in the header won't have an access policy
        # specified (default _is_ true read/write)
        case SVDAccessType.READ_WRITE_ONCE: return "{MMIO::AccessPolicy::ReadWrite}"
        # just return to default if somehow the access policy is invalid
        case _: return "{}"

class RegItem:
    def __init__(self, reg: SVDRegister, name=None, offset=None, array_len=None, size=None, clusterSize=None):
        self.reg = reg
        self.name = name or reg.name
        self.offset = offset if offset is not None else reg.address_offset
        self.array_len = array_len
        self.aliases: list[RegItem] = []
        self.size = size or reg.size
        if reg.size == 1:
            self.size = 8 # some vendors do stupid shit like this and mean size of 1 byte
            # but the size field of an SVD register means size in bits.
        self.clusterSize = clusterSize

    @property
    def size_bits(self):
        return self.size

    @property
    def size_bytes(self):
        ret = self.size // 8
        if self.clusterSize is not None:
            ret *= self.clusterSize
        return ret

    @property
    def fields(self):
        return self.reg.get_fields()

    @property
    def last_element_offset(self):
        return self.offset + self.size_bytes * ((self.array_len or 1) - 1)

    @property
    def description(self):
        return self.reg.description

    @property
    def rawreg(self):
        return self.reg

# il = indent level
def desc(desc, il):
    lines = []

    if desc is not None:
        desc_lines = desc.split("\n")
        for line in desc_lines:
            lines.append(idt(il) + f"// {line.strip()}")

    return "\n".join(lines)

def flatten_registers(entries, periph_name: str = "") -> list[RegItem]:
    items: list[RegItem] = []

    # helper function to recursively walk the SVD tree and create a map of register
    # entries
    def visit(entry):
        if isinstance(entry, SVDRegister):
            fixed_name = add_name_if_not_used(periph_name, entry.name)
            items.append(RegItem(entry, name=fixed_name))

        elif isinstance(entry, SVDRegisterArray):
            meta = entry.meta_register
            # remove the [] in the name and replace it with numbers
            if "[" in meta.name:
                fixed_name = add_name_if_not_used(periph_name, meta.name.split("[")[0])
                items.append(RegItem(meta, name=fixed_name, array_len=meta.dim))
            else:
                for reg in entry.registers:
                    fixed_name = add_name_if_not_used(periph_name, reg.name)
                    items.append(RegItem(reg, name=fixed_name))

        elif isinstance(entry, SVDRegisterClusterArray):
            instance_index = 0
            for cluster in entry.clusters:
                for reg in cluster.registers:
                    if type(reg) == SVDRegisterArray or type(reg) == SVDRegister:
                        visit(reg)
                    else:
                        target_name = add_name_if_not_used(periph_name, reg.name)
                        items.append(RegItem(reg, name=target_name, clusterSize=len(entry.clusters)))



    for entry in entries:
        visit(entry)

    items.sort(key=lambda i: i.offset)

    merged = []
    by_offset = {}

    # merge registers/fields that have the same offset and make the aliases of each other
    # the name of the register will be the first name that is parsed, and variants will
    # be noted in comments/fields
    for item in items:
        existing = by_offset.get(item.offset)
        if existing is None:
            by_offset[item.offset] = item
            merged.append(item)
        else:
            existing.aliases.append(item)

    return merged

def parse_fields(variantname: str, fields: list[SVDField], il=8):
    lines = []

    for field in fields:

        ap = access_policy(field.access)
        name = field.name

        # skip reserved ones. We can't use them anyway
        if name.lower() == "reserved":
            continue

        # if the name of the variant is the same as the current register, add
        # V_ to it to avoid name collision
        if name == variantname:
            name = "V_"+name

        if name in reserved_keywords:
            # if the name matches a c++ reserved keyword, just make it capitalized.
            # for example 'if' becomes 'If'. We do it this way because we can't just
            # uppercase it, because then it might conflict with things like EOF
            name = name.capitalize()
        if field.bit_width == 1:
            # technically this isn't needed. we could just define all fields with
            # the <high,low> syntax even if the bit range is only 1.
            # For example, Field<4,4> is the same as Field<4>. But this would make things
            # a little more clear for someone glancing at the generated header.
            pos = field.bit_offset
            lines.append(desc(field.description, il))
            lines.append(idt(il) + f"static constexpr MMIO::Field<{pos}> "\
                         f"{name}{ap};")
        else:
            high = field.bit_offset + field.bit_width - 1
            low = field.bit_offset
            lines.append(desc(field.description, il))
            lines.append(idt(il) + f"static constexpr MMIO::Field<{high}, {low}> "\
                         f"{name}{ap};")

    return "\n".join(lines)

def parse_register(register: RegItem, assert_info):

    lines = []

    size = register.size_bits
    name = register.name

    lines.append("")
    comment = f"// Offset +{hex(register.offset)}"
    itype = int_type(size)

    # if array_len is defined, then this is a register array, and we should create
    # an array entry rather than a single instace
    if register.array_len:
        lines.append(desc(register.description, 4))
        reg_line = f"MMIO::Register<{itype}> {name}[{register.array_len}]; {comment}"
        assert_info.append({"name": name, "offset": register.offset})
        lines.append(idt(4) + reg_line)

    elif register.aliases:
        variants = [register] + register.aliases
        comment = f"// Offset +{hex(register.offset)}, aliases: "
        for v in variants:
            if v.name != register.name:
                comment += f"{v.name}, "

        if comment == "// aliases: ":
            comment = ""
        else:
            comment = comment[:-2] # remove trailing comma

        lines.append(desc(register.description, 4))
        lines.append(idt(4) +  f"MMIO::Register<{itype}> {name}; {comment}")
        assert_info.append({"name": name, "offset": register.offset})

        if "[" in name:
            name = name.split("[")[0]

        lines.append(idt(4) + f"struct {name}_fields")
        lines.append(idt(4) + "{")

        for variant in variants:
            if variant.fields:
                # il = indent level
                il = 8
                if variant.name != register.name:
                    lines.append(idt(8) + f"struct {variant.name} // aliased variant")
                    lines.append(idt(8) + "{")
                    il = 12

                lines.append(parse_fields(variant.name, variant.fields, il))

                if variant.name != register.name:
                    lines.append(idt(8) + "};")
            else:
                ap = access_policy(variant.reg.access)
                lines.append(idt(8) + f"static constexpr MMIO::Field<{size - 1}, 0> {variant.name}{ap};")

        lines.append(idt(4) + "};")
    else:
        lines.append(desc(register.description, 4))
        lines.append(idt(4) + f"MMIO::Register<{itype}> {name}; {comment}")
        assert_info.append({"name": register.name, "offset": register.offset})

        if register.fields:
            if "[" in name:
                name = name.split("[")[0]

            lines.append(idt(4) + f"struct {name}_fields")
            lines.append(idt(4) + "{")
            lines.append(parse_fields("", register.fields))
            lines.append(idt(4) + "};")


    return "\n".join(lines)

def generate_peripheral(periph: SVDPeripheral):
    global infer_size_arg

    # flatten the register map if there are registers, otherwise just ignore
    # this peripheral (if there are no exposed registers, there's no meaningful
    # way to interact with it anyway. We'll still add it to the header, but
    # it will really only have a BaseAddr entry)
    if periph.registers:
        registers = flatten_registers(periph.registers, periph.name)
    else:
        registers = []

    assert_lines = []
    lines = []

    name = periph.name

    if "[" in periph.name:
        name = periph.name.replace("[","").replace("]","")

    lines.append(f"struct {name}")
    lines.append("{")
    # add the base address as part of the struct so implementing drivers can access it without
    # having to look at the datasheet or SVD file.
    lines.append(idt(4) + f"static constexpr uintptr_t BaseAddr = {hex(periph.base_address)};")

    prev: RegItem = None
    # cycle through the registers and determine if the offsets calculated match what the SVD
    # file says
    for register in registers:
        pad_size = register.offset

        # if no previous entry in this peripheral, that means we're on the first register
        if prev == None:
            # if the offset isn't 0, then that means there is reserved bytes at the start
            # of the register map
            if register.offset != 0x00:
                lines.append("")
                lines.append(idt(4) + f"uint8_t _pad_0x00[{hex(pad_size)}];")
            lines.append(parse_register(register, assert_lines))
        else:
            # If offset of the last element of the previous entry plus the last entry's
            # size goes past the currenr register's offset, then they overlap and we
            # need to reconcile this.

            # This may be because of a malformed SVD file, such as the register size not
            # being specified and the device size not being correct. Another reason may be
            # because the vendor specified a register, but then also specified sections of
            # the register as their own registers rather than as fields.
            if prev.last_element_offset + prev.size_bytes > register.offset:
                # this tries a naive approach to offset conflict resolution.
                # It just takes the difference between the current offset and the
                # previous offset and rounds it to the nearest multiple of 8
                # this _usually_ works, but in the occasions where it doesn't,
                # manual adjustment may be required
                #
                # eventually, I'd like to have a smarter approach that can
                # determine the sizes more robustly, and even possible coalesce
                # registers into fields if it turns out that the overlapping "registers"
                # are actually just subsections of the previous register
                if register.offset >= prev.offset and register.offset + register.size_bytes <= prev.offset + prev.size_bytes:
                    # fully contained, `prev` is the real register, this one is a redundant narrow view.
                    # drop `register` outright, keep prev unmodified, don't advance prev.
                    continue
                else:
                    if infer_size_arg:
                        assert_lines.pop()
                        # remove the last entry
                        lines.pop()
                        # adjust the offset
                        prev.size = (register.offset - prev.offset) * 8

                        lines.append(parse_register(prev, assert_lines))
                    else:
                        warn_str = f"WARNING: previous offset ({hex(prev.offset)}) + ({hex(prev.size_bytes)}) and current "\
                        f"offset ({hex(register.offset)}) for peripheral {name}::{register.name} overlap. Adding {register.name} "\
                        "to map, but manual adjustment will be required. You can try re-running with '--infer-size' to try "\
                        "and automatically resolve the conflict."

                        print(warn_str, file=sys.stderr)

                        faulty_chunk = parse_register(register, assert_lines)
                        # parse_register() always records an assert_lines entry as a side effect
                        # of rendering, whether or not we end up keeping the result. Since this
                        # chunk is about to be commented out, the member won't actually exist, so
                        # undo that push -- otherwise we'd emit a static_assert referencing a
                        # member that was never declared.
                        assert_lines.pop()
                        commented_lines = []
                        for line in faulty_chunk.split("\n"):
                            commented_lines.append("// "+ line)

                        # keep the definition in the map, but comment it out. Will require manual
                        # adjustment to fix. We stop here (continue) rather than falling through to
                        # the normal emission below: that would emit `register` a second time, for
                        # real and uncommented.
                        lines.append("\n".join(commented_lines))
                        prev = register
                        continue


            # this is basically just if the current offset is beyond where the last one
            # ended, we need to add padding between the registers.
            # (the overlap/malformed case above always `continue`s before reaching here,
            # so by this point prev/register are known to be a clean, non-conflicting pair)
            if register.offset - prev.offset != prev.size_bytes:
                itype = int_type(prev.size_bits)
                pad_size = register.offset - prev.last_element_offset - prev.size_bytes
                pad_str = idt(4) + f"uint8_t _pad_{hex(prev.last_element_offset)}[{hex(pad_size)}];"
                lines.append("")
                lines.append(pad_str)

            lines.append(parse_register(register, assert_lines))

        prev = register

    lines.append("};\n")

    # static asserts to ensure alignment and sizing
    for assertion in assert_lines:
        # if this is a register array or cluster, it will have [x] in its name.
        # we just want to omit that in the static_assert
        if "[" in assertion["name"]:
            assertion["name"] = assertion["name"].split("[")[0]
        lines.append(f"static_assert(offsetof({name}, {assertion["name"]}) == {hex(assertion["offset"])});")
    lines.append("")

    # return it as one chunk so if there's something we detect as malformed, we can easily
    # remove the whole chunk
    return "\n".join(lines)


def generate_header(device: SVDDevice):
    chunks = [
        "#pragma once\n"\
        # use the c stddef.h instead of c++ <cstddef> for compilers that don't include a
        # full c++ stdlib
        "#include <stddef.h>\n"\
        "#include \"MMIO.hpp\"\n"
    ]

    for periph in device.get_peripherals():
        chunks.append(generate_peripheral(periph))

    return "\n".join(chunks)


def main():
    global infer_size_arg

    parser = argparse.ArgumentParser(
        description="Generate an MMIO register-overlay header from a CMSIS-SVD file."
    )

    # in the case of overlapping register offsets, attempts to resolve the conflict
    # automatically
    parser.add_argument(
        "-s", "--infer-size", action="store_true",
        help="when a register's declared size overlaps the next register, shrink it to fit "
             "instead of just warning and leaving both in the output"
    )

    parser.add_argument(
        "filename",
        help="path to the input SVD file; output is written alongside it with a .hpp extension"
    )
    args = parser.parse_args()

    infer_size_arg = bool(args.infer_size)
    svd_path: Path = Path(args.filename)

    device: SVDDevice = SVDParser.for_xml_file(str(svd_path)).get_device()
    lines = generate_header(device)

    out_path = svd_path.with_suffix(".hpp")
    out_path.write_text(lines+ "\n")


if __name__ == "__main__":
    main()
