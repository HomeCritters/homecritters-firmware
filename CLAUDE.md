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
  ferret_anim.h        # frames do furão em RGB565, 80px, cena (gerado)
  ferret_game.h        # sprite do furão 40px p/ o mini-game (gerado)
  sounds/              # TODOS os áudios (MP3/WAV→PROGMEM, gerados): sleep_music
                       # + sfx_eat/drink/tap/wake/jump/boost/crumble/record/
                       #   death/throw/camera/click/buzzer.h
                       # + simon_green/red/yellow/blue.h (tons WAV gerados)
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
  DoodleGame.{h,cpp}   # mini-game estilo doodle jump (física; Renderer desenha)
  BallGame.{h,cpp}     # mini-game "Bolinha" (fetch: arremesso + furão busca)
  SimonGame.{h,cpp}    # mini-game "Genius" (sequência de cores: arcos + LED + tons)
  DebugConsole.{h,cpp} # console serial de debug (comandos de módulo; navegação
                       # vai pro main via callback)
  WebPortal.{h,cpp}    # WiFiManager + WebServer + WebSocket + mDNS (portal)
tools/                 # debug: hwshot.py (print da tela→PNG) + console.py (serial)
assets/
  ferret-sprite-sheet.{png,json}  # spritesheet do Aseprite (fonte da animação)
  aseprite_to_frames.py  # fatia o spritesheet → include/ferret_anim.h
  mp3_to_header.py     # converte MP3 → include/*.h (PROGMEM)
  web_to_header.py     # gzipa web/dist/index.html → include/web_index.h
web/                   # portal React (Vite + antd, single-file) → web_index.h
README.md
```

A integração do Home Assistant vive num **repo separado**
(`HomeCritters/homecritters-ha-plugin`, público p/ HACS) — só o firmware/
portal/tools ficam aqui.

## Como funciona hoje

- **4 stats** decaindo com o tempo: Fome (`hunger`), Energia (`energy`),
  Alegria (`joy`), Higiene (`hygiene`) — na classe `Pet`.
- **Cena: floresta mágica** com tema pela **hora real** (NTP): 06-16 = **dia**
  (céu turquesa, sol); 16-18 = **tarde** (pôr do sol dourado); 18-06 = **noite**
  (céu roxo mágico, estrelas, lua, janela da cabana acesa). Sem relógio
  sincronizado, cai no fallback pelo estado de sono (acordado=dia, dormindo=noite).
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
  `clock:on|off`, `fmt:12|24`, `date:dmy|mdy`, `tz:<posix>`, `idle:<seg>`. O portal espelha a
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
- **Game mode**: pull da **direita** abre o **menu de jogos** (tiles quadrados);
  pull da **esquerda** = "voltar" (menu de config, menu de jogos e Bolinha —
  todas as abas puxam pro centro). Tela dos jogos sempre no hardware; ambos
  também jogáveis **pelo celular** (portal vira controle via WS). O `main` tem a
  máquina de telas (`SCREEN_PET`/`GAMES`/`DOODLE`/`BALL`); durante os jogos o
  pet continua vivo em background (decaimento, som, portal).
  - **Jump!** (`DoodleGame`): furão (sprite 40px, `ferret_game.h`) pula entre
    plataformas seguindo o dedo. Tipos: normal, **mola** (boost), **móvel**
    (azul) e **quebradiça** (bege, some após 1 pulo). Dificuldade sobe com a
    altura; nuvens em parallax; **recorde na NVS** ("NOVO RECORDE!" + jingle).
    SFX por tipo de quicada; LED pisca vermelho no game over.
  - **Bolinha** (`BallGame`): fetch — swipe pra cima arremessa a bola de tênis
    (física: gravidade, quiques, atrito); o furão passeia, persegue e pega a
    bola **no chão** (nunca no ar), comemora e ela volta. Cena da floresta de
    fundo; furão 2x (80px) com walk/idle.
  - **Genius** (`SimonGame`): 4 arcos coloridos em quadrantes diagonais na
    borda da tela redonda; o aparelho toca a sequência (arco aceso + **LED RGB
    na cor** + **um tom por cor** — WAVs gerados com as frequências do Simon
    original) e o jogador repete tocando os arcos ou os pads do portal. Centro:
    score, Leon, dica e botão ✕ de sair. Erro/timeout = buzzer + LED de morte;
    recorde na NVS (`game/shs`). O `AudioPlayer` decodifica **WAV e MP3**
    (auto-detect pelo header RIFF).
- **LED RGB** reflete o humor (verde/amarelo/laranja/vermelho/azul/âmbar).
- Estado salvo na NVS a cada 60s (`Pet::save()`).
- **Home Assistant** (repo à parte `homecritters-ha-plugin`, domínio
  `homecritters`, instala via HACS): integração Python que fala com o **WS do
  device** (porta 81, mesmo protocolo do portal). Descoberta por zeroconf
  (`_critter._tcp` no mDNS + `GET /info` com name/mac/fw). Entidades: sensores
  (stats/humor/bateria/tela), botões (alimentar/carinho/banho), switches
  (dormir, relógio), sliders (LED, tela) e **media_player** (TTS/anúncios;
  Music Assistant via provider "Home Assistant MediaPlayers", codec MP3).
  Nomes das entidades em EN + tradução PT.
- **Media streaming**: `AudioPlayer::playStream(url)` toca stream/arquivo MP3
  **http://** (sem https) — fonte HTTP → ring buffer 64KB em PSRAM → decoder,
  tudo no task de áudio (core 0). Comandos WS `media:play:<url>`/`media:stop`;
  campo `media` no estado. SFX do pet são **suprimidos** enquanto toca música
  (um decoder só). **Pegadinha**: o IDLE0 é removido do task watchdog no boot —
  a fonte HTTP da ESP8266Audio espera dados com `yield()` que nunca deixa o
  idle rodar, e o WDT estourava.

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

**REGRA: toda feature nova merece um efeito sonoro.** Procure um SFX que
combine no **MyInstants** e proponha junto com a feature (o dono adora sons em
tudo). O site bloqueia fetchers — buscar com `curl -A "<UA de navegador>"` em
`https://www.myinstants.com/en/search/?name=<termo>`; o caminho do MP3 sai do
`onclick play('/media/sounds/xxx.mp3')` e baixa direto de
`https://www.myinstants.com/media/sounds/xxx.mp3`.

MP3s são embutidos como headers PROGMEM (sem passo de upload de filesystem).
**Todos vão em `include/sounds/`** e são incluídos no `AudioPlayer.cpp` como
`#include "sounds/xxx.h"`:

```bash
python3 assets/mp3_to_header.py <arquivo.mp3> sfx_eat_mp3 include/sounds/sfx_eat.h
# idem p/ sfx_drink, sfx_tap, sfx_wake, sfx_jump, sfx_camera, sleep_music, ...
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

## Debug tools (tirar "print" da tela + navegar por serial)

**REGRA: após QUALQUER mudança visual ou feature nova, SEMPRE valide com a
ferramenta de screenshot antes de reportar ao dono** — `tools/hwshot.py` para
telas navegadas (menus/jogos) ou `curl http://ferret.local/shot.bmp` para
estado vivo (relógio/tema). Nunca assuma que a UI ficou certa sem ver o print.

Para validar UI **sem olhar o hardware**: o firmware tem um **console serial**
(`dispatchSerialCmd` em `main.cpp`, um comando por linha) e o comando `shot`
faz o `Renderer` despejar o buffer do canvas pela serial. Scripts no host
(usam o python do PlatformIO, que já tem `pyserial` + `Pillow`) remontam num
**PNG** — que dá pra abrir e inspecionar.

```bash
PY=~/.platformio/penv/bin/python
$PY tools/hwshot.py -o shot.png                         # print da tela atual
$PY tools/hwshot.py --cmd games -o games.png            # navega e depois captura
$PY tools/hwshot.py --cmd "menu:luz" -o luz.png
$PY tools/console.py "stats:80,20,50,10" pet            # só manda comandos
```

- Comandos: `shot`, `pet`, `games`, `doodle`, `ball`,
  `menu[:main|audio|luz|qr]`, `feed`/`pat`/`clean`/`sleep`, `vol:N`, `led:N`,
  `scr:N`, `stats:H,E,J,Hy`, `help`.
- Os scripts abrem a porta com DTR/RTS baixos (**não resetam** a placa) e
  auto-detectam `/dev/cu.usbmodem*`.
- Detalhes de formato do dump (header `@@SHOT w h` + RGB565 **big-endian**,
  pois o canvas usa `setSwapBytes`): `tools/README.md`.
- **Também tem screenshot no portal web**: `GET /shot.bmp` (servido pelo task
  HTTP no core 0) devolve a tela atual em BMP; o portal tem um botão **📸**. O
  render loop copia o canvas pra um buffer estável (`takeWebSnapshot`) antes de
  servir. Todo screenshot (serial ou web) toca o som de câmera (`playCamera`).
- **IMPORTANTE — reset da serial:** abrir a porta serial **reinicia** a placa
  (auto-reset do S3). Então print por **serial** mostra a tela recém-bootada e
  NÃO captura estado vivo (relógio NTP, tema por horário — perdidos no reboot).
  Pra ver o estado real, use a **web** (`curl http://ferret.local/shot.bmp` ou o
  botão 📸). Serial serve pra checar UI navegada (menus/jogos).
- **A captura acontece pós-render** (flag `g_shotPending` → `maybeShot()`); ler
  o buffer no topo do loop dava tela preta. Usar `_canvas.getBuffer()`, não
  `readRect` (retorna zeros).

## Pendências / próximos passos conhecidos

- [ ] Sem contagem de tempo offline (decaimento só considera tempo ligado; o
      `Clock` já sincroniza NTP — dá pra salvar o timestamp e computar o gap).
- [ ] Curva de bateria em `Battery::percent()` é aproximada — vale calibrar
      com a tensão real da bateria em repouso.
- [ ] Animação **Death** do sheet ainda não é usada (ex.: stat zerado por
      muito tempo).
- [~] **Home Assistant**: fases 1 (entidades) e 2 (media player) prontas via a
      integração custom. **Fase 3 pendente: voz/Assist** — habilitar o ADC do
      ES8311 (mic) + I2S RX 16k, push-to-talk, stream do áudio pelo WS pra
      integração rodar o pipeline do Assist (STT→IA→TTS; resposta toca pelo
      media player). Wake word on-device (ESP-SR) é etapa futura.
- [ ] Outras ideias: OTA, moedas/lojinha, clima real na cena.
- [ ] Acesso remoto (fora de casa): plano discutido = Tailscale num aparelho
      ajudante + Funnel (exigiria mover o WS pra porta 80 via HTTP Upgrade).

## Preferências de estilo

- **Código e comentários em inglês**; **strings de UI (tela e portal) em
  português** (o dono do projeto é BR).
- Manter `pins.h` como única fonte de verdade pro pinout — não hardcodar
  números de GPIO nos módulos.
- Assets viram headers gerados (`GENERATED ... do NOT edit`), nunca editados
  à mão; regenerar sempre pelos scripts de `assets/`.
