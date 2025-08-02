   /*
    {BQT38, Pack_bqt38, Unpack_bqt38, 38, {1, 0xFFF, 0x3FFFF, 0x7, 0}},                // BQT 38-bit
    {ISCS, Pack_iscs38, Unpack_iscs38, 38, {1, 0x3FF, 0xFFFFFF, 0, 0x7}},              // ISCS 38-bit
    {PW39, Pack_pw39, Unpack_pw39, 39, {1, 0xFFFF, 0xFFFFF, 0, 0}},                    // Pyramid 39-bit wiegand format
    {P10001, Pack_P10001, Unpack_P10001, 40, {0, 0xFFF, 0xFFFF, 0, 0}},                // HID P10001 Honeywell 40-bit
    {CASI40, pack_casi_rusco40, unpack_casi_rusco40, 40, {0, 0, 0xFFFFFFFFFF, 0, 0}},  // Casi-Rusco 40-bit
    {BC40, Pack_bc40, Unpack_bc40, 40, {1, 0xFFF, 0xFFFFF, 0, 0x7F}},                  // Bundy TimeClock 40-bit
    {H800002, Pack_H800002, Unpack_H800002, 46, {1, 0x3FFF, 0x3FFFFFFF, 0, 0}},        // HID H800002 46-bit
    {C1K48S, Pack_C1k48s, Unpack_C1k48s, 48, {1, 0x003FFFFF, 0x007FFFFF, 0, 0}},       // HID Corporate 1000 48-bit std
    {AVIG56, Pack_Avig56, Unpack_Avig56, 56, {1, 0xFFFFF, 0x3FFFFFFFF, 0, 0}},         // Avigilon 56-bit
    {IR56, pack_ir56, unpack_ir56, 56, {0, 0xFFFFFF, 0xFFFFFFFF, 0, 0}},               // Inner Range 56-bit
    */
/*
static bool Pack_bqt38(wiegand_card_t *card) {
    packed->Length = 38;  // Set number of bits
    set_linear_field(packed, card->FacilityCode, 24, 13);
    set_linear_field(packed, card->CardNumber, 1, 19);
    set_linear_field(packed, card->IssueLevel, 20, 4);

    set_bit_by_position(packed,
                        evenparity32(get_linear_field(packed, 1, 18)), 0);
    set_bit_by_position(packed,
                        oddparity32(get_linear_field(packed, 19, 18)), 37);
    return true;
}

static bool Unpack_bqt38(wiegand_message_t *packed, wiegand_card_t *card) {
    card->FacilityCode = get_linear_field(packed, 24, 13);
    card->CardNumber = get_linear_field(packed, 1, 19);
    card->IssueLevel = get_linear_field(packed, 20, 4);

    card->ParityValid =
        (get_bit_by_position(packed, 0) == evenparity32(get_linear_field(packed, 1, 18))) &&
        (get_bit_by_position(packed, 37) == oddparity32(get_linear_field(packed, 19, 18)));
    return true;
}

static bool Pack_iscs38(wiegand_card_t *card) {
    packed->Length = 38;  // Set number of bits

    set_linear_field(packed, card->FacilityCode, 5, 10);
    set_linear_field(packed, card->CardNumber, 15, 22);
    set_linear_field(packed, card->OEM, 1, 4);

    set_bit_by_position(packed,
                        evenparity32(get_linear_field(packed, 1, 18)), 0);
    set_bit_by_position(packed,
                        oddparity32(get_linear_field(packed, 19, 18)), 37);
    return true;
}

static bool Unpack_iscs38(wiegand_message_t *packed, wiegand_card_t *card) {
    card->FacilityCode = get_linear_field(packed, 5, 10);
    card->CardNumber = get_linear_field(packed, 15, 22);
    card->OEM = get_linear_field(packed, 1, 4);
    card->ParityValid =
        (get_bit_by_position(packed, 0) == evenparity32(get_linear_field(packed, 1, 18))) &&
        (get_bit_by_position(packed, 37) == oddparity32(get_linear_field(packed, 19, 18)));
    return true;
}

static bool Pack_pw39(wiegand_card_t *card) {
    packed->Length = 39;  // Set number of bits
    set_linear_field(packed, card->FacilityCode, 1, 17);
    set_linear_field(packed, card->CardNumber, 18, 20);

    set_bit_by_position(packed,
                        evenparity32(get_linear_field(packed, 1, 18)), 0);
    set_bit_by_position(packed,
                        oddparity32(get_linear_field(packed, 19, 19)), 38);
    return true;
}

static bool Unpack_pw39(wiegand_message_t *packed, wiegand_card_t *card) {
    card->FacilityCode = get_linear_field(packed, 1, 17);
    card->CardNumber = get_linear_field(packed, 18, 20);

    card->ParityValid =
        (get_bit_by_position(packed, 0) == evenparity32(get_linear_field(packed, 1, 18))) &&
        (get_bit_by_position(packed, 38) == oddparity32(get_linear_field(packed, 19, 19)));
    return true;
}

static bool Pack_P10001(wiegand_card_t *card) {
    memset(packed, 0, sizeof(wiegand_message_t));

    if (!validate_card_limit(format_idx, card)) return false;

    packed->Length = 40;  // Set number of bits
    set_linear_field(packed, 0xF, 0, 4);
    set_linear_field(packed, card->FacilityCode, 4, 12);
    set_linear_field(packed, card->CardNumber, 16, 16);
    set_linear_field(packed,
                     get_linear_field(packed, 0, 8) ^
                         get_linear_field(packed, 8, 8) ^
                         get_linear_field(packed, 16, 8) ^
                         get_linear_field(packed, 24, 8),
                     32, 8);
    return true;
}

static bool Unpack_P10001(wiegand_message_t *packed, wiegand_card_t *card) {
    card->CardNumber = get_linear_field(packed, 16, 16);
    card->FacilityCode = get_linear_field(packed, 4, 12);
    card->ParityValid = (get_linear_field(packed, 0, 8) ^
                         get_linear_field(packed, 8, 8) ^
                         get_linear_field(packed, 16, 8) ^
                         get_linear_field(packed, 24, 8)) == get_linear_field(packed, 32, 8);
    return true;
}

static bool pack_casi_rusco40(wiegand_card_t *card) {
    packed->Length = 40;  // Set number of bits
    set_linear_field(packed, card->CardNumber, 1, 38);
    return true;
}

static bool unpack_casi_rusco40(wiegand_message_t *packed, wiegand_card_t *card) {
    card->CardNumber = get_linear_field(packed, 1, 38);
    return true;
}

static bool Pack_bc40(wiegand_card_t *card) {
    packed->Length = 40;  // Set number of bits
    set_linear_field(packed, card->OEM, 0, 7);
    // cost center 12
    set_linear_field(packed, card->FacilityCode, 7, 12);
    set_linear_field(packed, card->CardNumber, 19, 19);
    set_bit_by_position(packed,
                        oddparity32(get_linear_field(packed, 19, 19)), 39);
    return true;
}

static bool Unpack_bc40(wiegand_message_t *packed, wiegand_card_t *card) {
    card->OEM = get_linear_field(packed, 0, 7);
    card->FacilityCode = get_linear_field(packed, 7, 12);
    card->CardNumber = get_linear_field(packed, 19, 19);
    card->ParityValid =
        (get_bit_by_position(packed, 39) == oddparity32(get_linear_field(packed, 19, 19)));
    return true;
}

static bool Pack_H800002(wiegand_card_t *card) {
    int even_parity = 0;
    memset(packed, 0, sizeof(wiegand_message_t));

    packed->Length = 46;
    set_linear_field(packed, card->FacilityCode, 1, 14);
    set_linear_field(packed, card->CardNumber, 15, 30);

    // Parity over 44 bits
    even_parity = evenparity32((packed->Bot >> 1) ^ (packed->Mid & 0x1fff));
    set_bit_by_position(packed, even_parity, 0);
    // Invert parity for setting odd parity
    set_bit_by_position(packed, even_parity ^ 1, 45);
    return true;
}

static bool Unpack_H800002(wiegand_message_t *packed, wiegand_card_t *card) {
    int even_parity = 0;
    memset(card, 0, sizeof(wiegand_card_t));

    card->FacilityCode = get_linear_field(packed, 1, 14);
    card->CardNumber = get_linear_field(packed, 15, 30);
    even_parity = evenparity32((packed->Bot >> 1) ^ (packed->Mid & 0x1fff));
    card->ParityValid = get_bit_by_position(packed, 0) == even_parity;
    // Invert logic to compare against oddparity
    card->ParityValid &= get_bit_by_position(packed, 45) != even_parity;
    return true;
}

static bool Pack_C1k48s(wiegand_card_t *card) {
    packed->Length = 48;  // Set number of bits
    packed->Bot |= (card->CardNumber & 0x007FFFFF) << 1;
    packed->Bot |= (card->FacilityCode & 0x000000FF) << 24;
    packed->Mid |= (card->FacilityCode & 0x003FFF00) >> 8;
    packed->Mid |= (evenparity32((packed->Mid & 0x00001B6D) ^ (packed->Bot & 0xB6DB6DB6))) << 14;
    packed->Bot |= (oddparity32((packed->Mid & 0x000036DB) ^ (packed->Bot & 0x6DB6DB6C)));
    packed->Mid |= (oddparity32((packed->Mid & 0x00007FFF) ^ (packed->Bot & 0xFFFFFFFF))) << 15;

    return true;
}

static bool Unpack_C1k48s(wiegand_message_t *packed, wiegand_card_t *card) {
    card->CardNumber = (packed->Bot >> 1) & 0x007FFFFF;
    card->FacilityCode = ((packed->Mid & 0x00003FFF) << 8) | ((packed->Bot >> 24));
    card->ParityValid =
        (evenparity32((packed->Mid & 0x00001B6D) ^ (packed->Bot & 0xB6DB6DB6)) == ((packed->Mid >> 14) & 1)) &&
        (oddparity32((packed->Mid & 0x000036DB) ^ (packed->Bot & 0x6DB6DB6C)) == ((packed->Bot >> 0) & 1)) &&
        (oddparity32((packed->Mid & 0x00007FFF) ^ (packed->Bot & 0xFFFFFFFF)) == ((packed->Mid >> 15) & 1));
    return true;
}

static bool Pack_Avig56(wiegand_card_t *card) {
    packed->Length = 56;
    set_linear_field(packed, card->FacilityCode, 1, 20);
    set_linear_field(packed, card->CardNumber, 21, 34);

    bool even_parity_valid = step_parity_check(packed, 0, 28, true);
    set_bit_by_position(packed, !even_parity_valid, 0);

    bool odd_parity_valid = step_parity_check(packed, 28, 28, false);
    set_bit_by_position(packed, !odd_parity_valid, 55);
    return true;
}

static bool Unpack_Avig56(wiegand_message_t *packed, wiegand_card_t *card) {
    card->FacilityCode = get_linear_field(packed, 1, 20);
    card->CardNumber = get_linear_field(packed, 21, 34);

    // Check step parity for every 2 bits
    bool even_parity_valid = step_parity_check(packed, 0, 28, true);
    bool odd_parity_valid = step_parity_check(packed, 28, 28, false);

    card->ParityValid = even_parity_valid && odd_parity_valid;
    return true;
}

static uint64_t pack_ir56(wiegand_card_t *card) {
    packed->Length = 56;
    packed->Bot = card->CardNumber;
    packed->Mid = card->FacilityCode;
    return true;
}

static wiegand_card_t *unpack_ir56(uint64_t hi, uint64_t lo) {
    card->FacilityCode = packed->Mid;
    card->CardNumber = packed->Bot;
    return true;
}
*/
