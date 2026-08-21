//CODIGO FINAL
#include <Wire.h>
#include <TDA7419.h>
#include <Encoder.h>
#include <Ucglib.h>
#include <EEPROM.h>

// ===== PINOS =====
#define ENC_DT  2
#define ENC_CLK 3
#define ENC_SW  A2

#define TFT_CS  10
#define TFT_DC   7
#define TFT_RST  8

#define SPEC_OUT A0
#define SPEC_CLK A1

#define BT_PIN   6   // Controle do módulo Bluetooth: LOW = ativo, HIGH = inativo

// ===== SPECTRUM =====
#define BAR_COUNT 8
#define BAR_W     14
#define BAR_GAP    6
#define SPEC_H   100
#define SPEC_Y0   20
#define SPEC_Y1  120

// ===== OBJETOS =====
TDA7419 tda;
Encoder enc(ENC_DT, ENC_CLK);
Ucglib_ST7735_18x128x160_HWSPI ucg(TFT_DC, TFT_CS, TFT_RST);

unsigned long lastAdjustTime = 0;
bool pendingSave = false;

// ===== ESTADOS =====
enum State  { MENU, EDIT };
enum Screen { SCREEN_MENU, SCREEN_EDIT };

State  state         = MENU;
Screen currentScreen = SCREEN_MENU;
Screen lastScreen    = SCREEN_MENU;
bool   screenChanged = true;

// ===== MENU =====
// Menu: Treble(3), Middle(4), Bass(5) foram unificados em EQ(3)
// Itens antigos 6..13 deslocam para 4..11
// Bluetooth adicionado no final (index 12) para não afetar os índices existentes
const char m0[]  PROGMEM = "Volume";
const char m1[]  PROGMEM = "Input";
const char m2[]  PROGMEM = "Gain";
const char m3[]  PROGMEM = "EQ";          // NOVO: unifica Bass+Mid+Treble
const char m4[]  PROGMEM = "Balance";
const char m5[]  PROGMEM = "Loudness";
const char m6[]  PROGMEM = "Treb.Freq";
const char m7[]  PROGMEM = "Mid.Freq";
const char m8[]  PROGMEM = "Mid.Q";
const char m9[]  PROGMEM = "Bass.Freq";
const char m10[] PROGMEM = "Bass.Q";
const char m11[] PROGMEM = "Spectrum";
const char m12[] PROGMEM = "Bluetooth";

const char* const menuItems[] PROGMEM = {
  m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12
};

char menuBuf[16];

#define MENU_SIZE 13

int   menuIndex       = 0;
int   lastMenuIndex   = -1;
float smoothIndex     = 0;
float lastSmoothIndex = -99;

// ===== ENCODER =====
long encRaw  = 0;
long encLast = 0;

// ===== BOTÃO =====
bool          btn      = false;
unsigned long btnTimer = 0;

// ===== AUDIO — parâmetros básicos =====
int  volume  = -20;
int  input   = 1;
int  gain[3] = {10, 10, 10};
int  treble  = 0, middle = 0, bass = 0;
bool mute    = false;

// ===== AUDIO — atenuações por canal =====
int fl = 0, fr = 0, rl = 0, rr = 0, sub = 0;

// ===== AUDIO — loudness =====
int  lon   = 6;
int  lon_f = 1;

// ===== AUDIO — frequências e Q dos filtros =====
int treb_f = 0;
int mid_f  = 0;
int mid_q  = 0;
int bass_f = 2;
int bass_q = 1;
int sab_f  = 0;

// ===== BLUETOOTH =====
bool btEnabled = false; // true = pino BT_PIN em LOW e input = 3

// ===== DIRTY FLAGS =====
int  lastVolume = -999, lastInput  = -999, lastGain   = -999;
int  lastTreble = -999, lastMiddle = -999, lastBass   = -999;
int  lastFr     = -999, lastLon    = -999, lastLon_f  = -999;
int  lastTrebF  = -999, lastMidF   = -999, lastMidQ   = -999;
int  lastBassF  = -999, lastBassQ  = -999;
int  lastBtEnabled = -999;

// ===== LOUDNESS =====
bool loudnessEditFreq = false;

// ===== EQ GRÁFICO =====
// banda: 0=Bass, 1=Middle, 2=Treble
int eqBand = 0;
int lastEqBass   = -999;
int lastEqMiddle = -999;
int lastEqTreble = -999;
int lastEqBand   = -999;

// ===== SPECTRUM =====
float spec[BAR_COUNT];
float specPeak[BAR_COUNT];
int   specLast[BAR_COUNT];
int   specPeakLast[BAR_COUNT];

// ===== CORES =====
#define CLR_BLACK   0,   0,   0
#define CLR_WHITE   255, 255, 255
#define CLR_CYAN    0,   255, 255
#define CLR_BLUE    190,  0,  200
#define CLR_GREEN   0,   220,   0
#define CLR_YELLOW  255, 200,   0
#define CLR_RED     255,   0,   0
#define CLR_GRAY    60,   60,  60
#define CLR_ORANGE  255, 120,   0

// ===== SETUP =====
void setup() {
  Wire.begin();
  pinMode(9, OUTPUT);
  digitalWrite(9, HIGH);

  pinMode(ENC_SW,   INPUT_PULLUP);
  pinMode(SPEC_CLK, OUTPUT);
  pinMode(SPEC_OUT, INPUT);

  pinMode(BT_PIN, OUTPUT);
  digitalWrite(BT_PIN, HIGH); // Bluetooth desativado por padrão (5V)

  ucg.begin(UCG_FONT_MODE_TRANSPARENT);
  ucg.setRotate270();

  for (int i = 0; i < BAR_COUNT; i++) {
    spec[i] = specPeak[i] = 0;
    specLast[i] = specPeakLast[i] = -1;
  }

  splash();
  loadEEPROM();

  mute = false;
  constrainAll();
  delay(100);
  applyAudio();
}

// ===== LOOP =====
void loop() {
  readEncoder();
  readButton();

  if (pendingSave && millis() - lastAdjustTime > 3000) {
    pendingSave = false;
    saveEEPROM();
  }

  smoothIndex += (menuIndex - smoothIndex) * 0.99f;

  if (screenChanged) {
    screenChanged    = false;
    lastSmoothIndex  = -99;
    lastMenuIndex    = -1;
    for (int i = 0; i < BAR_COUNT; i++)
      specLast[i] = specPeakLast[i] = -1;
    lastEqBass = lastEqMiddle = lastEqTreble = lastEqBand = -999;
    lastBtEnabled = -999;

    for (int x = 160; x >= 0; x -= 32) {
      ucg.setColor(CLR_BLACK);
      ucg.drawBox(x, 0, 32, 128);
    }

    if (state == MENU) drawMenu();
    else               drawEdit();
  }

  if (state == MENU) {
    if (abs(smoothIndex - lastSmoothIndex) > 0.05f || menuIndex != lastMenuIndex) {
      drawMenu();
      lastSmoothIndex = smoothIndex;
      lastMenuIndex   = menuIndex;
    }
  } else {
    drawEdit();
  }
}

// ===== SPLASH =====
void splash() {
  ucg.clearScreen();
  ucg.setColor(CLR_GRAY);
  for (uint8_t i = 1; i <= 4; i++) {
    ucg.drawCircle(120, 28, i * 12, UCG_DRAW_ALL);
  }
  ucg.setFont(ucg_font_ncenB14_tr);
  ucg.setColor(CLR_CYAN);
  ucg.setPrintPos(18, 55);
  ucg.print("BUBU AUDIO");
  delay(1500);
}

// ===== ENCODER =====
void readEncoder() {
  encRaw = enc.read() / 4;
  if (encRaw != encLast) {
    int d = encRaw - encLast;
    encLast = encRaw;
    if (abs(d) > 1) d *= 2;

    if (state == MENU) {
      menuIndex = constrain(menuIndex + d, 0, MENU_SIZE - 1);
    } else {
      adjust(d);
    }
  }
}

// ===== BOTÃO =====
void readButton() {
  if (!digitalRead(ENC_SW) && !btn) {
    btn      = true;
    btnTimer = millis();
  }
  if (digitalRead(ENC_SW) && btn) {
    btn = false;
    unsigned long pressTime = millis() - btnTimer;

    if (state == EDIT && menuIndex == 3) {
      // ===== Tela EQ =====
      if (pressTime > 600) {
        // Clique longo → volta ao menu (sem mute aqui)
        state         = MENU;
        currentScreen = SCREEN_MENU;
        screenChanged = true;
        eqBand        = 0;
      } else {
        // Clique rápido → circula entre as bandas Bass→Mid→Treble→Bass
        eqBand = (eqBand + 1) % 3;
        lastEqBand = -999; // força redraw do highlight
      }

    } else if (state == EDIT && menuIndex == 5) {
      // ===== Tela Loudness =====
      if (pressTime > 600) {
        state = MENU;
        currentScreen = SCREEN_MENU;
        screenChanged = true;
        loudnessEditFreq = false;
      } else {
        loudnessEditFreq = !loudnessEditFreq;
      }

    } else if (state == EDIT && menuIndex == 12) {
      // ===== Tela Bluetooth =====
      // Qualquer clique (curto ou longo) sai da opção e volta ao menu,
      // sem acionar o mute padrão. A seleção Sim/Não é feita pelo encoder.
      state         = MENU;
      currentScreen = SCREEN_MENU;
      screenChanged = true;

    } else {
      // ===== Comportamento padrão =====
      if (pressTime > 600) {
        // Clique longo = mute (exceto EQ, Loudness e Bluetooth já tratados acima)
        mute = !mute;
        pendingSave    = true;
        lastAdjustTime = millis() - 2500;
        applyAudio();
        drawMuteIndicator();
      } else {
        // Clique rápido = alterna MENU ↔ EDIT
        state         = (state == MENU) ? EDIT : MENU;
        lastScreen    = currentScreen;
        currentScreen = (state == MENU) ? SCREEN_MENU : SCREEN_EDIT;
        screenChanged = true;
        lastVolume = lastInput  = lastGain   = -999;
        lastTreble = lastMiddle = lastBass   = -999;
        lastFr     = lastLon    = lastLon_f  = -999;
        lastTrebF  = lastMidF   = lastMidQ   = -999;
        lastBassF  = lastBassQ  = -999;
        lastEqBass = lastEqMiddle = lastEqTreble = lastEqBand = -999;
        lastBtEnabled = -999;

        // Se estamos entrando na opção Input com o Bluetooth ainda ativo,
        // desliga o pino BT (5V) e desativa a flag de Bluetooth. O input
        // permanece com o valor atual (1, 2 ou 3) e passa a ser editável
        // normalmente pelo encoder.
        if (state == EDIT && menuIndex == 1 && btEnabled) {
          btEnabled = false;
          digitalWrite(BT_PIN, HIGH);
        }
      }
    }
  }
}

// ===== MUTE =====
void drawMuteIndicator() {
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(105, 2, 55, 14);
  if (mute) {
    ucg.setFont(ucg_font_6x10_tr);
    ucg.setColor(CLR_RED);
    ucg.setPrintPos(108, 12);
    ucg.print("MUTE");
  }
}

// ===== MENU =====
void drawMenu() {
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(0, 14, 160, 114);
  ucg.setFont(ucg_font_7x13_tr);

  for (int i = 0; i < MENU_SIZE; i++) {
    int y = 30 + (int)((i - smoothIndex) * 20);
    if (y < 14 || y > 128) continue;

    if (i == menuIndex) {
      ucg.setColor(CLR_BLUE);
      ucg.drawRBox(5, y - 2, 150, 18, 3);
      ucg.setColor(CLR_WHITE);
    } else {
      ucg.setColor(i >= 5 ? CLR_ORANGE : CLR_CYAN);
    }

    strcpy_P(menuBuf, (char*)pgm_read_word(&menuItems[i]));
    ucg.setPrintPos(20, y + 12);
    ucg.print(menuBuf);
  }

  drawMuteIndicator();
}

// ===== EDIT =====
void drawEdit() {
  static const char e0[]  PROGMEM = "10.0kHz"; static const char e1[]  PROGMEM = "12.5kHz";
  static const char e2[]  PROGMEM = "15.0kHz"; static const char e3[]  PROGMEM = "17.5kHz";
  static const char e4[]  PROGMEM = "0.5kHz";  static const char e5[]  PROGMEM = "1.0kHz";
  static const char e6[]  PROGMEM = "1.5kHz";  static const char e7[]  PROGMEM = "2.5kHz";
  static const char e8[]  PROGMEM = "0.50";    static const char e9[]  PROGMEM = "0.75";
  static const char e10[] PROGMEM = "1.00";    static const char e11[] PROGMEM = "1.25";
  static const char e12[] PROGMEM = "60Hz";    static const char e13[] PROGMEM = "80Hz";
  static const char e14[] PROGMEM = "100Hz";   static const char e15[] PROGMEM = "200Hz";
  static const char e16[] PROGMEM = "1.00";    static const char e17[] PROGMEM = "1.25";
  static const char e18[] PROGMEM = "1.50";    static const char e19[] PROGMEM = "2.00";

  static const char* const trebFreqs[] PROGMEM = {e0,e1,e2,e3};
  static const char* const midFreqs[]  PROGMEM = {e4,e5,e6,e7};
  static const char* const midQs[]     PROGMEM = {e8,e9,e10,e11};
  static const char* const bassFreqs[] PROGMEM = {e12,e13,e14,e15};
  static const char* const bassQs[]    PROGMEM = {e16,e17,e18,e19};

  static const char bt0[] PROGMEM = "SIM";
  static const char bt1[] PROGMEM = "NAO";
  static const char* const btOpts[] PROGMEM = {bt0, bt1};

  switch (menuIndex) {
    case 0:  if (volume != lastVolume)      { barUI(F("VOL"),  volume, -79, 15, F("dB")); lastVolume = volume; } break;
    case 1:  if (input  != lastInput)       { simpleUI(F("IN"), input, F("")); lastInput = input; } break;
    case 2:  if (gain[input-1] != lastGain) { simpleUI(F("GAIN"), gain[input-1], F("")); lastGain = gain[input-1]; } break;
    case 3:  eqUI(); break;   // EQ gráfico
    case 4:  if (fr != lastFr)              { faderUI(); lastFr = fr; } break;
    case 5:  if (lon != lastLon || lon_f != lastLon_f) { loudnessUI(); lastLon = lon; lastLon_f = lon_f; } break;
    case 6:  if (treb_f != lastTrebF) { enumUI(F("TREB FREQ"), treb_f, 4, trebFreqs); lastTrebF = treb_f; } break;
    case 7:  if (mid_f  != lastMidF)  { enumUI(F("MID FREQ"),  mid_f,  4, midFreqs);  lastMidF  = mid_f;  } break;
    case 8:  if (mid_q  != lastMidQ)  { enumUI(F("MID Q"),     mid_q,  4, midQs);     lastMidQ  = mid_q;  } break;
    case 9:  if (bass_f != lastBassF) { enumUI(F("BASS FREQ"), bass_f, 4, bassFreqs); lastBassF = bass_f; } break;
    case 10: if (bass_q != lastBassQ) { enumUI(F("BASS Q"),    bass_q, 4, bassQs);    lastBassQ = bass_q; } break;
    case 11: spectrum(); break;
    case 12: {
      int btIdx = btEnabled ? 0 : 1; // 0 = SIM, 1 = NAO
      if (btIdx != lastBtEnabled) { enumUI(F("BLUETOOTH"), btIdx, 2, btOpts); lastBtEnabled = btIdx; }
    } break;
  }
}

// ===== AJUSTE =====
void adjust(int d) {
  switch (menuIndex) {
    case 0:  volume        += d; break;
    case 1:  input         += d; break;
    case 2:  gain[input-1] += d; break;
    case 3:
      // EQ gráfico: ajusta a banda selecionada
      if      (eqBand == 0) bass   += d;
      else if (eqBand == 1) middle += d;
      else                  treble += d;
      break;
    case 4:  fr     += d; break;
    case 5:
      if (!loudnessEditFreq) lon  -= d;
      else                   lon_f += d;
      break;
    case 6:  treb_f += d; break;
    case 7:  mid_f  += d; break;
    case 8:  mid_q  += d; break;
    case 9:  bass_f += d; break;
    case 10: bass_q += d; break;
    case 11: volume += d; break;
    case 12: setBluetooth(!btEnabled); break; // Alterna Sim/Não a cada clique do encoder
  }
  constrainAll();
  applyAudio();
  lastAdjustTime = millis();
  pendingSave    = true;
}

// ===== LIMITES =====
void constrainAll() {
  volume = constrain(volume, -79, 15);
  input  = constrain(input,   1,   3);
  for (int i = 0; i < 3; i++)
    gain[i] = constrain(gain[i], 0, 15);
  treble = constrain(treble, -15, 15);
  middle = constrain(middle, -15, 15);
  bass   = constrain(bass,   -15, 15);
  fr     = constrain(fr,    -15,  15);
  lon    = constrain(lon,     0,  15);
  lon_f  = constrain(lon_f,   0,   3);
  treb_f = constrain(treb_f,  0,   3);
  mid_f  = constrain(mid_f,   0,   3);
  mid_q  = constrain(mid_q,   0,   3);
  bass_f = constrain(bass_f,  0,   3);
  bass_q = constrain(bass_q,  0,   3);
  sab_f  = constrain(sab_f,   0,   3);
}

// ===== BLUETOOTH =====
// Liga: pino BT_PIN em LOW (0V) e força a entrada 3.
// Desliga: pino BT_PIN em HIGH (5V), sem alterar a entrada atual.
void setBluetooth(bool on) {
  btEnabled = on;
  if (btEnabled) {
    digitalWrite(BT_PIN, LOW);
    input = 3;
  } else {
    digitalWrite(BT_PIN, HIGH);
  }
}

// ===== ÁUDIO =====
void applyAudio() {
  int safeInput = constrain(input,             1,   3);
  int safeGain  = constrain(gain[safeInput-1], 0,  15);
  int safeVol   = constrain(volume,          -79,  15);
  int safeTre   = constrain(treble,          -15,  15);
  int safeMid   = constrain(middle,          -15,  15);
  int safeBas   = constrain(bass,            -15,  15);
  int safeFr    = constrain(fr,               -15,  15);

  tda.setInput(safeInput, safeGain, 0);
  // Input2 deve apontar para a mesma entrada selecionada com mesmo gain,
  // evitando que uma entrada flutuante se misture ao sinal principal.
  // O mixer está desligado (mix_en=0 no setMix_Gain_Eff), então Input2
  // não vai para a saída principal — mas precisa estar definido para não flutuar.
  tda.setInput2(safeInput, safeGain, 0);
  //tda.setInput2(0,0,0);
  tda.setAtt_loudness(lon, lon_f, 2, 0);
  tda.setSoft(!mute, 0, 0, 0, 0);
  tda.setVolume(safeVol, 0);
  // Balance L/R simétrico: fr=0 centro, fr>0 empurra para direita (L atenua), fr<0 empurra para esquerda (R atenua)
  int attL = (safeFr > 0) ? 95 - safeFr : 95;
  int attR = (safeFr < 0) ? 95 + safeFr : 95;
  tda.setAtt_LF(attL, 0);
  tda.setAtt_RF(attR, 0);
  tda.setAtt_LT(95,          0);  // L traseiro: sem atenuação
  tda.setAtt_RT(95,          0);  // R traseiro: sem atenuação
  tda.setAtt_SUB(95,         0);  // Sub: sem atenuação extra
  tda.setAtt_Mix(95,         0);  // Mixer: 0dB (mixer desligado via setMix_Gain_Eff)
  tda.setFilter_Treble(safeTre, treb_f, 0);
  tda.setFilter_Middle(safeMid, mid_q,  0);
  tda.setFilter_Bass(safeBas,   bass_q, 0);
  tda.setSub_M_B(sab_f, mid_f, bass_f, 0, 0);
  // Mixer desligado (mix_en=0, sub_en=0), gain_eff=1 → 0dB sem bypass especial
  tda.setMix_Gain_Eff(0, 0, 0, 0, 1);
  // Spectrum: q=1(narrow), analog on, reset=0 (não reseta a cada applyAudio!), mode=2
   tda.setSpektor(2, 0, 1, 0, 1, 0, 0);
}

// ===== UI: EQ GRÁFICO =====
// Layout: 3 barras verticais centralizadas
// Cada barra: largura 28px, gap 16px
// Posições X dos centros: Bass=32, Mid=80, Treble=128
// Área de barra: Y=18..108 (90px = range -15..+15 dB)
// Zero line em Y=63 (meio)
// Labels dB acima, nome da banda abaixo

#define EQ_BAR_W    28
#define EQ_BAR_H    90    // altura total da área de barra
#define EQ_Y_TOP    18    // topo da área de barra (= +15dB)
#define EQ_Y_ZERO   63    // linha de zero (meio)
#define EQ_Y_BOT   108    // fundo da área de barra (= -15dB)
#define EQ_X_BASS   24
#define EQ_X_MID    68
#define EQ_X_TRE   112

void drawEqBar(int cx, int db, bool selected, bool forceRedraw) {
  // cx = x do centro da barra
  int x = cx - EQ_BAR_W / 2;

  // Fundo da barra (área total)
  ucg.setColor(CLR_GRAY);
  ucg.drawFrame(x, EQ_Y_TOP, EQ_BAR_W, EQ_BAR_H);

  // Limpa interior
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(x + 1, EQ_Y_TOP + 1, EQ_BAR_W - 2, EQ_BAR_H - 2);

  // Linha de zero
  ucg.setColor(CLR_GRAY);
  ucg.drawHLine(x + 1, EQ_Y_ZERO, EQ_BAR_W - 2);

  // Barra colorida
  if (db > 0) {
    int barH = map(db, 0, 15, 0, EQ_Y_ZERO - EQ_Y_TOP - 1);
    int barY = EQ_Y_ZERO - barH;
    ucg.setColor(CLR_GREEN);
    ucg.drawBox(x + 2, barY, EQ_BAR_W - 4, barH);
  } else if (db < 0) {
    int barH = map(-db, 0, 15, 0, EQ_Y_BOT - EQ_Y_ZERO - 1);
    ucg.setColor(CLR_RED);
    ucg.drawBox(x + 2, EQ_Y_ZERO + 1, EQ_BAR_W - 4, barH);
  }

  // Highlight da barra selecionada: borda colorida
  if (selected) {
    ucg.setColor(CLR_CYAN);
    ucg.drawFrame(x - 1, EQ_Y_TOP - 1, EQ_BAR_W + 2, EQ_BAR_H + 2);
  }

  // Valor dB acima da barra
  ucg.setFont(ucg_font_6x10_tr);
  ucg.setColor(selected ? CLR_WHITE : CLR_GRAY);
  // Limpa área do valor
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(x, 4, EQ_BAR_W, 12);
  ucg.setColor(selected ? CLR_WHITE : CLR_GRAY);
  ucg.setPrintPos(cx - (db < 0 ? 9 : 6), 13);
  ucg.print(db);
}

void eqUI() {
  bool changed = (bass   != lastEqBass   ||
                  middle != lastEqMiddle ||
                  treble != lastEqTreble ||
                  eqBand != lastEqBand);
  if (!changed) return;

  // Título
  if (lastEqBass == -999) {
    // Primeiro desenho: apaga tela e desenha tudo fixo
    ucg.setColor(CLR_BLACK);
    ucg.drawBox(0, 0, 160, 128);

    ucg.setFont(ucg_font_6x10_tr);
    ucg.setColor(CLR_ORANGE);
    ucg.setPrintPos(135, 70);
    ucg.print("EQ");

    // Labels das bandas (fixos, embaixo)
    ucg.setColor(CLR_GRAY);
    ucg.setPrintPos(EQ_X_BASS - 8, 122);  ucg.print("BASS");
    ucg.setPrintPos(EQ_X_MID  - 7, 122);  ucg.print("MID");
    ucg.setPrintPos(EQ_X_TRE  - 8, 122);  ucg.print("TRE");

    // Hint de navegação
    ucg.setColor(CLR_GRAY);
    ucg.setPrintPos(130, 122);
    ucg.print("< >");
  }

  // Redesenha as 3 barras
  drawEqBar(EQ_X_BASS, bass,   eqBand == 0, true);
  drawEqBar(EQ_X_MID,  middle, eqBand == 1, true);
  drawEqBar(EQ_X_TRE,  treble, eqBand == 2, true);

  // Redesenha labels das bandas com cor correta (selecionada = branco)
  ucg.setFont(ucg_font_6x10_tr);
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(0, 113, 160, 12);

  ucg.setColor(eqBand == 0 ? CLR_WHITE : CLR_GRAY);
  ucg.setPrintPos(EQ_X_BASS - 8, 122); ucg.print("BASS");
  ucg.setColor(eqBand == 1 ? CLR_WHITE : CLR_GRAY);
  ucg.setPrintPos(EQ_X_MID  - 7, 122); ucg.print("MID");
  ucg.setColor(eqBand == 2 ? CLR_WHITE : CLR_GRAY);
  ucg.setPrintPos(EQ_X_TRE  - 8, 122); ucg.print("TRE");

  ucg.setColor(CLR_GRAY);
  ucg.setPrintPos(130, 122);
  ucg.print("< >");

  lastEqBass   = bass;
  lastEqMiddle = middle;
  lastEqTreble = treble;
  lastEqBand   = eqBand;
}

// ===== UI: BARRA HORIZONTAL =====
void barUI(const __FlashStringHelper* label, int val, int minv, int maxv, const __FlashStringHelper* unit) {
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(0, 0, 160, 128);
  ucg.setFont(ucg_font_7x13_tr);
  ucg.setColor(CLR_WHITE);
  ucg.setPrintPos(10, 20);  ucg.print(label);
  ucg.setPrintPos(70, 20);  ucg.print(val);
  ucg.print(unit);          ucg.print("  ");
  ucg.setColor(CLR_WHITE);
  ucg.drawFrame(10, 50, 140, 14);
  int bar = map(val, minv, maxv, 0, 138);
  ucg.setColor(CLR_GREEN);  ucg.drawBox(11, 51, bar, 12);
  ucg.setColor(CLR_BLACK);  ucg.drawBox(11 + bar, 51, 138 - bar, 12);
}

void simpleUI(const __FlashStringHelper* label, int val, const __FlashStringHelper* unit) {
  ucg.setColor(CLR_BLACK);  ucg.drawBox(0, 0, 160, 128);
  ucg.setFont(ucg_font_7x13_tr);
  ucg.setColor(CLR_CYAN);   ucg.setPrintPos(55, 45); ucg.print(label);
  ucg.setFont(ucg_font_ncenB14_tr);
  ucg.setColor(CLR_WHITE);  ucg.setPrintPos(65, 85);
  ucg.print(val); ucg.print(unit); ucg.print(F("  "));
}

void enumUI(const __FlashStringHelper* label, int val, int total, const char* const opts[]) {
  ucg.setColor(CLR_BLACK);  ucg.drawBox(0, 0, 160, 128);
  ucg.setFont(ucg_font_7x13_tr);
  ucg.setColor(CLR_ORANGE); ucg.setPrintPos(10, 20); ucg.print(label);
  for (int i = 0; i < total; i++) {
    int y = 45 + i * 20;
    if (i == val) {
      ucg.setColor(CLR_BLUE);
      ucg.drawRBox(20, y - 2, 120, 16, 3);
      ucg.setColor(CLR_WHITE);
    } else {
      ucg.setColor(CLR_GRAY);
    }
    strcpy_P(menuBuf, (char*)pgm_read_word(&opts[i]));
    ucg.setPrintPos(30, y + 10);
    ucg.print(menuBuf);
  }
}

// ===== UI: LOUDNESS =====
void loudnessUI() {
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(0, 0, 160, 128);

  ucg.setFont(ucg_font_7x13_tr);
  ucg.setColor(CLR_ORANGE);
  ucg.setPrintPos(10, 20);
  ucg.print("LOUDNESS");

  ucg.setColor(!loudnessEditFreq ? CLR_WHITE : CLR_GRAY);
  ucg.setPrintPos(10, 45);
  ucg.print("ATT:");
  ucg.setPrintPos(50, 45);
  ucg.print(-lon);
  ucg.print("dB  ");

  int bar = map(lon, 0, 15, 138, 0);
  ucg.setColor(CLR_WHITE);
  ucg.drawFrame(10, 52, 140, 10);
  ucg.setColor(CLR_CYAN);
  ucg.drawBox(11, 53, bar, 8);
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(11 + bar, 53, 138 - bar, 8);

  ucg.setColor(loudnessEditFreq ? CLR_WHITE : CLR_GRAY);
  ucg.setPrintPos(10, 80);
  ucg.print("FREQ:");
  ucg.setPrintPos(55, 80);
  const char* freqs[] = {"FLAT","400Hz","800Hz","2.4kHz"};
  ucg.print(freqs[lon_f]);
  ucg.print("   ");
}

// ===== UI: FADER =====
void faderUI() {
  ucg.setColor(CLR_BLACK);
  ucg.drawBox(0, 0, 160, 128);

  ucg.setFont(ucg_font_7x13_tr);
  ucg.setColor(CLR_ORANGE);
  ucg.setPrintPos(40, 20);
  ucg.print("BALANCE");

  // Barra horizontal de balance
  ucg.setColor(CLR_WHITE);
  ucg.drawFrame(10, 56, 140, 16);

  // Linha de centro
  ucg.setColor(CLR_GRAY);
  ucg.drawVLine(80, 57, 14);

  // Barra preenchida da posição atual
  if (fr > 0) {
    // Empurrando para direita: barra do centro para direita
    int barW = map(fr, 0, 15, 0, 68);
    ucg.setColor(CLR_GREEN);
    ucg.drawBox(81, 57, barW, 14);
  } else if (fr < 0) {
    // Empurrando para esquerda: barra do centro para esquerda
    int barW = map(-fr, 0, 15, 0, 68);
    ucg.setColor(CLR_GREEN);
    ucg.drawBox(80 - barW, 57, barW, 14);
  }

  // Labels L / R
  ucg.setFont(ucg_font_6x10_tr);
  ucg.setColor(CLR_WHITE);
  ucg.setPrintPos(18, 54); ucg.print("L");
  ucg.setPrintPos(144, 54); ucg.print("R");

  // Valor numérico
  ucg.setFont(ucg_font_7x13_tr);
  ucg.setColor(CLR_WHITE);
  ucg.setPrintPos(55, 95);
  if      (fr == 0)  { ucg.print("CENTER  "); }
  else if (fr  > 0)  { ucg.print("R+"); ucg.print(fr);  ucg.print("   "); }
  else               { ucg.print("L+"); ucg.print(-fr); ucg.print("   "); }
}

// ===== SPECTRUM =====
void spectrum() {
  digitalWrite(SPEC_CLK, LOW);
  digitalWrite(SPEC_CLK, HIGH);
  delayMicroseconds(50);

  for (int i = 0; i < BAR_COUNT; i++) {
    digitalWrite(SPEC_CLK, LOW);
    digitalWrite(SPEC_CLK, HIGH);
    delayMicroseconds(30);

    int raw = analogRead(SPEC_OUT);
    int v   = raw;
    if (v < 8) v = 0;

    spec[i] += (v - spec[i]) * 0.4f;

    if (spec[i] > specPeak[i]) {
      specPeak[i] = spec[i];
    } else {
      specPeak[i] -= 2;
      if (specPeak[i] < 0) specPeak[i] = 0;
    }

    drawBarDelta(i, (int)spec[i], (int)specPeak[i]);
  }
}

// ===== SPECTRUM: DELTA DRAW =====
void drawBarDelta(int idx, int v, int peak) {
  v    = constrain(v,    0, SPEC_H);
  peak = constrain(peak, 0, SPEC_H);

  int vOld    = specLast[idx];
  int peakOld = specPeakLast[idx];

  if (v == vOld && peak == peakOld) return;

  int x = idx * (BAR_W + BAR_GAP);

  if (peakOld != peak && peakOld > 0) {
    ucg.setColor(CLR_BLACK);
    ucg.drawBox(x, SPEC_Y1 - peakOld, BAR_W, 2);
  }

  if (v > vOld) {
    for (int y = vOld + 1; y <= v; y++) {
      if      (y < 20) ucg.setColor(CLR_GREEN);
      else if (y < 40) ucg.setColor(CLR_YELLOW);
      else             ucg.setColor(CLR_RED);
      ucg.drawBox(x, SPEC_Y1 - y, BAR_W, 1);
    }
  } else if (v < vOld) {
    ucg.setColor(CLR_BLACK);
    ucg.drawBox(x, SPEC_Y1 - vOld, BAR_W, vOld - v);
  }

  if (peak > 0) {
    ucg.setColor(CLR_WHITE);
    ucg.drawBox(x, SPEC_Y1 - peak, BAR_W, 2);
  }

  specLast[idx]     = v;
  specPeakLast[idx] = peak;
}

// ===== EEPROM =====
void loadEEPROM() {
  if (EEPROM.read(20) != 0xAB) return;

  int v = (int)EEPROM.read(0) - 79;
  int i = (int)EEPROM.read(1);
  volume = (v >= -79 && v <= 15) ? v : -20;
  input  = (i >= 1   && i <= 3)  ? i : 1;

  gain[0] = constrain((int)EEPROM.read(2), 0, 15);
  gain[1] = constrain((int)EEPROM.read(3), 0, 15);
  gain[2] = constrain((int)EEPROM.read(4), 0, 15);

  int t = (int)EEPROM.read(5) - 15;
  int m = (int)EEPROM.read(6) - 15;
  int b = (int)EEPROM.read(7) - 15;
  treble = (t >= -15 && t <= 15) ? t : 0;
  middle = (m >= -15 && m <= 15) ? m : 0;
  bass   = (b >= -15 && b <= 15) ? b : 0;

  lon    = constrain((int)EEPROM.read(8),  0, 15);
  lon_f  = constrain((int)EEPROM.read(9),  0,  3);
  treb_f = constrain((int)EEPROM.read(10), 0,  3);
  mid_f  = constrain((int)EEPROM.read(11), 0,  3);
  mid_q  = constrain((int)EEPROM.read(12), 0,  3);
  bass_f = constrain((int)EEPROM.read(13), 0,  3);
  bass_q = constrain((int)EEPROM.read(14), 0,  3);
  sab_f  = constrain((int)EEPROM.read(15), 0,  3);
}

void saveEEPROM() {
  EEPROM.update(0,  volume + 79);
  EEPROM.update(1,  input);
  EEPROM.update(2,  gain[0]);
  EEPROM.update(3,  gain[1]);
  EEPROM.update(4,  gain[2]);
  EEPROM.update(5,  treble + 15);
  EEPROM.update(6,  middle + 15);
  EEPROM.update(7,  bass   + 15);
  EEPROM.update(8,  lon);
  EEPROM.update(9,  lon_f);
  EEPROM.update(10, treb_f);
  EEPROM.update(11, mid_f);
  EEPROM.update(12, mid_q);
  EEPROM.update(13, bass_f);
  EEPROM.update(14, bass_q);
  EEPROM.update(15, sab_f);
  EEPROM.update(20, 0xAB);
}
