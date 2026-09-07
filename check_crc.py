"""
CRC16-Modbus 验证脚本
【Modbus RTU 请求帧结构】从站地址(1B) + 功能码(1B) + 数据(N B) + CRC(2B,低字节先)
【算法】初始 0xFFFF,多项式 0xA001(反转的 0x8005),输入输出都反转,异或输出 0x0000
"""
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc  # 高字节在后,低字节在前(注意线上是低字节先!)

cases = [
    ("01 03 00 00 00 0A", "经典教学例:C5 CD"),  # 从站 1,读保持寄存器,起始 0,数量 10
    ("01 04 00 00 00 0A", "功能码 04(读输入寄存器)"),
    ("01 03 00 00 00 01", "只读 1 个寄存器"),
]

for hex_str, note in cases:
    data = bytes.fromhex(hex_str.replace(" ", ""))
    crc  = crc16_modbus(data)
    low  = crc & 0xFF
    high = (crc >> 8) & 0xFF
    print(f"[{note}]")
    print(f"  输入: {data.hex(' ').upper()}")
    print(f"  CRC:  0x{crc:04X}  → 线上顺序: {low:02X} {high:02X}")
    print(f"  完整帧: {(data + bytes([low, high])).hex(' ').upper()}")
    print()
