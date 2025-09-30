## 📚 Library Dependencies

### ESP32 (Main Controller)

| Library | Version | Purpose | Installation |
|---------|---------|---------|--------------|
| MFRC522 | 1.4.10 | RFID card reader | `pio lib install "miguelbalboa/MFRC522@^1.4.10"` |
| Adafruit Fingerprint | 2.1.0 | Fingerprint sensor | `pio lib install "adafruit/Adafruit Fingerprint Sensor Library@^2.1.0"` |
| TFT_eSPI | 2.5.0 | TFT display driver | `pio lib install "bodmer/TFT_eSPI@^2.5.0"` |
| TJpg_Decoder | 1.0.8 | JPEG image decoder | `pio lib install "bodmer/TJpg_Decoder@^1.0.8"` |
| ArduinoJson | 6.21.0 | JSON parser | `pio lib install "bblanchon/ArduinoJson@^6.21.0"` |

### Arduino Mega (LCD Controller)

| Library | Version | Purpose | Installation |
|---------|---------|---------|--------------|
| LiquidCrystal_I2C | 1.1.2 | LCD display | Arduino Library Manager |
| Keypad | 3.1.1 | Matrix keypad | Arduino Library Manager |

### ODROID Backend (Python)

```bash
pip install flask==2.3.0 sqlalchemy==2.0.0 opencv-python==4.8.0
