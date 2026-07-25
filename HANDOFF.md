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
