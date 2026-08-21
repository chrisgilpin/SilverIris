# PR-05a: host gcc + PORT shims. No window.
ROOT := ..
DECOMP := $(ROOT)/third_party/goldeneye_src
BUILD := $(ROOT)/build/native
PORT := $(ROOT)/src/port

CC ?= gcc
CFLAGS := -std=gnu89 -fms-extensions -fno-builtin -ffp-contract=off \
	-fno-strict-aliasing -fno-common \
	-Wno-incompatible-pointer-types -Wno-int-conversion \
	-Wno-implicit-int -Wno-return-type -Wno-unused-variable \
	-Wno-unused-but-set-variable -Wno-parentheses -Wno-switch \
	-Wno-missing-braces -Wno-overflow -Wno-pointer-sign \
	-Werror=implicit-function-declaration \
	-I$(PORT) -I$(PORT)/compat -I$(ROOT)/build/overlay -I$(DECOMP)/src -I$(DECOMP)/src/game \
	-idirafter $(DECOMP)/include -idirafter $(DECOMP) -idirafter $(DECOMP)/include/PR \
	-include $(PORT)/compat/ido.h \
	-DVERSION_US -DLANG_US -DREFRESH_NTSC -DBUGFIX_R0 -D_LANGUAGE_C -DPORT

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
ifneq ($(SDL_LIBS),)
SDL_DEF := -DSILVERIRIS_HAS_SDL
endif

include $(ROOT)/cmake/sources.mk

GE_SIM_OBJS := $(addprefix $(BUILD)/,$(GE_SIM_C:.c=.o))

PORT_OS_OBJS := \
	$(BUILD)/src/port/os/count.o \
	$(BUILD)/src/port/os/mesg.o \
	$(BUILD)/src/port/os/ultra_stubs.o \
	$(BUILD)/src/port/vi/wait_override.o \
	$(BUILD)/src/port/cont/playback.o \
	$(BUILD)/src/port/gfx/gbi_trace.o \
	$(BUILD)/src/port/gfx/gbi_interp.o \
	$(BUILD)/src/port/gfx/sw_raster.o $(BUILD)/src/port/gfx/tmem.o $(BUILD)/src/port/gfx/tex_bank.o $(IMAGE_NAMES_O) \
	$(BUILD)/src/port/fs/sha256.o \
	$(BUILD)/src/port/audio/mixer.o \
	$(BUILD)/src/port/audio/ai.o

PORT_STAGE_OBJS := \
	$(BUILD)/src/overrides/lv_clock.o \
	$(BUILD)/src/port/vi/sim_tick.o \
	$(BUILD)/src/port/fs/inflate1172.o \
	$(BUILD)/tools/extractor/puff.o \
	$(BUILD)/src/port/fs/stage.o \
	$(BUILD)/src/port/fs/prop.o \
	$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o \
	$(BUILD)/src/port/player/gun.o \
	$(BUILD)/src/port/chr/patrol.o \
	$(BUILD)/src/port/mp/score.o \
	$(BUILD)/src/port/det/checksum.o

PORT_PACK_OBJS := \
	$(BUILD)/src/port/fs/filelist.o \
	$(BUILD)/src/port/fs/c0pack.o \
	$(BUILD)/src/port/fs/pack_dma.o

FILELIST_U := $(abspath $(DECOMP)/scripts/filelist.u.csv)
BRINGUP_CFLAGS := $(CFLAGS) -DPORT_BRINGUP_ROM_DMA \
	-DPORT_FILELIST_DEFAULT=\"$(FILELIST_U)\"

.PHONY: all overlay ge_sim port_stub rng-test tick-test joy-playback rom-dma \
	g0-test g1-test pack-dma port-api audio-test stage-test player-test gun-test chr-test score-test split-test view-seat-test tape-test replay-synth match-config lockstep-test wasm silveriris silveriris_bringup \
	product-no-rom-dma clean trial

all: port_stub silveriris

IMAGE_NAMES_C := $(ROOT)/build/overlay/image_names.c
IMAGE_NAMES_O := $(BUILD)/overlay/image_names.o

$(IMAGE_NAMES_C): $(DECOMP)/assets/images.def $(ROOT)/tools/pack/gen_image_names.py
	python3 $(ROOT)/tools/pack/gen_image_names.py $< $@

$(IMAGE_NAMES_O): $(IMAGE_NAMES_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

overlay: $(IMAGE_NAMES_C)
	python3 $(ROOT)/native/gen_overlay.py

$(BUILD)/%.o: $(DECOMP)/%.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/game/frametiming.o: $(DECOMP)/src/game/frametiming.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DwaitForNextFrame=waitForNextFrame_hw -c $< -o $@

$(BUILD)/src/port/os/%.o: $(PORT)/os/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/vi/%.o: $(PORT)/vi/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/cont/%.o: $(PORT)/cont/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/gfx/%.o: $(PORT)/gfx/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/fs/sha256.o: $(PORT)/fs/sha256.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -c $< -o $@

$(BUILD)/src/port/fs/c0pack.o: $(PORT)/fs/c0pack.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -c $< -o $@

$(BUILD)/src/port/fs/filelist.o: $(PORT)/fs/filelist.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -c $< -o $@

$(BUILD)/src/port/fs/pack_dma.o: $(PORT)/fs/pack_dma.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/audio/%.o: $(PORT)/audio/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/overrides/lv_clock.o: $(ROOT)/src/overrides/lv_clock.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/vi/sim_tick.o: $(PORT)/vi/sim_tick.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/fs/stage.o: $(PORT)/fs/stage.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/fs/prop.o: $(PORT)/fs/prop.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/fs/inflate1172.o: $(PORT)/fs/inflate1172.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT)/fs -I$(DECOMP)/tools/extractor -c $< -o $@

$(BUILD)/tools/extractor/puff.o: $(DECOMP)/tools/extractor/puff.c
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -O2 -c $< -o $@

$(BUILD)/src/port/player/move.o: $(PORT)/player/move.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/player/stan_walk.o: $(PORT)/player/stan_walk.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/player/gun.o: $(PORT)/player/gun.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/chr/patrol.o: $(PORT)/chr/patrol.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/mp/score.o: $(PORT)/mp/score.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/det/checksum.o: $(PORT)/det/checksum.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/det/tape.o: $(PORT)/det/tape.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(ROOT)/src -c $< -o $@

$(BUILD)/src/port/net/lockstep.o: $(PORT)/net/lockstep.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -I$(PORT) -I$(PORT)/net -I$(ROOT)/src -c $< -o $@

$(BUILD)/native/main.o: $(ROOT)/native/main.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DPORT_FILELIST_DEFAULT=\"$(FILELIST_U)\" \
		$(SDL_DEF) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/src/port/os/port_stub.o: $(PORT)/os/port_stub.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/rng/random.o: $(PORT)/rng/random.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/fs/sha1.o: $(PORT)/fs/sha1.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/port/fs/rom_dma.o: $(PORT)/fs/rom_dma.c
	@mkdir -p $(dir $@)
	$(CC) $(BRINGUP_CFLAGS) -c $< -o $@

$(BUILD)/native/main_bringup.o: $(ROOT)/native/main.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(BRINGUP_CFLAGS) $(SDL_DEF) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/libge_sim.a: $(GE_SIM_OBJS) $(BUILD)/src/port/rng/random.o
	ar rcs $@ $^

port_stub: overlay $(BUILD)/libge_sim.a $(BUILD)/src/port/os/port_stub.o
	$(CC) -o $(BUILD)/port_stub $(BUILD)/src/port/os/port_stub.o \
		$(BUILD)/libge_sim.a -lm -Wl,--unresolved-symbols=ignore-all
	@echo "linked $(BUILD)/port_stub"

$(BUILD)/test_rng: $(ROOT)/tools/det/test_rng.c $(PORT)/rng/random.c $(PORT)/rng/random.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -o $@ $(ROOT)/tools/det/test_rng.c $(PORT)/rng/random.c

rng-test: $(BUILD)/test_rng
	$(BUILD)/test_rng $(ROOT)/testdata/rng

$(BUILD)/silveriris: $(BUILD)/native/main.o $(PORT_OS_OBJS) $(PORT_STAGE_OBJS) $(PORT_PACK_OBJS) $(BUILD)/libge_sim.a
	$(CC) -o $@ $(BUILD)/native/main.o $(PORT_OS_OBJS) $(PORT_STAGE_OBJS) $(PORT_PACK_OBJS) \
		$(BUILD)/libge_sim.a -lm $(SDL_LIBS)

silveriris: overlay $(BUILD)/silveriris

tick-test: silveriris
	$(BUILD)/silveriris --headless --ticks=8

# Bringup is ROM DMA only. pack_dma.o also defines osPiStartDma/osPiRawStartDma
# and must not be linked here (product silveriris is the pack-backed binary).
# stage.o talks to the pack; omit it — bringup main never loads a stage.
BRINGUP_STAGE_OBJS := $(filter-out $(BUILD)/src/port/fs/stage.o,$(PORT_STAGE_OBJS))

$(BUILD)/silveriris_bringup: $(BUILD)/native/main_bringup.o $(PORT_OS_OBJS) $(BRINGUP_STAGE_OBJS) \
		$(BUILD)/src/port/fs/rom_dma.o $(BUILD)/src/port/fs/sha1.o \
		$(BUILD)/src/port/fs/filelist.o $(BUILD)/libge_sim.a
	$(CC) -o $@ $(BUILD)/native/main_bringup.o $(PORT_OS_OBJS) $(BRINGUP_STAGE_OBJS) \
		$(BUILD)/src/port/fs/rom_dma.o $(BUILD)/src/port/fs/sha1.o \
		$(BUILD)/src/port/fs/filelist.o \
		$(BUILD)/libge_sim.a -lm $(SDL_LIBS)

silveriris_bringup: overlay $(BUILD)/silveriris_bringup

product-no-rom-dma: silveriris silveriris_bringup
	@if nm -P $(BUILD)/silveriris | awk '$$1=="port_rom_open"{found=1} END{exit found}'; then \
		echo "product silveriris has no port_rom_open"; \
	else \
		echo "FAIL: product silveriris contains K18 ROM loader"; exit 1; \
	fi
	@nm -P $(BUILD)/silveriris_bringup | awk '$$1=="port_rom_open"{found=1} END{exit !found}' \
		|| { echo "FAIL: silveriris_bringup missing port_rom_open"; exit 1; }
	$(BUILD)/silveriris --rom /tmp/ge007.u.z64 --headless; test $$? -eq 2
	$(BUILD)/silveriris_bringup --headless; test $$? -eq 2
	$(BUILD)/silveriris --pack $(ROOT)/testdata/pack/synthetic.c0pack --headless --ticks=1

$(BUILD)/test_rom_dma: $(ROOT)/tools/det/test_rom_dma.c \
		$(PORT)/fs/rom_dma.c $(PORT)/fs/filelist.c $(PORT)/fs/sha1.c \
		$(BUILD)/src/port/os/mesg.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(BRINGUP_CFLAGS) -o $@ $(ROOT)/tools/det/test_rom_dma.c \
		$(PORT)/fs/rom_dma.c $(PORT)/fs/filelist.c $(PORT)/fs/sha1.c \
		$(BUILD)/src/port/os/mesg.o

rom-dma: overlay $(BUILD)/test_rom_dma
	$(BUILD)/test_rom_dma $(ROOT)/testdata/rom_dma/synthetic.csv $(FILELIST_U)

$(BUILD)/test_joy_playback: $(ROOT)/tools/det/test_joy_playback.c \
		$(BUILD)/src/joy.o $(PORT_OS_OBJS) | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ROOT)/tools/det/test_joy_playback.c \
		$(BUILD)/src/joy.o $(PORT_OS_OBJS) -lm

joy-playback: overlay $(BUILD)/test_joy_playback
	$(BUILD)/test_joy_playback

G1_GFX := $(PORT)/gfx/gbi_trace.c $(PORT)/gfx/gbi_interp.c $(PORT)/gfx/sw_raster.c $(PORT)/gfx/tmem.c $(PORT)/gfx/tex_bank.c $(IMAGE_NAMES_C)

$(BUILD)/test_g0: $(ROOT)/tools/det/test_g0.c $(G1_GFX) \
		$(BUILD)/src/port/fs/sha256.o $(BUILD)/src/port/fs/c0pack.o \
		$(BUILD)/src/port/fs/inflate1172.o $(BUILD)/tools/extractor/puff.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ROOT)/tools/det/test_g0.c $(G1_GFX) \
		$(BUILD)/src/port/fs/sha256.o $(BUILD)/src/port/fs/c0pack.o \
		$(BUILD)/src/port/fs/inflate1172.o $(BUILD)/tools/extractor/puff.o -lm

g0-test: overlay $(BUILD)/test_g0
	$(BUILD)/test_g0 $(BUILD)/synthetic.g0
	python3 $(ROOT)/tools/gbi_dump.py --check-hash \
		$(ROOT)/testdata/g0/synthetic.words.sha256 $(BUILD)/synthetic.g0

$(BUILD)/test_g1: $(ROOT)/tools/det/test_g1.c $(G1_GFX) \
		$(BUILD)/src/port/fs/sha256.o $(BUILD)/src/port/fs/c0pack.o \
		$(BUILD)/src/port/fs/inflate1172.o $(BUILD)/tools/extractor/puff.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ROOT)/tools/det/test_g1.c $(G1_GFX) \
		$(BUILD)/src/port/fs/sha256.o $(BUILD)/src/port/fs/c0pack.o \
		$(BUILD)/src/port/fs/inflate1172.o $(BUILD)/tools/extractor/puff.o -lm

g1-test: overlay $(BUILD)/test_g1
	$(BUILD)/test_g1 $$(cat $(ROOT)/testdata/g1/synthetic.fb.sha256)

$(BUILD)/test_pack_dma: $(ROOT)/tools/det/test_pack_dma.c $(PORT_PACK_OBJS) \
		$(BUILD)/src/port/fs/sha256.o $(BUILD)/src/port/os/mesg.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ROOT)/tools/det/test_pack_dma.c $(PORT_PACK_OBJS) \
		$(BUILD)/src/port/fs/sha256.o $(BUILD)/src/port/os/mesg.o

pack-dma: overlay $(BUILD)/test_pack_dma silveriris
	$(BUILD)/test_pack_dma $(ROOT)/testdata/pack/extract_synth.csv
	$(BUILD)/silveriris --pack $(ROOT)/testdata/pack/synthetic.c0pack --headless --ticks=1

$(BUILD)/src/glue/port_api.o: $(ROOT)/src/glue/port_api.c | overlay
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/src -I$(ROOT)/src/glue -c $< -o $@

$(BUILD)/test_port_api: $(ROOT)/tools/det/test_port_api.c $(BUILD)/src/glue/port_api.o \
		$(PORT_PACK_OBJS) $(PORT_STAGE_OBJS) $(BUILD)/src/port/fs/sha256.o \
		$(BUILD)/src/port/gfx/gbi_trace.o $(BUILD)/src/port/gfx/gbi_interp.o \
		$(BUILD)/src/port/gfx/sw_raster.o $(BUILD)/src/port/gfx/tmem.o $(BUILD)/src/port/gfx/tex_bank.o $(IMAGE_NAMES_O) $(BUILD)/src/port/os/mesg.o \
		$(BUILD)/src/port/os/ultra_stubs.o \
		$(BUILD)/src/port/audio/mixer.o $(BUILD)/src/port/audio/ai.o \
		$(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -I$(ROOT)/src/glue -o $@ \
		$(ROOT)/tools/det/test_port_api.c $(BUILD)/src/glue/port_api.o \
		$(PORT_PACK_OBJS) $(PORT_STAGE_OBJS) $(BUILD)/src/port/fs/sha256.o \
		$(BUILD)/src/port/gfx/gbi_trace.o $(BUILD)/src/port/gfx/gbi_interp.o \
		$(BUILD)/src/port/gfx/sw_raster.o $(BUILD)/src/port/gfx/tmem.o $(BUILD)/src/port/gfx/tex_bank.o $(IMAGE_NAMES_O) $(BUILD)/src/port/os/mesg.o \
		$(BUILD)/src/port/os/ultra_stubs.o \
		$(BUILD)/src/port/audio/mixer.o $(BUILD)/src/port/audio/ai.o \
		$(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o -lm

port-api: overlay $(BUILD)/test_port_api
	$(BUILD)/test_port_api $$(cat $(ROOT)/testdata/g1/synthetic.fb.sha256)

$(BUILD)/test_audio: $(ROOT)/tools/det/test_audio.c $(PORT)/audio/mixer.c \
		$(PORT)/audio/ai.c $(PORT)/rng/random.c $(PORT)/fs/sha256.c
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -I$(PORT) -o $@ \
		$(ROOT)/tools/det/test_audio.c $(PORT)/audio/mixer.c $(PORT)/audio/ai.c \
		$(PORT)/rng/random.c $(PORT)/fs/sha256.c

audio-test: $(BUILD)/test_audio
	@if grep -E 'randomGetNext|chrObjRandomGetNext|random\.h' $(PORT)/audio/mixer.c \
		$(PORT)/audio/ai.c >/dev/null; then \
		echo "FAIL: audio mixer/ai references game RNG"; exit 1; \
	fi
	$(BUILD)/test_audio $(ROOT)/testdata/audio

$(BUILD)/test_stage: $(ROOT)/tools/det/test_stage.c $(PORT_STAGE_OBJS) \
		$(PORT_PACK_OBJS) $(BUILD)/src/glue/port_api.o \
		$(BUILD)/src/port/fs/sha256.o \
		$(BUILD)/src/port/os/count.o $(BUILD)/src/port/os/mesg.o \
		$(BUILD)/src/port/os/ultra_stubs.o \
		$(BUILD)/src/port/vi/wait_override.o \
		$(BUILD)/src/port/audio/mixer.o $(BUILD)/src/port/audio/ai.o \
		$(BUILD)/src/port/gfx/gbi_trace.o $(BUILD)/src/port/gfx/gbi_interp.o \
		$(BUILD)/src/port/gfx/sw_raster.o $(BUILD)/src/port/gfx/tmem.o $(BUILD)/src/port/gfx/tex_bank.o $(IMAGE_NAMES_O) \
		$(BUILD)/src/port/rng/random.o $(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -I$(ROOT)/src/glue -o $@ $(ROOT)/tools/det/test_stage.c \
		$(BUILD)/src/glue/port_api.o \
		$(PORT_STAGE_OBJS) $(PORT_PACK_OBJS) \
		$(BUILD)/src/port/fs/sha256.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/os/mesg.o $(BUILD)/src/port/os/ultra_stubs.o \
		$(BUILD)/src/port/vi/wait_override.o \
		$(BUILD)/src/port/audio/mixer.o $(BUILD)/src/port/audio/ai.o \
		$(BUILD)/src/port/gfx/gbi_trace.o $(BUILD)/src/port/gfx/gbi_interp.o \
		$(BUILD)/src/port/gfx/sw_raster.o $(BUILD)/src/port/gfx/tmem.o $(BUILD)/src/port/gfx/tex_bank.o $(IMAGE_NAMES_O) \
		$(BUILD)/src/port/rng/random.o $(BUILD)/src/game/frametiming.o -lm

stage-test: overlay $(BUILD)/test_stage
	$(BUILD)/test_stage $$(cat $(ROOT)/testdata/g1/synthetic.fb.sha256)

$(BUILD)/test_player: $(ROOT)/tools/det/test_player.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o \
		$(BUILD)/src/port/os/count.o $(BUILD)/src/port/vi/wait_override.o \
		$(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_player.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o \
		$(BUILD)/src/port/rng/random.o $(BUILD)/src/game/frametiming.o -lm

player-test: overlay $(BUILD)/test_player
	$(BUILD)/test_player

$(BUILD)/test_gun: $(ROOT)/tools/det/test_gun.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_gun.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o -lm

gun-test: overlay $(BUILD)/test_gun
	$(BUILD)/test_gun

$(BUILD)/test_chr: $(ROOT)/tools/det/test_chr.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_chr.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o -lm

chr-test: overlay $(BUILD)/test_chr
	$(BUILD)/test_chr

$(BUILD)/test_score: $(ROOT)/tools/det/test_score.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_score.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o -lm

score-test: overlay $(BUILD)/test_score
	$(BUILD)/test_score

$(BUILD)/test_split: $(ROOT)/tools/det/test_split.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_split.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/port/det/checksum.o $(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o -lm

split-test: overlay $(BUILD)/test_split
	$(BUILD)/test_split

$(BUILD)/test_view_seat: $(ROOT)/tools/det/test_view_seat.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o \
		$(BUILD)/src/port/os/count.o $(BUILD)/src/port/vi/wait_override.o \
		$(BUILD)/src/port/rng/random.o \
		$(BUILD)/src/game/frametiming.o | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_view_seat.c \
		$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
		$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
		$(BUILD)/src/overrides/lv_clock.o \
		$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
		$(BUILD)/src/port/vi/wait_override.o \
		$(BUILD)/src/port/rng/random.o $(BUILD)/src/game/frametiming.o -lm

view-seat-test: overlay $(BUILD)/test_view_seat
	$(BUILD)/test_view_seat

TAPE_OBJS := \
	$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
	$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
	$(BUILD)/src/port/det/checksum.o $(BUILD)/src/port/det/tape.o \
	$(BUILD)/src/overrides/lv_clock.o \
	$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
	$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
	$(BUILD)/src/game/frametiming.o

$(BUILD)/test_tape: $(ROOT)/tools/det/test_tape.c $(TAPE_OBJS) | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_tape.c $(TAPE_OBJS) -lm

SYNTH_TAPE := $(ROOT)/testdata/tapes/synthetic.tape

tape-test: overlay $(BUILD)/test_tape
	$(BUILD)/test_tape $(SYNTH_TAPE)

$(BUILD)/replay: $(ROOT)/tools/det/replay.c $(TAPE_OBJS) | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/replay.c $(TAPE_OBJS) -lm

replay-synth: tape-test $(BUILD)/replay
	$(BUILD)/replay $(SYNTH_TAPE)

$(BUILD)/test_match_config: $(ROOT)/tools/det/test_match_config.c $(PORT)/net/match_config.c $(PORT)/net/match_config.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -o $@ $(ROOT)/tools/det/test_match_config.c $(PORT)/net/match_config.c -I$(PORT)

match-config: $(BUILD)/test_match_config
	$(BUILD)/test_match_config

LOCKSTEP_OBJS := \
	$(BUILD)/src/port/net/lockstep.o \
	$(BUILD)/src/port/player/move.o $(BUILD)/src/port/player/stan_walk.o $(BUILD)/src/port/player/gun.o \
	$(BUILD)/src/port/chr/patrol.o $(BUILD)/src/port/mp/score.o \
	$(BUILD)/src/port/det/checksum.o \
	$(BUILD)/src/overrides/lv_clock.o \
	$(BUILD)/src/port/vi/sim_tick.o $(BUILD)/src/port/os/count.o \
	$(BUILD)/src/port/vi/wait_override.o $(BUILD)/src/port/rng/random.o \
	$(BUILD)/src/game/frametiming.o

$(BUILD)/test_lockstep: $(ROOT)/tools/det/test_lockstep.c $(LOCKSTEP_OBJS) | overlay
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ $(ROOT)/tools/det/test_lockstep.c $(LOCKSTEP_OBJS) -lm

lockstep-test: overlay $(BUILD)/test_lockstep
	$(BUILD)/test_lockstep

EMCC ?= $(HOME)/emsdk/upstream/emscripten/emcc
WASM_OUT := $(ROOT)/web/shell/public
WASM_CFLAGS := -std=gnu11 -fno-strict-aliasing -ffp-contract=off \
	-Wno-incompatible-pointer-types -Wno-unused-variable \
	-Werror=implicit-function-declaration \
	-I$(PORT) -I$(PORT)/compat -I$(ROOT)/src -I$(ROOT)/src/glue \
	-I$(ROOT)/build/overlay -I$(DECOMP)/src -I$(DECOMP)/src/game \
	-idirafter $(DECOMP)/include -idirafter $(DECOMP) -idirafter $(DECOMP)/include/PR \
	-include $(PORT)/compat/ido.h \
	-I$(DECOMP)/tools/extractor \
	-DVERSION_US -DLANG_US -DREFRESH_NTSC -DBUGFIX_R0 -D_LANGUAGE_C -DPORT -DPORT_WASM

WASM_SRCS := \
	$(ROOT)/src/glue/port_api.c \
	$(PORT)/fs/pack_dma.c \
	$(PORT)/fs/c0pack.c \
	$(PORT)/fs/sha256.c \
	$(PORT)/fs/filelist.c \
	$(PORT)/gfx/gbi_trace.c \
	$(PORT)/gfx/gbi_interp.c \
	$(PORT)/gfx/sw_raster.c \
	$(PORT)/gfx/tmem.c \
	$(PORT)/gfx/tex_bank.c \
	$(IMAGE_NAMES_C) \
	$(PORT)/os/mesg.c \
	$(PORT)/os/count.c \
	$(PORT)/os/ultra_stubs.c \
	$(PORT)/vi/wait_override.c \
	$(PORT)/audio/mixer.c \
	$(PORT)/audio/ai.c \
	$(ROOT)/src/overrides/lv_clock.c \
	$(PORT)/vi/sim_tick.c \
	$(PORT)/fs/inflate1172.c \
	$(DECOMP)/tools/extractor/puff.c \
	$(PORT)/fs/stage.c \
	$(PORT)/fs/prop.c \
	$(PORT)/player/move.c \
	$(PORT)/player/stan_walk.c \
	$(PORT)/player/gun.c \
	$(PORT)/chr/patrol.c \
	$(PORT)/mp/score.c \
	$(PORT)/det/checksum.c \
	$(PORT)/rng/random.c

$(BUILD)/wasm/frametiming.o: $(DECOMP)/src/game/frametiming.c | overlay
	@mkdir -p $(dir $@)
	$(EMCC) $(WASM_CFLAGS) -DwaitForNextFrame=waitForNextFrame_hw -c $< -o $@

wasm: overlay $(IMAGE_NAMES_C) $(BUILD)/wasm/frametiming.o
	@if [ ! -x "$(EMCC)" ]; then echo "emcc not found at $(EMCC); install emsdk"; exit 1; fi
	@mkdir -p $(WASM_OUT)
	$(EMCC) $(WASM_CFLAGS) -O2 $(WASM_SRCS) $(BUILD)/wasm/frametiming.o -o $(WASM_OUT)/game.js \
		-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createSilverIris \
		-sENVIRONMENT=web \
		-sINITIAL_MEMORY=268435456 -sALLOW_MEMORY_GROWTH=1 \
		-sMAXIMUM_MEMORY=1073741824 \
		-sEXPORTED_FUNCTIONS='["_port_api_init","_port_api_shutdown","_port_api_fb","_port_api_fb_width","_port_api_fb_height","_port_api_draw","_port_api_last_draw","_port_api_last_error","_port_api_ready","_port_api_audio_cb","_port_api_audio_play_gun","_port_api_audio_set_music","_port_api_audio_rate","_port_api_load_stage","_port_api_sim_tick","_port_api_clock_timer","_port_api_stage_rooms","_port_api_bg_rooms","_port_api_gdl_raw","_port_api_gdl_c0","_port_api_gdl_vtx","_port_api_portal_count","_port_api_current_room","_port_api_rooms_walked","_port_api_fb_nonzero","_port_api_settex","_port_api_tex_ok","_port_api_tex_miss","_port_api_tex_miss_absent","_port_api_tex_miss_decode","_port_api_pack_files","_port_api_set_pad","_port_api_set_player_count","_port_api_player_count","_port_api_env_players","_port_api_player_x","_port_api_player_y","_port_api_player_z","_port_api_player_theta","_port_api_player_phi","_port_api_set_look_delta","_port_api_player_x_at","_port_api_player_z_at","_port_api_player_theta_at","_port_api_vp_left","_port_api_vp_top","_port_api_vp_width","_port_api_vp_height","_port_api_gun_mag","_port_api_gun_reserve","_port_api_gun_hits","_port_api_gun_have_hit","_port_api_gun_hit_x","_port_api_gun_hit_y","_port_api_gun_hit_z","_port_api_crc_players","_port_api_chr_count","_port_api_chr_x","_port_api_chr_z","_port_api_chr_theta","_port_api_chr_action","_port_api_crc_chrs","_port_api_kills","_port_api_crc_objectives","_port_api_rng_lo","_port_api_chr_rng_lo","_port_api_begin_match","_port_api_set_view_seat","_port_api_view_seat","_port_api_view_unsplit","_port_api_set_screen_size","_port_api_set_screen_position","_port_api_set_perspective","_port_api_view_hfov","_malloc","_free"]' \
		-sEXPORTED_RUNTIME_METHODS='["HEAPU8","HEAP16","UTF8ToString"]' \
		-sNO_EXIT_RUNTIME=1
	@echo "wrote $(WASM_OUT)/game.js $(WASM_OUT)/game.wasm"

trial:
	$(ROOT)/native/compile_trial.sh

clean:
	rm -rf $(BUILD)
