# HAP

HAP je ovladač výhřevu sedaček do auta pro Arduino Nano.

Verze: **V1.2**

## Co projekt dělá

- Ovládá výhřev levé a pravé sedačky.
- Každá sedačka má třípolohové ovládání: `OFF`, `LOW`, `HIGH`.
- Spíná čtyři relé: levá nízký/vysoký stupeň a pravá nízký/vysoký stupeň.
- Zobrazuje stav sedaček a napětí palubní sítě na OLED displeji SH1106 128x64.
- Hlídá podpětí a přepětí, aby se výhřev vypnul při nevhodném napájení.
- Po nečinnosti spustí animaci pouze tehdy, když jsou obě sedačky vypnuté.

## Hardware

- Arduino Nano
- OLED displej SH1106 128x64 přes I2C
- 4 vstupy z třípolohových přepínačů
- 4 aktivně-low relé pro výhřev
- Dělič napětí na analogový vstup A0 pro měření palubního napětí

## Zapojení pinů

| Funkce | Pin |
| --- | --- |
| Levá sedačka LOW | D2 |
| Levá sedačka HIGH | D3 |
| Pravá sedačka LOW | D4 |
| Pravá sedačka HIGH | D5 |
| Pomocný výstup pro HIGH režim | D6 |
| Relé levá LOW | D7 |
| Relé levá HIGH | D8 |
| Relé pravá LOW | D9 |
| Relé pravá HIGH | D10 |
| Měření napětí | A0 |
| OLED SDA | A4 |
| OLED SCL | A5 |

Vstupy používají interní `INPUT_PULLUP`, takže aktivní stav je sepnutí proti GND.

Relé jsou aktivně-low: `LOW = sepnuto`, `HIGH = vypnuto`.

## Ochrany

Software hlídá napětí:

- podpětí vypne výhřev pod `11.8 V`
- návrat z podpětí nastane nad `12.4 V`
- přepětí vypne výhřev nad `15.5 V`
- návrat z přepětí nastane pod `15.0 V`

Když je u jedné sedačky současně sepnutý vstup `LOW` i `HIGH`, program to bere jako chybu přepínače a výhřev vypne.

Teplotní ochranu neřeší Arduino. Tu řeší samotné vyhřívací deky vlastním odpojovacím čidlem.

## Displej

Horní část displeje ukazuje stav levé a pravé sedačky:

- `OFF`
- `LOW`
- `HIGH`

Spodní část ukazuje napětí. Při problému zobrazí hlášení:

- `NIZKE NAPETI`
- `VYSOKE NAPETI`
- `CHYBA SPINACE`

Animace nečinnosti se spustí po 5 minutách pouze tehdy, když jsou obě sedačky vypnuté a není aktivní žádná chyba.

## Nahrání do Arduino Nano

V Arduino IDE otevři soubor `HAP.ino`, vyber:

- deska: `Arduino Nano`
- procesor podle konkrétní desky, typicky `ATmega328P` nebo `ATmega328P (Old Bootloader)`
- správný COM port

Použité knihovny:

- `Wire`
- `U8g2`
- `avr/wdt`

## Poznámky

Konstanta `KALIBRACE_NAPETI` ve funkci `prectiPrumerneNapeti()` závisí na použitém děliči napětí. Po zapojení v autě je dobré porovnat zobrazenou hodnotu s multimetrem a případně ji doladit.

Arduino Nano má omezenou RAM. Sketch používá full-buffer režim knihovny U8g2, proto při kompilaci může Arduino IDE hlásit varování o nízké paměti.
