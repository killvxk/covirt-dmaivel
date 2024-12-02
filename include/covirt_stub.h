#pragma once

#define __covirt_vm_start() \
    __asm__ __volatile__ (".byte 0x67, 0x48, 0x0F, 0x1F, 0x84, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x66, 0x67, 0x0F, 0x1F, 0x04, 0x00\n\t")

#define __covirt_vm_end() \
    __asm__ __volatile__ (".byte 0x67, 0x48, 0x0F, 0x1F, 0x84, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x66, 0x67, 0x0F, 0x1F, 0x04, 0x01\n\t")

#define __covirt_vm_start_bytes \
    "\x67\x48\x0F\x1F\x84\x00\xDE\xAD\xBE\xEF\x66\x67\x0F\x1F\x04\x00"

#define __covirt_vm_end_bytes \
    "\x67\x48\x0F\x1F\x84\x00\xDE\xAD\xBE\xEF\x66\x67\x0F\x1F\x04\x01"

#define __covirt_vm_stub_length0 \
    10

#define __covirt_vm_stub_length1 \
    6

#define __covirt_vm_stub_length \
    (__covirt_vm_stub_length0 + __covirt_vm_stub_length1)
    