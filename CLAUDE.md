# Tamagotchi de mesa (furão) — Xiaozhi/Spotpear Ball V2

Firmware para transformar a "Ball V2" (ESP32-S3, tela redonda touch) num
bichinho virtual de mesa com um furão original em pixel art.

## Hardware

- ESP32-S3-DevKitC-1 (16MB flash, PSRAM octal)
- Display GC9A01 redondo 240x240, SPI
- Touch CST816, I2C dedicado (bus separado do resto)
- LED RGB WS2812 (1 unidade)
- ADC de bateria (com divisor de tensão já na placa)
- Botão BOOT (GPIO0)
- Codec de áudio ES8311 (I2C bus "A" + I2S) + amplificador (`PIN_SPEAKER_EN`)

Pinout completo e comentado em `include/pins.h` (única fonte de verdade).
Fonte original do pinout: https://github.com/RealDeco/xiaozhi-esphome (Ball_v2.yaml).

## Stack

- PlatformIO + Arduino framework (`env:ball_v2` em `platformio.ini`)
- LovyanGFX para display + touch (config em `include/LGFX_BallV2.h`)
- Adafruit NeoPixel para o LED
- ESP8266Audio (decoder MP3) + ES8311 (codec I2S) para o áudio
- WiFiManager (portal cativo) + WebServer + links2004/WebSockets + mDNS
- React (Vite + Ant Design) para o portal web, embutido gzipado no flash
- ricmoo/QRCode para o QR do portal na tela
- Preferences (NVS) para persistência de estado

## Estrutura

Código modular: cada módulo tem uma responsabilidade única e não conhece
os outros (Pet = regras; Renderer = pixels; Input = eventos; etc.).
`main.cpp` só costura tudo. Feature nova → mexe no módulo dela.

```
platformio.ini
include/
  pins.h               # mapeamento de todos os GPIOs (fonte de verdade)
  GameConfig.h         # parâmetros de jogo (decaimento, ganhos, limiares)
  Theme.h              # paletas de cores (dia/noite + menu)
  UiLayout.h           # geometria da UI (arco, menu) + hit-test compartilhado
  LGFX_BallV2.h        # config do LovyanGFX (display + touch)
  ferret_anim.h        # frames de animação do furão em RGB565 (gerado)
  sleep_music.h        # MP3 do ronco (dormir) em PROGMEM (gerado)
  sfx_eat.h, sfx_drink.h, sfx_tap.h, sfx_wake.h  # efeitos sonoros (gerados)
  web_index.h          # portal React (single-file, gzip) em PROGMEM (gerado)
src/
  main.cpp             # orquestração: junta os módulos e roda o loop
  Pet.{h,cpp}          # estado, stats, decaimento, humor, nome, NVS
  Renderer.{h,cpp}     # desenho da cena (dono do display + canvas)
  FerretActor.{h,cpp}  # comportamento do furão (passeia/come/pula/cava/dorme)
  Animator.h           # troca de quadros por tempo (genérico)
  InputController.{h,cpp}  # toque (tap/swipe) + BOOT → Ações/eventos de UI
  StatusLed.{h,cpp}    # LED RGB de humor
  Battery.{h,cpp}      # ADC + curva de bateria
  AudioPlayer.{h,cpp}  # ES8311 + I2S + task de áudio (efeitos + ronco) + volume
  Clock.{h,cpp}        # relógio NTP (timezone POSIX, 12/24h, ociosidade)
  WebPortal.{h,cpp}    # WiFiManager + WebServer + WebSocket + mDNS (portal)
assets/
  ferret-sprite-sheet.{png,json}  # spritesheet do Aseprite (fonte da animação)
  aseprite_to_frames.py  # fatia o spritesheet → include/ferret_anim.h
  mp3_to_header.py     # converte MP3 → include/*.h (PROGMEM)
  web_to_header.py     # gzipa web/dist/index.html → include/web_index.h
web/                   # portal React (Vite + antd, single-file) → web_index.h
README.md
```

## Como funciona hoje

- **4 stats** decaindo com o tempo: Fome (`hunger`), Energia (`energy`),
  Alegria (`joy`), Higiene (`hygiene`) — na classe `Pet`.
- **Cena: floresta mágica**, dia/noite: acordado = dia (céu turquesa, **sol**);
  dormindo = noite (céu roxo mágico, estrelas, **lua**, janela da cabana acesa).
  Elementos: treeline distante, **cabana de madeira** ao fundo, pinheiros,
  grama e poeira mágica/vaga-lumes que cintilam. HUD por cima: 2x2 barras de
  status e 4 botões num **arco** na base. Cores/paletas em `Theme.h`.
- **Furão animado** (spritesheet 32x32, escalado 2.5x → 80px): `FerretActor`
  escolhe a animação pela situação — **idle/idle2** (alternados aleatoriamente),
  **passeio** andando ⇄ (esquerda = frames espelhados), **comer** (Dig) ao
  alimentar, **pulo** (Jump) ao fazer carinho e aleatoriamente, **cavar buraco**
  (Dig→Disappear→Emerge, some e volta) aleatoriamente, e **dormir** (Sleep).
  `Animator` só avança os quadros; ações "uma vez" são time-boxed. Expõe
  `animName()`/`animSeq()`/`faceLeft()`/`xNorm()` pro portal espelhar.
- **Interação**: toque nos botões da base (hit-test por distância); toque no
  bichinho/cena = carinho; BOOT curto = alimentar; BOOT longo (1.5s+) =
  dormir/acordar. Gestos resolvidos no SOLTAR do toque (swipe vs tap).
- **Áudio** (`AudioPlayer`, ES8311 + I2S, task no core 0): efeitos por ação —
  comer, banho/água, carinho (tap), acordar — e ronco ao dormir. Um som por
  vez (um novo `play()` interrompe o anterior). MP3s em PROGMEM. Volume 0-100
  com curva perceptual `pow(v, 2.5)`, persistido em NVS. **Áudio validado no
  hardware** (funciona de verdade).
- **Menu de config (tela cheia)**: abre por **swipe pra baixo** (ou tap na aba
  ⌄); fecha por swipe pra cima ou botão **Fechar**. Volume −/+, botão **WiFi**
  e, quando conectado, **QR code** do portal (URL do IP) + `ferret.local`.
- **Config de WiFi não-bloqueante**: `WiFiManager` em modo `process()` (tela
  viva), tela dedicada `drawWifiConfig()` com botão **Sair** (`cancelConfig()`).
- **Portal web React + WebSocket** (`WebPortal`): `WebServer` (porta 80) serve
  o app React gzipado; `WebSocketsServer` (porta 81) **empurra o estado**
  (~10x/s andando, ~2x/s parado, + push imediato quando a animação muda) e
  recebe comandos de texto: `feed`/`pat`/`clean`/`sleep`, `name:X`, `vol:N`,
  `clock:on|off`, `fmt:12|24`, `tz:<posix>`, `idle:<seg>`. O portal espelha a
  **mesma animação/posição** do furão (spritesheet próprio + CSS steps), tem
  barras 2x2, botões de ação e drawer de configurações. Comandos rodam via
  `doAction()`/`applyCommand()` no loop principal (thread-safe). mDNS:
  `ferret.local`.
- **Modo relógio (ocioso)**: sem interação por um tempo (config.), barras e
  botões dão lugar a um **relógio** (o furão continua passeando). Hora via
  **NTP** (3 servidores com fallback), timezone POSIX, **12/24h**. **Padrão:
  desabilitado e sem fuso** — o navegador detecta o fuso (`Intl`) ao abrir o
  portal e habilita. Toque só "acorda" a tela. Persistido em NVS (com migração
  de versão).
- **LED RGB** reflete o humor (verde/amarelo/laranja/vermelho/azul/âmbar).
- Estado salvo na NVS a cada 60s (`Pet::save()`).

## Notas de hardware (aprendidas na prática)

- Painel usa `invert=true`; backlight invertido.
- Frames do furão precisam de `canvas.setSwapBytes(true)` (senão marrom vira
  verde) — o RGB565 dos `pushImage` é big-endian.
- ES8311 em modo SCLK (clock preso ao BCLK): segue a taxa do MP3 (44.1/48kHz)
  automaticamente, sem reconfigurar o codec.
- `pio` fica fora do PATH: usar `~/.platformio/penv/bin/pio`.

## Regenerar as animações do furão

O furão vem de um spritesheet do Aseprite (`assets/ferret-sprite-sheet.png` +
`.json`, frames 32x32, uma linha por animação). Para regenerar:

```bash
python3 assets/aseprite_to_frames.py   # gera include/ferret_anim.h
```

Quais animações exportar (e quais espelhar para a esquerda) fica na lista
`EXPORT` dentro do script. Cor-chave de transparência: `0xF81F` (magenta) —
não usar essa cor no desenho.

## Trocar os sons

MP3s são embutidos como headers PROGMEM (sem passo de upload de filesystem):

```bash
python3 assets/mp3_to_header.py <arquivo.mp3> sleep_music_mp3 include/sleep_music.h
python3 assets/mp3_to_header.py <arquivo.mp3> sfx_eat_mp3 include/sfx_eat.h
# idem p/ sfx_drink, sfx_tap, sfx_wake
```

## Regenerar o portal web (React)

O portal é um app React (Vite + Ant Design) em `web/`, buildado como
**single-file** e embutido gzipado no flash. Depois de mexer em `web/src`:

```bash
cd web && npm install && npm run build && cd ..
python3 assets/web_to_header.py   # gera include/web_index.h
pio run -t upload
```

O `web/dist/` não é versionado (só o `web_index.h` gerado importa).

## Build

```bash
pio run                 # compila
pio run -t upload       # compila e grava (USB-C; segure BOOT se a porta não aparecer)
pio device monitor       # log serial, 115200 baud
```

## Pendências / próximos passos conhecidos

- [ ] Sem contagem de tempo offline (decaimento só considera tempo ligado; o
      `Clock` já sincroniza NTP — dá pra salvar o timestamp e computar o gap).
- [ ] Curva de bateria em `Battery::percent()` é aproximada — vale calibrar
      com a tensão real da bateria em repouso.
- [ ] Sem modo "brincar" / mini-jogo (CST816 suporta gestos, dá pra explorar).
- [ ] Animação **Death** do sheet ainda não é usada (ex.: stat zerado por
      muito tempo).
- [ ] Acesso remoto (fora de casa): plano discutido = Tailscale num aparelho
      ajudante + Funnel (exigiria mover o WS pra porta 80 via HTTP Upgrade).

## Preferências de estilo

- **Código e comentários em inglês**; **strings de UI (tela e portal) em
  português** (o dono do projeto é BR).
- Manter `pins.h` como única fonte de verdade pro pinout — não hardcodar
  números de GPIO nos módulos.
- Assets viram headers gerados (`GENERATED ... do NOT edit`), nunca editados
  à mão; regenerar sempre pelos scripts de `assets/`.
