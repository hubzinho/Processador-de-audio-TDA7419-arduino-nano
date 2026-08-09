# PROCESSADOR DE AUDIO TDA7419

Controlador de áudio automotivo baseado no chip **TDA7419**, rodando em um **Arduino Nano**, com display **ST7735 (TFT 160x128)** via Ucglib e navegação por **encoder rotativo**.

## Funcionalidades

- **Volume, Input e Gain** — controle direto por entrada selecionada
- **EQ gráfico unificado** — Bass / Mid / Treble em uma única tela com barras, navegação por clique curto entre as bandas
- **Balance (Fader L/R)** — ajuste bidirecional simétrico
- **Loudness** — atenuação + frequência de corte
- **Filtros configuráveis** — frequência e Q para Treble, Mid e Bass
- **Spectrum Analyzer** — 8 barras em tempo real lidas do TDA7419
- **Bluetooth** *(novo)* — liga/desliga um módulo Bluetooth externo direto do menu
- Estado persistido em **EEPROM** (salvo automaticamente 3s após o último ajuste)

## Bluetooth

Nova opção no menu principal. Ao entrar na tela, o encoder alterna entre **SIM** e **NÃO**:

- **SIM** → pino `D6` vai para **0V (LOW)** e a entrada é forçada para **3** (entrada dedicada ao módulo BT)
- **NÃO** → pino `D6` volta para **5V (HIGH)**, desligando o módulo
- Clique (curto ou longo) sai da tela e volta ao menu

Se o usuário entrar na opção **Input** com o Bluetooth ainda ativo, o firmware desliga automaticamente o pino `D6` (5V) e a flag interna de Bluetooth, sem alterar o valor de entrada atual — a opção Input volta a se comportar normalmente entre 1, 2 e 3. Selecionar a entrada 3 manualmente pela opção Input não afeta o pino `D6`.

## Hardware

| Componente          | Pino / Interface     |
|---------------------|-----------------------|
| Encoder (DT/CLK)    | D2 / D3               |
| Botão do encoder    | A2                    |
| Display TFT (CS/DC/RST) | D10 / D7 / D8     |
| Spectrum (OUT/CLK)  | A0 / A1                |
| Controle Bluetooth  | D6                     |
| TDA7419             | I2C (Wire)             |

## VISTA DA PLACA + UI

<img width="1256" height="1001" alt="IMG_20260809_111148 jpg" src="https://github.com/user-attachments/assets/847f82a7-17cd-4def-98ed-2bedccb0d3a4" />

<img width="1256" height="1001" alt="IMG_20260809_111224 jpg" src="https://github.com/user-attachments/assets/48231170-d69a-48ca-bfc9-755fbb350f45" />

<img width="1129" height="1077" alt="IMG_20260809_111250 jpg" src="https://github.com/user-attachments/assets/68968e35-0a36-4793-8c98-0f0686927354" />

<img width="1046" height="973" alt="IMG_20260809_115747 jpg" src="https://github.com/user-attachments/assets/a4070c07-84a9-4e60-a2d4-bd803b5a7da8" />


## Dependências (Arduino Library Manager)

- `Wire`
- `TDA7419`
- `Encoder`
- `Ucglib`
- `EEPROM`

## Compilando

Aberto no Arduino IDE, selecionar placa **Arduino Nano** (ATmega328P) e enviar normalmente. O sketch é otimizado para caber no limite de flash de 30720 bytes do Nano.

## Estrutura

```
bubu_audio.ino   # firmware completo
```

## Licença

Projeto pessoal / hobby — sinta-se à vontade para usar como referência.
