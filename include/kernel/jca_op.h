#ifndef JACKBOT_JCA_OP_H
#define JACKBOT_JCA_OP_H
/* JCA bytecode opcodes. Shared by the JCA language front end (kernel/jca.c)
   and the C front end (kernel/jcac.c) so both emit the same instruction set
   into the same struct jca_program. Byte order of the operands is the same
   little-endian u32 the VM reads, and jump targets are absolute code offsets. */
enum {
    OP_HALT = 0, OP_PUSHI, OP_PUSHS, OP_PUSHNIL,
    OP_LOAD, OP_STORE, OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE,
    OP_AND, OP_OR, OP_NOT,
    OP_JMP, OP_JZ, OP_JNZ,
    OP_CALL, OP_RET,
    OP_BPRINT, OP_BLEN, OP_BSTR, OP_BINT, OP_BABS
};

/* Operand sizes in bytes following the opcode byte; kept in sync with the
   disassembler length table. */
#define JCA_OP_OPERANDS 5
#define JCA_CALL_OPERANDS 9
#endif
