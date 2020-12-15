#!/usr/bin/env python3

"""
Usage: python3 aura.py [hex bytes]

This is a tweaked version of https://github.com/Benzhaomin/pyrmi, licensed under GPLv3

Examples:

./aura.py ec 82 00 00
ec 02
41 55 4c 41 31 2d 53 30 37 32 2d 30 32 30 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
dec 291746156056970610279402714092557633
str AULA1-S072-0208
"""

import usb
import logging
import sys

def is_device_we_want(device):
    return device.idVendor == 0xb05 and device.idProduct in [0x18f3] #18f3 or 0x1872
"""
[
        0x1c0a,  # RM650i
        0x1c0b,  # RM750i
        0x1c0c,  # RM850i
        0x1c0d,  # RM1000i
        0x1c04,  # HX650i
        0x1c05,  # HX750i
        0x1c06,  # HX850i
        0x1c07,  # HX1000i
        0x1c08,  # HX1200i
    ]
"""

dev = usb.core.find(custom_match=is_device_we_want)
if dev is None:
    raise ValueError('No device found')

# grab the device from the kernel's claws
ifaceid = 2
if dev.is_kernel_driver_active(ifaceid):
    dev.detach_kernel_driver(ifaceid)
    usb.util.claim_interface(dev, ifaceid)

try:
    reader = None
    #cfg = dev.get_active_configuration()
    #print(cfg[1,0].endpoints())
    #writer = cfg[(1,0)].endpoints()[0]
    # data is an array of ints
    def write(data):
        padding = [0x0]*(65 - len(data))
        #writer.write(data + padding, timeout=200)
        dev.ctrl_transfer(0x21, 9, 0x02ec, wIndex=2, data_or_wLength=data+padding)

    def read():
        if reader is not None:
            data = reader.read(65, timeout=200)
            print(f'{data[0]:02x} {data[1]:02x}')
            return bytearray(data)[2:]
            #return bytearray(data)

    # send user-provided length+opcode



    write([int(b, 16) for b in sys.argv[1:]])

    # get data back and print in it various encoding
    data = read()

    try:
        for b in data:
            print(f'{b:02x} ', end ='')
        print(' ')
    except Exception as e:
        pass

    try:
        print('dec', int.from_bytes(data, byteorder='little'))
    except Exception as e:
        pass

    try:
        tmp = int.from_bytes(data, byteorder='little')
        exp = tmp >> 11
        fra = tmp & 0x7ff
        if fra > 1023:
            fra = fra - 2048
        if exp > 15:
            exp = exp - 32
        if exp > 15:
            raise ValueError('big number')
        print('lin', fra * 2**exp)
    except Exception as e:
        pass

    try:
        print('str', data.decode())
    except Exception as e:
        pass

finally:
    # always say goodbye
    usb.util.release_interface(dev, ifaceid)
    dev.attach_kernel_driver(ifaceid)
