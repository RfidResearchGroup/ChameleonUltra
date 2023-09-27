## Get ChameleonUltraGUI

- [on Google Play](https://play.google.com/store/apps/details?id=io.chameleon.ultra) / [APK](https://nightly.link/GameTec-live/ChameleonUltraGUI/workflows/buildapp/main/apk.zip)
- [on iOS](https://apps.apple.com/dk/app/chameleon-ultra-gui/id6462919364)
- [on Windows](https://nightly.link/GameTec-live/ChameleonUltraGUI/workflows/buildapp/main/windows-installer.zip) ([or without installer](https://nightly.link/GameTec-live/ChameleonUltraGUI/workflows/buildapp/main/windows.zip))
- [on Linux](https://nightly.link/GameTec-live/ChameleonUltraGUI/workflows/buildapp/main/linux.zip)
- [on macOS](https://apps.apple.com/app/chameleon-ultra-gui/id6462919364)


## How to use MFKEY32

- set slot to mifare classic (by uploading an empty dump)
- enable mfkey32 for that slot (toggle in slot settings)
- select slot and present ultra repeatedly to the reader in order to collect nonces
- after collecting some nonces, go back into slot settings and click recover keys

## Troubleshooting

### Font scaling issues on HiDPI screens

If you have font scaling issues (tiny fonts) with the Linux desktop app and a HiDPI screen, you can solve it by running the app as
```
GDK_SCALE=2 GDK_DPI_SCALE=0.5 /usr/local/lib/chameleonultragui/chameleonultragui
```

### Key rocvery from this card is not yet supported
This error may occur when the card you are trying to recovery keys from is requiring a not yet supported recovery technology. You can not tell the required recovery method from the outside.

You may be able to solve this issue by dumping the card with a Proxmark3 or Mifare Classic Tool (Mobile App) and then importing a bin file.