# DESFire sample credentials

Test files for the DESFire EV1 emulation. Load one with either front end:

    hf des eload -f resource/desfire_samples/blank-des.dfc

or import it in the GUI's saved-cards view and drop it on a slot.

| File | Contents |
|---|---|
| `blank-des.dfc` | One application (AID 010000), one all-zero D40 DES key, one free-access Standard Data file holding `DE AD BE EF 00 00 00 00`. Access rights `EEEE` means every operation is free, so a reader can read the file without authenticating first. |

Keys in a `.dfc` are stored unencrypted; treat real credential files as
secrets.
