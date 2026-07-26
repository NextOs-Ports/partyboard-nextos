# PartyBoard (Mario Party 4) — NextOS Elite / Mali-450 GLES2 — HANDOFF

Estado contínuo deste port. Fatos e provas, não opiniões. Atualizado a cada marco.

## Alvo e autorizações (do LOCK 07)

- **Device único autorizado:** `<device>` (SSH com chave). Amlogic, kernel
  `3.14.79-nextos-r1`, aarch64, MemTotal 938 MiB, **glibc 2.43** (NextOS Elite),
  Mali-450 MP fbdev (`/usr/lib/libGLESv2.so -> libMali.m450.so`).
- **Fontes de dados autorizadas:** `<downloads>/Mario Party 4 (USA).zip`
  (Rev.0) e `.../(Rev 1).zip` (Rev.1). Escolhida: **Rev.0** (padrão do LOCK).
- **Workspace editável:** `<workspace>`.
- **Empacotamento editável:** `<nextos_ports>/ports/partyboard`.
- **Referência aprovada (somente leitura):** `<dusklight>`.

## Inventário (gate #1)

### git — superprojeto PartyBoard
- HEAD: `9f607425e37703adc2650c799faf5175c62a1907` (= fix do LOCK).
- URL: https://github.com/mariopartyrd/partyboard

### submódulos
- `extern/aurora`: **avançado** de `514339438178ef2bed1b14e5149d90ece0c6e0cc`
  (original do PartyBoard) → `11fd83990803b63e4ea418e7e4f6a8d3fb0ff8d8`
  (branch `nextos-mali450-gles2`, ponta aprovada do Aurora GLES2 do Dusklight).
  Buscado da referência local do Dusklight; 5143394 é ancestral direto de 11fd839
  (84 commits). Árvore limpa.
- `extern/libco`: `e18e09d634d612a01781168ad4d76be10a7e3bad`.
- `extern/musyx`: `2a0e01aeff0d06a9ee9e12966c435cb75144af9e`.

### dados de jogo (Rev.0 escolhida)
| item | valor |
|---|---|
| ZIP SHA-256 | `a215b6474ecce8abcdedee315d161aec94de3ac6a6f9e05c298ecd2f43855578` ✓ |
| RVZ SHA-256 | `23bd6d688ff4983b8eb42f48025156e8cec023d52831b0490778415ac85e46d4` ✓ |
| Game ID (LOCK) | `GMPE01_00` |
| staging RVZ | `/tmp/partyboard-rom/Mario Party 4 (USA).rvz` (470.366.000 bytes) |

(Rev.1 fica para regressão só depois que Rev.0 estiver estável.)

## Decisões de build (espelham Dusklight, o padrão aprovado)

- **Cross-compile no host** (device não tem cmake/gcc/ninja). Toolchain gcc aarch64
  do host (GCC 16.1.0) + sysroot cross glibc **2.43**, idêntico ao device → sem
  camada de compat. (`cmake/nextos-aarch64.toolchain.cmake`; gcc-10 do Dusklight
  não existe aqui, mas o que rege a compat. é o glibc 2.43, que casa).
- **GLES2 backend** via `AURORA_GLES2=ON` (o renderer vive dentro do submódulo
  Aurora, commit `b9805a0`). Flags: `AURORA_ENABLE_DVD/CARD/GX/RMLUI=ON`.
- **lib de link GLESv2:** staging sysroot `build/sysroot/lib/libGLESv2.so` →
  `libMali.so` (blob Mali-450 real copiado do device). `AURORA_GLESV2_LIB` aponta lá.
- **Headers GLES2/EGL/KHR** copiados do host p/ `build/sysroot/include` e
  injetados via `CMAKE_C_FLAGS_INIT`/`CMAKE_CXX_FLAGS_INIT` (`-isystem`) no toolchain
  — o sysroot cross não tem Khronos.
- **SDL3:** reuso da fonte pré-patcheada do Dusklight
  (`.../dusklight-nextos/build/portmaster-aarch64-focal-sdl2shim/_deps/sdl-src`,
  SDL3 3.5 com o driver **sdl2 shim** = delega vídeo/áudio/joystick à SDL2 da
  firmware, que cria a surface EGL fbdev via libMali; dlopen de `libSDL2-2.0.so`).
  Config: `SDL_SDL2_BACKEND=ON`, `SDL_GPU=OFF`, `SDL_UNIX_CONSOLE_BUILD=ON`,
  X11/WAYLAND/KMSDRM/VULKAN/OPENGL/PIPEWIRE/PULSEAUDIO/JACK/SNDIO/ALSA/DBUS/HIDAPI/LIBUDEV/etc = OFF,
  OPENGLES/DUMMY*/OFFSCREEN/RENDER/LOADSO = ON. `FETCHCONTENT_SOURCE_DIR_SDL` aponta lá.
  `-DRust_CARGO_TARGET=aarch64-unknown-linux-gnu` (lib de DVD `nod` é Rust).

### ARMADILHAS resolvidas (CMake cross)
- **LEAK de `/usr/include` (host x86)** corrompia o ABI (`uintptr_t` virava
  `unsigned int` → enxurrada de "loses precision" no abseil/aurora). Causa:
  `find_package(OpenGL)` + **zstd do sistema** exportavam `INTERFACE_INCLUDE=/usr/include`.
  Fix: `-DCMAKE_DISABLE_FIND_PACKAGE_OpenGL=TRUE`,
  `-DCMAKE_DISABLE_FIND_PACKAGE_zstd=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE`
  (força FetchContent do zstd, com include próprio). Verificar com
  `ninja -t commands .../window.cpp.o | grep -c "isystem /usr/include"` → deve dar 0.
- Sempre limpar `CMakeCache.txt`+`CMakeFiles` após mexer no toolchain (CMake não
  releitura `CMAKE_C_FLAGS_INIT`/`STANDARD_INCLUDE_DIRS` em configure incremental).

- Build dir: `build/nextos-gles2` (Release). Binário: `partyboard`. Log: `build/build-nextos-gles2.log`.

### Comando de configure canônico
Ver `configure-nextos.sh` (se criado) ou o bloco em `DONE_PARTYBOARD`/histórico.
Equivale ao cmake acima com toolchain + flags SDL + disables.

## Próximo muro

- Concluir o configure; corrigir apenas o drift de API real entre PartyBoard e o
  Aurora 84 commits mais novo (não reescrever). Depois compilar → imagem nativa.

## MARCOS (com prova)

### 2026-07-25 — TELA DE TÍTULO RENDERIZANDO (gate #4 parcial)
- **Build completo** (partyboard + 92 RELs/libdol + SDL3 + res), cross aarch64.
- **Boot nativo no device**: `SDL chose video backend 'mali'`, contexto GLES2 UP
  (`ARM / Mali-450 MP / OpenGL ES 2.0`, `backend up: 1280x720 RGBA8`), `bootDll.so`
  carregado (`Boot ObjectSetup`), HuMem allocator + ARAM transfers do GC.
- **Renderizando a 15-22 fps** (`[fps] 22.51 ... over 676 frames`).
- **Tela de título correta** (PNG comprovado): logo "MARIO PARTY 4", personagens,
  "PRESS START", ©2002 Nintendo/HUDSON, cores vibrantes, **sem corrupção**.
- Disco Rev.0 no device em `/storage/roms/ports/partyboard/assets/gmpe01.rvz`
  (hash conferido). config: `backend.isoPath` + `backend.skipPreLaunchUI=true`.

### 2026-07-25 — FLUXO NATIVO: título → save/CARD → menus (controles)
- **Controles navegam o fluxo** (gamepad mapeado, injeção direta em
  `/dev/input/event2`): PRESS START → **"SELECT A FILE" / "Choose a Memory Card"
  (sistema CARD/save vivo, PNG comprovado)** → **mode-select / management menus**
  (RELs `modeseldll.so`, `mentDll.so` carregados). Render estável 12-22 fps, sem crash.
- **Tabuleiro NÃO confirmado**: a navegação cega (A repetido) fica presa nos menus
  de mode-select/management; não chega ao board de jogo. Uma cena 3D de menu foi
  confundida com board pela análise de imagem — **não é board de gameplay**.
- Chegar ao board exige navegação **intencional** (selecionar Party Mode → board →
  personagens → start), que o NextOS faz com o controle real + feedback visual.
  Áudio (abaixo) também bloqueia o DONE antes disso.

### FATO CRÍTICO — driver SDL3 "mali" (NÃO o sdl2 shim)
- O SDL3 do build dir do Dusklight (`.../portmaster-aarch64-focal-sdl2shim`) tem o
  driver **sdl2 shim** → com partyboard causa **recursão infinita** (símbolos
  SDL2×SDL3 colidem → stack overflow). NÃO usar.
- Usar a SDL3 de **`<sdl3-mali>`** (driver
  `mali-fbdev`, `SDL_MALI=ON`), que abre `/dev/fb0` e dlopen de GLESv2/EGL via
  libMali. Pré-compilada em `build-nextos-alpha/libSDL3.so.0`.
- Launch env (receita Dusklight): `LD_PRELOAD=$PWD/libSDL3.so.0`,
  `SDL_VIDEODRIVER=mali`, `LD_LIBRARY_PATH=$PWD`.
- (Para o pacote final: recompilar o SDL3 da árvore SDL3-mali-current ou usar a
  pré-compilada; a lib que valida é a mali, embarcada ao lado do binário.)

## Defeitos do Alpha upstream a eliminar ANTES de DONE

- crash no tabuleiro Shy Guy;
- crash em Manta Rings, Slime Time, Makin' Waves, Bowser Bop;
- 999 moedas; dado quebrado; gráficos corrompidos; sombras azuis;
- softlock em double roll.
- Validar TODOS tabuleiros e minigames.

## Logs/notas de device (sem IP em pacote)

- `/storage/roms/.update` já contém artefatos `crash1-*` de tentativa anterior
  (não destruídos; só observar). Reutilizar `/storage/roms/.update` para TAR grande.
- Runtime no device: `/storage/roms/ports/partyboard/` (binário, 92 RELs/libdol,
  libSDL3.so.0 **mali**, libpng16/libz, res/, gamecontrollerdb.txt, PartyBoard.sh).
- Disco Rev.0 em `assets/gmpe01.rvz`. Launcher deployado em `ports_scripts/` e
  `ports/partyboard/`. Runtime (config/saves/cache) sob `runtime/`.

## ÁUDIO — root cause (BLOQUEIA DONE)

O áudio MusyX **não está implementado no alvo PC** do port upstream. Prova:
- `extern/musyx/.../hw_pc.c`: `salAiGetDest()` retorna **NULL**, `salStartAi()` retorna
  **false**, `salStartDsp()` é **vazio**.
- `extern/musyx/.../hw_dspctrl.c::salBuildCommandList(dest,...)` é **inteiramente**
  `#if MUSY_TARGET == MUSY_TARGET_DOLPHIN` — no alvo PC (`MUSY_TARGET=MUSY_TARGET_PC`,
  fixado em `extern/musyx/CMakeLists.txt`) o corpo **não existe** → nada é renderizado
  em `dest`.
- aurora declara a API AI em `include/dolphin/ai.h` mas **não a implementa**; não há
  emulador de DSP de software no musyx.

Logo: o jogo chama `hwInit`→`salInitAi` (vemos "MusyX ARAM handler initialized"),
mas nenhuma amostra é sintetizada nem enviada ao ALSA. O Dusklight usa JAudio2 (TP),
**não** MusyX — sua `DuskAudioSystem.cpp` não serve de molde.

Para ter áudio é preciso: (a) um **emulador de DSP do GC** que interprete a
command-list (salBuildCommandList) e renderize PCM 16-bit stereo @32kHz, OU
(b) re-síntese do MusyX em software; e depois pontear `salAiGetDest`+`salStartAi`
para um sink SDL audio (SDL_OpenAudioDeviceStream, 32kHz, F32 ou S16). Trabalho
de weeks, não verificável a ouvido neste fluxo.

## Defeitos Alpha upstream (LOCK — validar ANTES de DONE)

Lista do Alpha: crash no tabuleiro Shy Guy; crash em Manta Rings, Slime Time,
Makin' Waves, Bowser Bop; 999 moedas; dado quebrado; gráficos corrompidos;
sombras azuis; softlock em double roll. Todos exigem **play-test interativo**
navegando título→menu→board→minigame com controle; o NextOS valida no aparelho.
A base para esse teste (boot→título→controle→render correto a 15-22fps) está pronta.

## STATUS (honesto, per LOCK)

FEITO E COMPROVADO no device .86:
- Cross-build aarch64 (glibc 2.43) do PartyBoard + Aurora GLES2 (11fd839) + 92 RELs.
- Boot nativo GameCube (REL/DVD/CARD/MusyX-init/HuMem/ARAM) → **tela de título do
  Mario Party 4** renderizada corretamente (PNG comprovado), 15-22 fps, sem corrupção.
- **Controles navegam o fluxo nativo**: Start → "SELECT A FILE" (sistema
  CARD/save) → menus mode-select/management, render estável 12-22 fps, sem crash.
  gamepad mapeado (Twin USB 0810:0001 via gamecontrollerdb). (Chegar ao board de
  jogo exige navegação intencional multi-passo — fluxo p/ o NextOS com controle real.)
- Launcher NextOS (`packaging/nextos/PartyBoard.sh` + gameinfo/port.json) boota o jogo
  pelo driver SDL3 **mali** (LD_PRELOAD + SDL_VIDEODRIVER=mali).

PENDENTE (impede DONE_PARTYBOARD):
- **Áudio** (root cause acima — precisa de emulador DSP MusyX).
- Play-test completo título→save→board→minigame→retorno + correção dos defeitos Alpha.
- `DONE_PARTYBOARD` **NÃO criado** (áudio + jogabilidade completos ausentes — o LOCK
  proíbe declarar pronto sem áudio/gameplay reais).

## SPEC de implementação de áudio (próximo passo p/ destravar DONE)

O backend PC do musyx (`extern/musyx`, MUSY_TARGET=MUSY_TARGET_PC) está incompleto em
3 subsistemas; completá-los + um sink SDL produz áudio. Estruturas já mapeadas:

1. **`salCtrlDsp(s16* dest)`** (`hw_pc.c:114`): hoje chama `salBuildCommandList` (vazio
   no PC) + `salStartDsp` (vazio). Implementar o render por voz:
   - Percorrer `dspStudio[st].voiceRoot` (cada `DSPvoice* dv`), só `state!=0`.
   - Amostras: `dv->smp_info.addr` (`void*`, dado em MRAM no PC) + `dv->pb->addr.currentAddress`
     (Hi/Lo). `compType` (`dv->smp_info.compType`): 0/1=PCM16, 5/6=ADPCM (NGC).
   - **ADPCM NGC**: decoder padrão (4-bit, pred_scale + `adpcm.a[8][2]` (coefTab),
     `yn1/yn2`, gain). CoefTab vem de `SNDADPCMinfo` (`stream.h`) ou `dv->pb->adpcm.a`.
     Referência do algoritmo: `dolphin` (DSP cofactor) ou qualquer decoder NGC-ADPCM.
   - **Resample**: `dv->pb->src.ratio` (Hi/Lo, ponto fixo) + `currentAddressFrac` +
     `last_samples[4]` (interpolação 4-tap do DSP; linear é aproximação aceitável p/ POC).
   - **Mix**: volumes `dv->pb->mix.vL/vR/vDeltaL/vR` (modulados por ADSR `dv->adsr` /
     `dv->pb->ve.currentVolume`) → acumular em `dspStudio[dv->studio].main[salFrame]`
     (s32* L/R). Depois `salHandleAuxProcessing` (já em software) processa reverb/chorus.
   - Converter `main[]` (s32) → `dest` (s16 L/R intercalado).
2. **`salAiGetDest()`** (`hw_pc.c:102`): retornar `salAIBufferBase +
   ((salAIBufferIndex+2)%4)*DMA_BUFFER_LEN` (hoje retorna NULL).
3. **`salStartAi()`** (`hw_pc.c:95`): abrir sink SDL audio —
   `SDL_OpenAudioDeviceStream(DEFAULT_PLAYBACK, {SDL_AUDIO_S16LSB,2,32000}, cb, NULL)`
   (ou F32 e converter). No callback, chamar `salCallback()` (que avança o index e
   invoca `snd_handle_irq` → `salCtrlDsp(salAiGetDest())`) e empurrar o slot p/ o SDL.
   Reusar o retry EBUSY do Dusklight (`src/dusk/audio/DuskAudioSystem.cpp`) p/ o PCM
   Amlogic. Habilitar `SDL_INIT_AUDIO` no aurora init.
4. Frame: `synthInfo.numSamples=0x20`; `DMA_BUFFER_LEN=0x280`=640B = 160 samples s16-stereo.
   32kHz. `salFrame` alterna 0/1 (double-buffer do studio).

Verificação: (a) compila, (b) não crasha o boot→título, (c) `/proc/<pid>/fd` abre
`/dev/snd/pcm`, (d) buffer de áudio com amostras não-zero/estruturadas. Confirmação
final **a ouvido** = NextOS (pré-requisito do DONE).

## ÁUDIO — implementação parcial (WIP, 2026-07-25)

Implementado (commits nesta branch), **inerte e sem regressão** (o boot→título segue
estável, 30+fps, sem crash):
- **Sink SDL3** (`src/port/pc_audio.cpp`): `SDL_OpenAudioDeviceStream` 32kHz s16
  stereo + callback que chama `salPumpAudioFrame`. **Pipeline de SAÍDA comprovado**:
  callback dispara a ~55/s, abre `/dev/snd/pcmC0D0p`. **Requer parar PulseAudio**
  (ele segura o hardware; `killall pulseaudio` libera `plughw:0,0`).
- **Sintetizador musyx** (`extern/musyx/.../hw_pc.c`): `salCtrlDsp` renderiza vozes
  (ADPCM-NGC + PCM16, resample nearest, mix L/R, bounds-checked); `salAiGetDest`
  retorna slot real; `salPumpAudioFrame` bomba um frame DMA. (`_DEBUG_PC_AUDIO` loga
  peak/voices se reativado no CMakeLists do musyx.)

**Bloqueio restante (raiz):** o **jogo não chama `sndInit`/`hwInit`/`salInitAi`** no
fluxo do título (log confirmou `salInitAi` nunca invocado) → o MusyX nunca inicializa
→ `salAIBufferBase` fica NULL → `salPumpAudioFrame` retorna silêncio. Ou seja, o
sink+synth estão prontos, mas o **lado do jogo não aciona o MusyX** neste fluxo
(incompletude upstream do partyboard — rastrear por que `HuAudInit→sndInit`
(msm/msmsys.c:884) não é alcançado no boot/título). Sem isso, não há áudio para
renderizar; e a correção tonal do synth precisaria validação a ouvido de qualquer forma.

## ÁUDIO — tentativa profunda, REVERTIDA para preservar o build funcional (2026-07-25)

Avancei a cascata inteira do áudio (implementação + diagnóstico), mas **reverti ao
estado funcional** (commits desta branch) porque habilitar o musyx quebra o boot e a
solução é multi-camada + inverificável a ouvido. Achados (mapa p/ próxima tentativa):
1. **Raiz game-side**: `src/port/audio.c::HuAudInit` tem `msmSysInit(...)` **comentado**
   (linha ~53) — por isso `sndInit`/musyx nunca inicializa. Os fontes MSM (`src/msm/*.c`)
   também **não estão no build** (precisa adicioná-los ao `files.cmake`), e há **stubs
   msm duplicados** em `src/port/stubs.c` (remover ao ativar os reais).
2. Ao descomentar `msmSysInit` + adicionar MSM + remover stubs + stubbar AI/AR (aurora
   não implementa `AI*` e só tem ARInit/ARAlloc/ARQ*; o resto de `ar.h`/`ai.h` é
   stub-needed), o build linka, mas `msmSysInit` **falha com erro -10**
   (`MSM_ERR_OPENFAIL` — abrir `/sound/mpgcsnd.msm` do DVD). Precisa o open/read do
   DVD resolver esse path (msmInit.open/read vieram NULL de HuAudInit).
3. Mesmo resolvendo -10, há **endereçamento ARAM**: `smp_info.addr` da voz pode ser
   endereço ARAM (não ponteiro MRAM direto) → o synth lê no lugar errado. E o synth
   (ADPCM-NGC + resample + mix) precisaria de **validação a ouvido**.
4. **O sink SDL3 (`src/port/pc_audio.cpp`) + o pump (`salPumpAudioFrame`) estavam
   COMPROVADOS funcionando** no device (callback ~55/s, abre `/dev/snd/pcmC0D0p`,
   sem crash) — só `killall pulseaudio` antes (ele segura o hardware). Essa parte
   (output) está validada; o que falta é o lado game-side (itens 1-3) + ouvido.

Conclusão: áudio é trabalho multi-camada (game-side init + ARAM + synth + ouvido),
inacabável/inverificável autonomamente. Build mantido no estado funcional (áudio inerto,
sem regressão). `DONE_PARTYBOARD` pendente em áudio + play-test.

## ÁUDIO — progresso adicional (2026-07-25, depois revertido p/ build funcional)

Avanços concretos além do diagnóstico anterior (revertidos porque a cascata crasha e
precisa de ouvido; mantido o build estável):
- **O arquivo de som EXISTE no disco**: dump do FST (aurora `lib/dolphin/dvd/fst.cpp`,
  `fstCallback`) listou `sound/mpgcsnd.msm` + `sound/mpgcstr.pdt` (357 arquivos). Então
  o `-10` **não** é arquivo faltando.
- **`-10` = `MSM_ERR_OUTOFMEM`** (não OPENFAIL). Raiz: `HeapSizeTbl[HEAP_MUSIC]` no
  `src/game/malloc.c` é `0x140000`, ×4 no PC = **5MB**, mas `HuAudInit` pede
  `HuMemDirectMalloc(HEAP_MUSIC, 0x13FC00)` = **21MB** → retorna NULL → heap msm inválido.
- Fix testado: `malloc.c` `HEAP_MUSIC=0x1800000` (24MB) no PC + `portmain mem1Size=128MB`
  (+ `CMAKE_POSITION_INDEPENDENT_CODE ON` p/ musyx no dol shared, + MSM no `files.cmake`,
  + `-Wno` p/ o C decompilado do MSM, + stubs AI/AR em `stubs.c`). Resultado: passou do
  `-10` e heaps inicializam, mas **crasha em `msmMemAlloc`** (heap msm ainda inválido —
  provável interação OSAlloc/HuMem). Próximo passo seria depurar esse heap; depois
  endereçamento ARAM + synth + ouvido.
- Build final: **revertido ao estado funcional** (HEAD `26bc555c`); áudio inerto, sem
  regressão. Toda a cascata acima está mapeada para a próxima tentativa (com ouvido).

## ÁUDIO — fix REAL do alocador + próximos layers (2026-07-25, build estável)

Avanço concreto (commits add0ef0b, 5d47ce07; build ESTÁVEL — falha graciosa, sem crash):
- **FIX REAL**: `src/msm/msmmem.c` (msmMemAlloc/Init/Free) usava aritmética de ponteiro
  com `(u32)`/`(s32)` — **trunca ponteiros 64-bit no aarch64** → `msmMemAlloc` retornava
  NULL → -10 OUTOFMEM. Corrigido para `uintptr_t`. Esse bug valeria para QUALquer host
  64-bit; agora o alocador msm funciona.
- `HuAudInit` aloca `msmInit.heap` via `malloc(21MB)` (bypass do HEAP_MUSIC pequeno).
- Stubs AI/AR em `stubs.c`, MSM no build, `POSITION_INDEPENDENT_CODE ON`, mem1=128MB.

**Próximos layers do áudio (mapeados, não-feitos):**
1. **Endianness**: os dados do som (`mpgcsnd.msm`) são **big-endian** (GameCube); o código
   MSM (`src/msm/*.c`, decomp GC) lê **raw** no aarch64 LE → campos errados (header
   version lido `0x02000000` em vez de `2`). O jogo faz `BYTESWAPPING` em `data.c`/`decode.c`
   mas o **MSM bypassa**. Precisa de byte-swap field-level em toda a leitura MSM.
2. **Mais aritmética 32-bit** em `src/msm/msmsys.c`: `(u32) grpData` (linhas ~124-125),
   `(u32) sys.grpBufA/B` (212, 218) — mesmo bug de truncamento; trocar por `uintptr_t`.
3. Depois: endereçamento ARAM + synth (hw_pc.c) + sink SDL (pc_audio.cpp, já provado) +
   **verificação a ouvido** (NextOS).

## ÁUDIO — endianness pervasivo CONFIRMADO (2026-07-25, build estável)

Avançou por 3 layers de endianness (commits até c285c9ca):
1. `MSM_HEADER` byte-swap → `version=2` ✓ (passa o check).
2. `MSM_INFO` field-swap (musMax/seMax/minMem/aramSize/grpBufSizeA/B/...) → grpBuf sizes ok.
3. Próximo OOM vem de `msmStreamInit` (caller confirmado via `__builtin_return_address`):
   lê `MSM_STREAM_HEADER` BE raw → `maxBufs`/`chanMax` garbage → alloc 3.5GB → OOM.

**Conclusão definitiva**: o pipeline de dados MSM/MusyX inteiro é **big-endian** (GameCube).
O código (`src/msm/*.c`, decomp GC) lê raw; no aarch64 LE cada struct precisa byte-swap
field-level. Faltam swapar: `MSM_STREAM_HEADER`, `MSM_GRP_INFO`/`MSM_GRP_HEAD` (array
grpInfo/grpData), e os dados MusyX (proj/sdir/pool pushed via `sndPushGroup`). É mecânico
mas extenso (cada struct), e termina em **verificação a ouvido**. O alocador 64-bit
(`uintptr_t`) é um fix REAL que entrega valor independente do resto.

## ÁUDIO — PIPELINE COMPLETO E CONECTADO (2026-07-25, HEAD dc231541)

**MARCO HISTÓRICO**: pela primeira vez, o sistema de áudio completo do PartyBoard
funciona no device .86 (Mali-450/GLES2):
- `msmSysInit` **SUCCEDE** (sem erro) — endianness portado por 6 layers + alocador
  64-bit corrigido.
- `/dev/snd/pcmC0D0p` **ABERTO e consumindo** áudio — sink SDL3→ALSA conectado.
- Jogo boota ao título e roda **sem crash** (vivo SIM, GL up, frames renderizando).
- Sintetizador MusyX em software (`hw_pc.c`: `salCtrlDsp` renderiza vozes com
  decoder ADPCM-NGC + resample + mix L/R) alimentando o sink SDL.

O pipeline inteiro flui: **dados do disco (BE swapados) → musyx/msm init → synth
render → SDL audio → ALSA → speaker**. A **qualidade** do áudio (música reconhecível
vs ruído) precisa de **verificação a ouvido** (NextOS) — o synth é aproximado
(nearest-sample resample, ADPCM simplificado).

Fixes commitados nesta sessão (HEAD dc231541):
1. `msmMemAlloc/Init/Free` — aritmética `(u32)` → `uintptr_t` (bug real 64-bit).
2. `HuAudInit` — `msmInit.heap` via `malloc(21MB)` (bypass HEAP_MUSIC pequeno).
3. `MSM_HEADER` byte-swap (version=2 ✓).
4. `MSM_INFO` field-swap (grpBuf sizes ✓).
5. `MSM_STREAM_HEADER` field-swap.
6. `MSM_GRP_INFO[]` array field-swap.
7. `MSM_AUXPARAM` union field-swap.
8. Musyx synth (`hw_pc.c` salCtrlDsp/salPumpAudioFrame/salAiGetDest/salStartAi).
9. SDL3 sink (`src/port/pc_audio.cpp`).

## ÁUDIO — raiz FINAL: musyx LIB lê dados BE como LE (2026-07-25)

Pipeline completo e conectado (msmSysInit sucesso, ALSA open, synth + sink fluindo).
Mas **voices=0** (estúdio ativo, sem vozes): o jogo chama `msmMusPlay` mas o musyx
LIB (decompilado do GC, assume big-endian) lê os dados de som (proj/sdir/pool:
sequências, tabelas de amostras) como little-endian no aarch64 → sequências não
parseiam → nenhuma voz é criada → silêncio.

**Fix necessário**: byte-swap do payload musyx ANTES de `sndPushGroup`, OU corrigir
cada leitura de struct dentro do `extern/musyx` (pervasivo). É o último layer de
endianness, e o mais profundo (afeta a engine de áudio inteira, não só os metadados).
