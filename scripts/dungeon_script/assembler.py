# shoutouts to ChatGPT who generated this code based on the C code
# that implements the interpreter and an example script alone

import struct
import sys
import shlex
from argparse import ArgumentParser

# Type codes for packing struct
TYPE_UINT8 = 'B'
TYPE_UINT16 = 'H'
TYPE_INT32 = 'i'
TYPE_UINT32 = 'I'
TYPE_STRING = 's'
TYPE_DIR = "DIRECTION"

DIR_MAP = {
    "DOWN": 0,
    "DOWNRIGHT": 1,
    "RIGHT": 2,
    "UPRIGHT": 3,
    "UP": 4,
    "UPLEFT": 5,
    "LEFT": 6,
    "DOWNLEFT": 7
}

RANGES = {
    TYPE_UINT8: (0, 255),
    TYPE_UINT16: (0, 65535),
    TYPE_INT32: (-2147483648, 2147483647),
    TYPE_UINT32: (0, 4294967295),
}

class Instruction:
    def __init__(self, opcode, params):
        self.opcode = opcode
        self.params = params

instructions = {
    "end": Instruction(0x0, { "exitcode": TYPE_UINT8 }),
    "dialogue": Instruction(0x1, { "char": TYPE_UINT8, "portrait": TYPE_UINT8, "string": TYPE_STRING }),
    "dialoguewait": Instruction(0x2, { "char": TYPE_UINT8, "emotion": TYPE_UINT8, "string": TYPE_STRING }),
    "effect": Instruction(0x3, { "char": TYPE_UINT8, "effect": TYPE_UINT16 }),
    "effectwait": Instruction(0x4, { "char": TYPE_UINT8, "effect": TYPE_UINT16 }),
    "wait": Instruction(0x5, { "frames": TYPE_UINT16 }),
    "playsfx": Instruction(0x6, { "sfx_id": TYPE_UINT16 }),
    "playme": Instruction(0x7, { "sfx_id": TYPE_UINT16 }),
    "jump": Instruction(0x8, { "target": TYPE_INT32 }),
    "jumpeq": Instruction(0x9, { "target": TYPE_INT32 }),
    "jumpne": Instruction(0xA, { "target": TYPE_INT32 }),

    "yesno": Instruction(0x10, { "char": TYPE_UINT8, "portrait": TYPE_UINT8, "default": TYPE_UINT8, "string": TYPE_STRING }),

    "checkitem": Instruction(0x20, { "item": TYPE_UINT16, "amount": TYPE_UINT8 }),
    "giveitem": Instruction(0x21, { "item": TYPE_UINT16, "amount": TYPE_UINT8 }),
    "takeitem": Instruction(0x22, { "item": TYPE_UINT16 }),
    "checkbagcount": Instruction(0x23, { "amount": TYPE_UINT8 }),
    "checkmove": Instruction(0x2A, { "char": TYPE_UINT8, "move": TYPE_UINT16 }),

    "checktalkflag": Instruction(0x30, { "idx": TYPE_UINT8 }),
    "settalkflag": Instruction(0x31, { "idx": TYPE_UINT8, "val": TYPE_UINT8 }),

    "checkattacked": Instruction(0x42, { "char": TYPE_UINT8 }),
    "warp": Instruction(0x45, { "char": TYPE_UINT8, "type": TYPE_UINT8, "x": TYPE_UINT8, "y": TYPE_UINT8 }),
    "checkapparentspecies": Instruction(0x46, { "char": TYPE_UINT8, "species": TYPE_UINT16 }),
    "setnextfloorflag": Instruction(0x47, {}),

    "setanimation": Instruction(0x48, { "char": TYPE_UINT8, "anim_group_id": TYPE_UINT8 }),
    "setdirection": Instruction(0x49, { "char": TYPE_UINT8, "direction": TYPE_DIR }),
    "displayaction": Instruction(0x50, {}),
    "walk": Instruction(0x51, { "char": TYPE_UINT8, "direction": TYPE_DIR }),
    "pickitem": Instruction(0x52, { "char": TYPE_UINT8 }),
    "usemove": Instruction(0x53, {"char": TYPE_UINT8, "move_key": TYPE_UINT8}),
    "initdungeonrng": Instruction(0x54, {"seed": TYPE_UINT32}),
    "setmonstermove": Instruction(0x55, {"char": TYPE_UINT8, "slot": TYPE_UINT8, "move_id": TYPE_UINT16, "pp": TYPE_UINT8})
}

def disassemble_script(binary_data):
    ip = 0
    output = []
    while ip < len(binary_data):
        start_ip = ip
        opcode = struct.unpack('<B', binary_data[ip:ip+1])[0]
        ip += 1

        # Reverse lookup the opcode to instruction
        instruction = None
        for name, instr in instructions.items():
            if instr.opcode == opcode:
                instruction = name
                break
        if not instruction:
            raise ValueError(f"Unknown opcode {opcode}")

        params = []
        for param, type_code in instructions[instruction].params.items():
            storage_type_code = type_code
            if type_code == TYPE_DIR:
                storage_type_code = TYPE_UINT8
                
            if storage_type_code == TYPE_STRING:
                size = struct.unpack('<H', binary_data[ip:ip+2])[0]
                ip += 2
                storage_param_value = '"' + binary_data[ip:ip+size].decode('windows-1252').replace('\n', '\\n') + '"'
                ip += size
            else:
                size = struct.calcsize(storage_type_code)
                storage_param_value = struct.unpack('<' + storage_type_code, binary_data[ip:ip+size])[0]
                ip += size

            if type_code == TYPE_DIR:
                param_value = None
                for k, v in DIR_MAP:
                    if int(storage_param_value) == v:
                        param_value = k
                if param_value is None:
                    raise ValueError(f"Invalid direction value {storage_param_value}")
            else:
                param_value = storage_param_value
            params.append(str(param_value))

        # Append the instruction with the offset as a comment
        output.append(f"{instruction} {' '.join(params)} # {start_ip:#04x}")

    return '\n'.join(output)

def assemble_script(input_data):
    output = b''
    label_offsets = {}
    jump_locations = []
    line_nb = 0

    # First pass: Generate all code with jump offsets set to 0 and record label and jump locations
    for line in input_data:
        line_nb += 1
        line = line.split('#')[0].strip()  # Ignore comments
        if not line:
            continue
        tokens = shlex.split(line)

        if len(tokens) == 0:
            continue
        instruction_name = tokens[0].lower()

        # Check for labels
        if instruction_name.endswith(':'):
            label_name = instruction_name[:-1]
            label_offsets[label_name] = len(output)  # Record the label's offset
            continue

        # If it's not a label, it's an instruction
        instruction = instructions.get(instruction_name)
        if not instruction:
            raise ValueError(f"Unknown instruction {instruction_name}")

        params = []
        for token in tokens[1:]:
            if token.startswith('@'):  # This is a label reference
                params.append(0)  # Use a placeholder offset for now
                # Record the location and target of the jump
                # 1 is added to the location to account for the opcode
                jump_locations.append((len(output) + 1, token[1:])) 
            elif token.isdigit():
                params.append(int(token))
            else:
                params.append(token)

        instr_bytes = assemble(instruction_name, line_nb, params)
        output += instr_bytes

    # Second pass: Update jump offsets
    for jump_location, label_name in jump_locations:
        if label_name not in label_offsets:
            raise ValueError(f"Unknown label {label_name}")
        relative_offset = label_offsets[label_name] - jump_location + 1 # Account for the opcode
        output = output[:jump_location] + struct.pack('<i', relative_offset) + output[jump_location+4:]

    return output

def assemble(instruction_name, line_nb, args):
    if instruction_name not in instructions:
        raise ValueError("Invalid instruction")
    
    instruction = instructions[instruction_name]

    # Check that the number of arguments matches the number of parameters
    if len(args) != len(instruction.params):
        raise ValueError(f"{instruction_name} requires {len(instruction.params)} arguments, {len(args)} given")

    packed_params = []
    for (param, type_), value in zip(instruction.params.items(), args):
        if type_ == TYPE_DIR:
            type_ = TYPE_UINT8
            value = DIR_MAP[str(value)]
        if type_ == TYPE_STRING:
            # Check if the argument is indeed a string
            if not isinstance(value, str):
                raise ValueError(f"{param} must be a string")
            value = value.replace("\\n", "\n").encode('windows-1252') + b'\0' # Null-terminate the string
            packed_params.append(struct.pack('<H', len(value)) + value)
        else:
            # Check if the argument is a number and within range
            if not isinstance(value, int) or not RANGES[type_][0] <= value <= RANGES[type_][1]:
                raise ValueError(f"{param} must be a number within {RANGES[type_]} at line {line_nb}")
            if instruction_name == "setmonstermove" and param == "slot" and value >= 4:
                raise ValueError(f"{param} is greater than 3")
            packed_params.append(struct.pack('<' + type_, value))

    return struct.pack('<B', instruction.opcode) + b''.join(packed_params)

def main():
    parser = ArgumentParser()
    parser.add_argument('input', nargs='?', type=str, default='-')
    parser.add_argument('-o', '--output', type=str, default='-')
    parser.add_argument('-d', '--disassemble', action='store_true', help='Disassemble instead of assemble')
    args = parser.parse_args()

    if args.disassemble:
        if args.input == '-':
            binary_data = sys.stdin.buffer.read()
        else:
            with open(args.input, 'rb') as f:
                binary_data = f.read()
        output_data = disassemble_script(binary_data)
    else:
        if args.input == '-':
            input_data = sys.stdin
        else:
            input_data = open(args.input, 'r')
        output_data = assemble_script(input_data)
        if args.input != '-':
            input_data.close()

    if args.output == '-':
        print(output_data)
    else:
        with open(args.output, 'w' if args.disassemble else 'wb') as f:
            f.write(output_data)

if __name__ == "__main__":
    main()
