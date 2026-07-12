#!/usr/bin/env python3
"""
gen_emv.py - Generate VietQR EMV CoQR payload từ URL vietqr.app

Cách dùng:
    python gen_emv.py "https://vietqr.app/img?acc=977334560&bank=Vietcombank&amount=30000&des=Chuyen+khoan+mua+hang"

Output:
    EMV payload string (dán vào ESP32 / lv_qrcode để generate QR)

Chuẩn: NAPAS VietQR (EMVCo MPM QR Code) - Thông tư NHNN 2022
Không cần pip install - chỉ dùng stdlib.
"""
import sys
import urllib.parse


# =============================================================================
# Bank name → BIN code table (NAPAS)
# Thêm bank mới vào đây nếu thiếu
# =============================================================================
BANK_BIN = {
    # Top phổ biến
    "Vietcombank": "970436",
    "Vietcombank (VCB)": "970436",
    "VCB": "970436",
    "VietinBank": "970415",
    "Vietinbank": "970415",
    "BIDV": "970424",
    "Agribank": "970405",
    "AGRIBANK": "970405",
    # Thương mại cổ phần
    "MB Bank": "970422",
    "MBBank": "970422",
    "MB": "970422",
    "Techcombank": "970407",
    "TCB": "970407",
    "ACB": "970416",
    "TPBank": "970523",
    "VPBank": "970432",
    "HDBank": "970437",
    "VIB": "970441",
    "OCB": "970448",
    "MSB": "970426",
    "Bac A Bank": "970409",
    "BacA Bank": "970409",
    "Nam A Bank": "970428",
    "Nam A": "970428",
    "SHB": "970443",
    "Sacombank": "970403",
    "Sacombank (STB)": "970403",
    "Saigonbank": "970400",
    "Vietbank": "970433",
    "PVB": "970418",
    "Oceanbank": "970414",
    "GPBank": "970408",
    "CGD": "970419",
    # Digi / app bank
    "Cake": "970448",
    "Timo": "970422",
    "TNEX": "970423",
    "TikiJP": "970427",
    "Lotte": "970401",
    "KienLongBank": "970452",
    "Kienlongbank": "970452",
    "Vietcapital": "970454",
    "VietCapitalBank": "970454",
    "PVcombank": "970430",
    "Coopbank": "970446",
    "BaoViet Bank": "970438",
    "Baoviet": "970438",
    "VRB": "970421",
    # Quân đội / chính sách
    "MB (Military Bank)": "970422",
    # Liên doanh
    "Shinhan": "970424",
    "Woori": "970402",
    "HSBC": "970412",
    "Standard Chartered": "970400",
    "Public Bank": "970435",
    "UOB": "970406",
}

# GUID NAPAS cho VietQR (fix, công khai)
NAPAS_GUID = "A000000727"


# =============================================================================
# CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF (chuẩn EMV)
# =============================================================================
def crc16_ccitt_false(data: str) -> str:
    crc = 0xFFFF
    for byte in data.encode("ascii"):
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return f"{crc:04X}"


# =============================================================================
# TLV helper: tag(2) + length(2 digit) + value
# =============================================================================
def tlv(tag: str, value: str) -> str:
    assert len(value) <= 99, f"Tag {tag} value too long: {len(value)}"
    return f"{tag}{len(value):02d}{value}"


# =============================================================================
# Build EMV CoQR payload
# =============================================================================
def build_emv(account: str, bank_bin: str,
              amount: str = None, description: str = None) -> str:
    """
    Build EMV CoQR payload theo NAPAS VietQR spec.

    account     : số tài khoản (string)
    bank_bin    : mã ngân hàng 6 chữ số (BIN code)
    amount      : số tiền VND (string). None = QR tĩnh
    description : nội dung CK (string, optional)
    """
    # Tag 38 - Merchant Account Information (VietQR format)
    # Theo spec VietQR.io / NAPAS (3 sub-tag flat):
    #   sub 00: GUID NAPAS = "A000000727"
    #   sub 01: nested TLV {BIN + account}
    #   sub 02: service code = "QRIBFTTA"
    sub_00 = tlv("00", NAPAS_GUID)
    sub_01 = tlv("01", tlv("00", bank_bin) + tlv("01", account))
    sub_02 = tlv("02", "QRIBFTTA")
    tag_38 = tlv("38", sub_00 + sub_01 + sub_02)

    # Tag 62 - Additional Data Field Template
    # Sub-tag 08: Purpose of Transaction (nội dung CK)
    tag_62 = ""
    if description:
        tag_62 = tlv("62", tlv("08", description))

    # Assemble payload (chưa có CRC)
    payload = tlv("00", "01")                      # Payload Format Indicator
    if amount:
        payload += tlv("01", "12")                 # Dynamic (có amount)
    else:
        payload += tlv("01", "11")                 # Static
    payload += tag_38                              # Merchant Account
    payload += tlv("53", "704")                    # Currency VND
    if amount:
        payload += tlv("54", amount)               # Amount
    payload += tlv("58", "VN")                     # Country
    if tag_62:
        payload += tag_62                          # Additional Data
    payload += "6304"                              # CRC tag + length placeholder

    # Tính CRC trên toàn payload (bao gồm "6304")
    crc = crc16_ccitt_false(payload)
    return payload + crc


# =============================================================================
# Parse URL vietqr.app/img?acc=...&bank=...&amount=...&des=...
# (cũng chấp nhận URL qr.sepay.vn hoặc img.vietqr.io với format tương tự)
# =============================================================================
def parse_vietqr_url(url: str) -> dict:
    parsed = urllib.parse.urlparse(url)
    qs = urllib.parse.parse_qs(parsed.query)

    # Normalize key (một số service dùng addInfo thay vì des)
    def first(key, default=""):
        return qs.get(key, [default])[0]

    return {
        "account":  first("acc"),
        "bank":     first("bank"),
        "amount":   first("amount", "") or None,
        "des":      urllib.parse.unquote_plus(first("des", "")) or None,
        "holder":   urllib.parse.unquote_plus(first("holder", "")) or None,
    }


def resolve_bank_bin(bank_input: str) -> str:
    """Match tên ngân hàng với BIN table. Cho phép partial match."""
    if not bank_input:
        raise ValueError("Thiếu tên ngân hàng (?bank=)")

    # Exact match
    if bank_input in BANK_BIN:
        return BANK_BIN[bank_input]

    # Đã là BIN rồi (6 số)
    if bank_input.isdigit() and len(bank_input) == 6:
        return bank_input

    # Partial match (không phân biệt hoa thường)
    lower = bank_input.lower()
    for name, code in BANK_BIN.items():
        if lower in name.lower() or name.lower() in lower:
            return code

    raise ValueError(
        f"Không nhận ra ngân hàng '{bank_input}'. "
        f"Thêm vào BANK_BIN table hoặc dùng BIN 6 số."
    )


# =============================================================================
# Main
# =============================================================================
def main():
    if len(sys.argv) < 2:
        print("Cách dùng:")
        print('  python gen_emv.py "https://vietqr.app/img?acc=...&bank=...&amount=..."')
        print()
        print("Ví dụ:")
        print('  python gen_emv.py "https://vietqr.app/img?acc=977334560&bank=Vietcombank&amount=30000&des=Chuyen+khoan+mua+hang"')
        sys.exit(1)

    url = sys.argv[1]
    info = parse_vietqr_url(url)

    if not info["account"]:
        print("ERROR: URL thiếu ?acc=<số tài khoản>")
        sys.exit(1)

    try:
        bank_bin = resolve_bank_bin(info["bank"])
    except ValueError as e:
        print(f"ERROR: {e}")
        sys.exit(1)

    emv = build_emv(
        account=info["account"],
        bank_bin=bank_bin,
        amount=info["amount"],
        description=info["des"],
    )

    print("=== Input ===")
    print(f"  URL    : {url}")
    print(f"  Bank   : {info['bank']}  → BIN {bank_bin}")
    print(f"  Account: {info['account']}")
    if info["amount"]:
        print(f"  Amount : {info['amount']} VND")
    if info["des"]:
        print(f"  Desc   : {info['des']}")
    print()
    print("=== EMV payload (VietQR / EMV CoQR) ===")
    print(emv)
    print()
    print(f"Length: {len(emv)} ký tự")
    print()
    print("=== Test MQTT publish tới ESP32 ===")
    import json as _json
    payload = _json.dumps({
        "amount": int(info["amount"]) if info["amount"] else 0,
        "qr": emv,
    })
    print(f'mosquitto_pub -h broker.hivemq.com -p 1883 '
          f'-t "cucquy/esp_01/notify" -m \'{payload}\'')


if __name__ == "__main__":
    main()
