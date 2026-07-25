# PartyBoard (Mario Party 4) — NextOS Elite / Mali-450 GLES2 — HANDOFF

Estado contínuo deste port. Fatos e provas, não opiniões. Atualizado a cada marco.

## Alvo e autorizações (do LOCK 07)

- **Device único autorizado:** `192.168.31.86` (SSH com chave). Amlogic, kernel
  `3.14.79-nextos-r1`, aarch64, MemTotal 938 MiB, **glibc 2.43** (NextOS Elite),
  Mali-450 MP fbdev (`/usr/lib/libGLESv2.so -> libMali.m450.so`).
- **Fontes de dados autorizadas:** `/home/felipe/Downloads/Mario Party 4 (USA).zip`
  (Rev.0) e `.../(Rev 1).zip` (Rev.1). Escolhida: **Rev.0** (padrão do LOCK).
- **Workspace editável:** `/home/felipe/ports_compile/partyboard-nextos`.
- **Empacotamento editável:** `/home/felipe/nextos_ports_android/ports/partyboard`.
- **Referência aprovada (somente leitura):** `/home/felipe/ports_compile/dusklight-nextos`.

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
- Usar a SDL3 de **`/home/felipe/ports_compile/SDL3-mali-current`** (driver
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
