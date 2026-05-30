# Diagnostika Felicia přes K-line

Arduino sketch pro první praktickou diagnostiku Škoda Felicia 1.3 MPI / SIMOS 2P přes starší VAG KW1281 komunikaci na K-line.

## Co umí

- 5baud slow init na adrese motorové jednotky `0x01`
- komunikaci KW1281 na `9600 baud`
- čtení identifikace ECU
- čtení chybových kódů bez mazání
- živá data jen pro tři hodnoty:
  - otáčky motoru
  - napětí baterie
  - úhel škrticí klapky

## Hardware

- Arduino Nano / Uno 5 V
- AltSoftSerial: `D8 = RX`, `D9 = TX`
- tranzistorový K-line převodník z BC547
- K-line na OBD pin 7
- společná zem Arduino/auta

## Knihovna

Nainstalovat v Arduino IDE:

- `AltSoftSerial` od Paula Stoffregena

## Použití

Otevři Serial Monitor na `115200 baud`.

Příkazy:

- `l` = živá data: otáčky, napětí, úhel klapky
- `f` = přečíst chybové kódy ECU
- `t` = test převodníku bez ECU příkazu
- `?` = menu

Sketch neposílá žádné adaptační, kódovací ani mazací příkazy. Je určený jen pro čtení a bezpečné testování komunikace.

## Ověřeno při testu

ECU odpověděla jako `047906030N`, `SIMOS 2P`. Živá data z posledního testu ukázala přibližně:

```text
RPM=0 rpm
Voltage=13.7-13.8 V
Throttle=5.0 deg
```
