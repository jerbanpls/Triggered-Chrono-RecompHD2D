# Chrono Trigger (USA) 1.0: symbols (funcs.toml), extern hooks, generated
# code, the frontends linked against it, and every ROM-dependent test.

set(GAME_DIR ${CMAKE_CURRENT_SOURCE_DIR})
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_rom.py ${GAME_DIR} ${CT_ROM}
    RESULT_VARIABLE rom_ok)
if(NOT rom_ok EQUAL 0)
    message(FATAL_ERROR "CT_ROM check failed for ${GAME_DIR}: ${CT_ROM}")
endif()

# ---- generated code (into the build tree) ----
set(CT_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)
set(CT_FUNCS_TOML ${GAME_DIR}/funcs.toml)
file(GLOB CT_RECOMP_PY ${CMAKE_SOURCE_DIR}/recomp/*.py)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CT_FUNCS_TOML})
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/recomp/emit.py --list-modules
            --funcs ${CT_FUNCS_TOML}
    OUTPUT_VARIABLE CT_MODULES OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE ct_list_ok)
if(NOT ct_list_ok EQUAL 0)
    message(FATAL_ERROR "emit.py --list-modules failed")
endif()
# Code the game decompresses into WRAM (#92): images from the ROM, checked
# against overlays.toml's hashes, then recompiled with the rest.
set(CT_OVERLAYS_TOML ${GAME_DIR}/overlays.toml)
set(CT_OVERLAY_MANIFEST ${CT_OUT}/overlays/manifest.toml)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CT_OVERLAYS_TOML})
add_custom_command(
    OUTPUT ${CT_OVERLAY_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E env CT_ROM=${CT_ROM}
            ${Python3_EXECUTABLE} ${GAME_DIR}/tools/overlay_images.py ${CT_OVERLAYS_TOML}
            ${CT_OUT}/overlays
    DEPENDS ${CT_OVERLAYS_TOML} ${GAME_DIR}/tools/overlay_images.py
            ${GAME_DIR}/tools/ct_decompress.py
    COMMENT "overlay images"
    VERBATIM)

set(CT_GEN_OUTPUTS ${CT_OUT}/ct_funcs.h ${CT_OUT}/ct_funcs.c ${CT_OUT}/ct_overlays.c)
set(CT_GEN_SOURCES ${CT_OUT}/ct_overlays.c)
foreach(m ${CT_MODULES})
    list(APPEND CT_GEN_OUTPUTS ${CT_OUT}/${m}.c ${CT_OUT}/${m}.h)
    list(APPEND CT_GEN_SOURCES ${CT_OUT}/${m}.c)
endforeach()
add_custom_command(
    OUTPUT ${CT_GEN_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E env CT_ROM=${CT_ROM}
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/recomp/emit.py --out ${CT_OUT}
            --funcs ${CT_FUNCS_TOML} --overlays ${CT_OVERLAY_MANIFEST}
    DEPENDS ${CT_RECOMP_PY} ${CT_FUNCS_TOML} ${CT_OVERLAY_MANIFEST}
    COMMENT "emit ${CT_MODULES}"
    VERBATIM)

# Recompiled functions plus this game's own C: extern hooks, and the patches
# funcs.toml names (port: co-op), and sprites.c (docs/SCENE.md A1). build_module.py compiles the same *.c.
file(GLOB CT_GAME_C ${GAME_DIR}/*.c)
add_library(ct_gen STATIC ${CT_GEN_SOURCES} ${CT_GAME_C})
target_include_directories(ct_gen PUBLIC ${CT_OUT} ${GAME_DIR})
target_link_libraries(ct_gen PUBLIC ct_runtime ct_sched)
target_compile_options(ct_gen PRIVATE ${CT_WARN} -Wno-unused-label)
target_compile_definitions(ct_gen PRIVATE CT_TEST_BUILD CT_TABLES_DEFINE)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(ct_gen PRIVATE ${CT_GEN_OPT})   # see CT_GEN_OPT
endif()

# The tables (func_table.h), as objects so they precede ct_interp.
# scene.c, world_scene.c, audio.c, text.c: the scene description, the music
# watcher and the text boxes (runtime/module.h), objects so the
# weak references in func_table_static.c find them.
add_library(ct_gen_tables OBJECT ${CT_OUT}/ct_funcs.c ${CMAKE_SOURCE_DIR}/runtime/func_table_static.c
    scene.c world_scene.c audio.c text.c)
target_compile_definitions(ct_gen_tables PRIVATE CT_TABLES_DEFINE)
target_include_directories(ct_gen_tables PUBLIC ${CT_OUT})
target_link_libraries(ct_gen_tables PUBLIC ct_runtime)
target_compile_options(ct_gen_tables PRIVATE ${CT_WARN})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(ct_gen_tables PRIVATE ${CT_GEN_OPT})
endif()

function(ct_game_exe name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/test)
    target_link_libraries(${name} ct_sched ct_gen_tables ct_gen)
    target_compile_options(${name} PRIVATE ${CT_WARN})
endfunction()

# ---- frontends, at the top of the build tree ----
ct_game_exe(ct_boot ${CMAKE_SOURCE_DIR}/tools/ct_boot.c ${CMAKE_SOURCE_DIR}/tools/tas_resync.c)
target_link_libraries(ct_boot ct_packs)   # port: --music-pack (music.h)
set_target_properties(ct_boot PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(SDL2_FOUND)
    ct_game_exe(ct_sdl ${CMAKE_SOURCE_DIR}/frontend/main.c ${CMAKE_SOURCE_DIR}/frontend/saves.c
        ${CMAKE_SOURCE_DIR}/frontend/qol.c   # port: quality of life (qol.h)
        ${CMAKE_SOURCE_DIR}/frontend/padtest.c   # port: the controller test (padtest.h)
        ${CMAKE_SOURCE_DIR}/frontend/pack_scene.c)   # port: asset packs in the scene
    # port: the game id names its saves folder, as for ct_sdl_engine
    get_filename_component(ct_game_id ${GAME_DIR} NAME)
    target_compile_definitions(ct_sdl PRIVATE CT_GAME_ID="${ct_game_id}")
    target_link_libraries(ct_sdl ct_input ct_hd2d ct_packs ct_hdtext ct_menu)
    set_target_properties(ct_sdl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# ---- tests (all ROM-dependent) ----
# port: the same game code as a loadable module (runtime/module.h), and
# engine-only programs that load it with --module. The engine programs
# contain no game code: built from the runtime and the empty tables only.
add_library(ct_game_module MODULE ${CT_GEN_SOURCES} ${CT_GAME_C} ${CT_OUT}/ct_funcs.c
    ${CMAKE_SOURCE_DIR}/runtime/module_entry.c)
target_include_directories(ct_game_module PRIVATE ${CT_OUT} ${CMAKE_SOURCE_DIR}/runtime)
target_compile_definitions(ct_game_module PRIVATE CT_TEST_BUILD CT_TABLES_DEFINE)
target_compile_options(ct_game_module PRIVATE ${CT_WARN} -Wno-unused-label)
set_target_properties(ct_game_module PROPERTIES PREFIX "" OUTPUT_NAME ct_game
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR} C_VISIBILITY_PRESET hidden)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(ct_game_module PRIVATE ${CT_GEN_OPT})
endif()
add_dependencies(ct_game_module ct_gen)   # the generated sources

# ct_boot_engine and ct_sdl_engine: defined in the top-level CMakeLists.txt,
# which builds them without a game (CT_ENGINE_ONLY).

set(ct_game_tests)

# port: the module build must match the static one frame for frame.
add_test(NAME module_parity COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_module_parity.py
    $<TARGET_FILE:ct_boot> $<TARGET_FILE:ct_boot_engine> $<TARGET_FILE:ct_game_module>
    ${GAME_DIR}/test/replay/leene_square.txt 3000)
list(APPEND ct_game_tests module_parity)

# port: audio packs end to end (game/ct/audio.c, frontend/packs/music.c): the
# Gato fight's song replaced by a synthetic tone; the music voices muted,
# the sound effects kept, and the sound before it byte for byte the same.
add_test(NAME music_pack COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_music_pack.py
    $<TARGET_FILE:ct_boot_engine> --module $<TARGET_FILE:ct_game_module>)
list(APPEND ct_game_tests music_pack)

# The scene description (scene.c) against an independent decoding of the
# same WRAM and OAM (tools/scene_probe.py --text), static and as a module.
add_test(NAME scene_decode COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_scene.py
    $<TARGET_FILE:ct_boot> $<TARGET_FILE:ct_boot_engine> $<TARGET_FILE:ct_game_module>
    ${GAME_DIR}/test/replay ${GAME_DIR}/tools/scene_probe.py)
list(APPEND ct_game_tests scene_decode)

# The sprite decoder (sprites.c) against the console's OAM, VRAM, CGRAM and
# drawn frame: Leene Square while walking (8500-8700), the bell plaza
# (9100, 9200), and with a follower (party slot 2 poked at frame 8000).
ct_game_exe(test_sprites test/test_sprites.c)
add_test(NAME sprites_leene COMMAND test_sprites ${GAME_DIR}/test/replay/leene_square.txt
    8500 8600 8650 8700 8805 9100 9200)
add_test(NAME sprites_follower COMMAND test_sprites ${GAME_DIR}/test/replay/leene_square.txt
    --poke 8000:2981^82 8600 8650 8700)
list(APPEND ct_game_tests sprites_leene sprites_follower)

# port: the package scan (tools/check_dist.py) passes the engine-only
# programs and fails the module, the static programs, and ROM pieces.
set(ct_dist_pass $<TARGET_FILE:ct_boot_engine>)
set(ct_dist_fail $<TARGET_FILE:ct_game_module> $<TARGET_FILE:ct_boot>)
if(SDL2_FOUND)
    list(APPEND ct_dist_pass $<TARGET_FILE:ct_sdl_engine>)
    list(APPEND ct_dist_fail $<TARGET_FILE:ct_sdl>)
endif()
add_test(NAME check_dist COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_check_dist.py
    ${CMAKE_SOURCE_DIR}/tools/check_dist.py ${CT_ROM} --pass ${ct_dist_pass} --fail ${ct_dist_fail})
list(APPEND ct_game_tests check_dist)

ct_game_exe(test_c1_math test/test_c1_math.c)
foreach(t mul8 mulaccum divide shifts)
    add_test(NAME c1_math_${t} COMMAND test_c1_math ${t})
    add_test(NAME interp_c1_math_${t} COMMAND test_c1_math ${t} interp)
    list(APPEND ct_game_tests c1_math_${t} interp_c1_math_${t})
endforeach()

ct_game_exe(test_c1_text test/test_c1_text.c)
foreach(t format divten reencode)
    add_test(NAME c1_text_${t} COMMAND test_c1_text ${t})
    add_test(NAME interp_c1_text_${t} COMMAND test_c1_text ${t} interp)
    list(APPEND ct_game_tests c1_text_${t} interp_c1_text_${t})
endforeach()

ct_game_exe(test_c1_ui test/test_c1_ui.c)
add_test(NAME c1_ui_gauge COMMAND test_c1_ui)
add_test(NAME interp_c1_ui_gauge COMMAND test_c1_ui interp)
list(APPEND ct_game_tests c1_ui_gauge interp_c1_ui_gauge)

# The reset path's NMI/IRQ trampolines ($C00B64, $C00B75), native and interpreted.
ct_game_exe(test_c0_spine test/test_c0_spine.c)
add_test(NAME c0_spine_vectors COMMAND test_c0_spine)
add_test(NAME interp_c0_spine_vectors COMMAND test_c0_spine interp)
list(APPEND ct_game_tests c0_spine_vectors interp_c0_spine_vectors)

# Every recompiled function vs the interpreter, 2000 trials, in shards.
ct_game_exe(test_diff ${CMAKE_SOURCE_DIR}/test/test_diff.c)
set(CT_DIFF_SHARDS 8)
math(EXPR ct_last_shard "${CT_DIFF_SHARDS} - 1")
foreach(k RANGE ${ct_last_shard})
    add_test(NAME diff_all_${k} COMMAND test_diff 2000 --shard ${k}/${CT_DIFF_SHARDS})
    list(APPEND ct_game_tests diff_all_${k})
endforeach()

# Regression (#29): Sub_B309 trial 32 reads the open-bus $2188 right after
# LDA long,X's operand fetch; generated code must leave the operand's last
# byte on the bus as the interpreter does.
add_test(NAME diff_open_bus COMMAND test_diff 200 Sub_B309)
list(APPEND ct_game_tests diff_open_bus)

add_test(NAME sync_determinism COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_sync.py)
set_tests_properties(sync_determinism PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME manual_root COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_manual_root.py)
add_test(NAME notcode COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_notcode.py)
add_test(NAME discover_loop COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_discover_loop.py)
list(APPEND ct_game_tests sync_determinism manual_root notcode)

# tools/ct_decompress.py against the game's own decompressor, call by call.
add_test(NAME decompress COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_decompress.py
    ${CMAKE_BINARY_DIR})
list(APPEND ct_game_tests decompress)

# Boot from reset in the interpreter to frame 1500, on the title screen:
# no fatal, NMIs running (1387 by then; the bound leaves room for timing
# fixes), frame not black,
# audio produced.
add_test(NAME boot_interp COMMAND ct_boot --frames 1500 --min-nmis 1350 --require-render
    --require-audio)
# Start on the title (frames 1500-1505): a new game opens the Battle Mode
# menu through Menu_InitPpu ($C2940D), which writes the read-only
# $4216-$4219. Found in ct_sdl, where the keyboard drives pad 1.
add_test(NAME boot_menu COMMAND ct_boot --frames 1700 --input 1500-1505:start
    --expect-pc C2940D --require-render)
list(APPEND ct_game_tests boot_interp boot_menu)

# Native dispatch vs the interpreter alone: per-frame state hashes taken at
# the frame edge must match on every frame (tools/lockstep.py reports the
# first divergent one). The boot to the title; a new game through
# Crono's house and the world map into Leene Square up to the bell; and on
# from there into Gato's fight, the first battle ($C10089 Battle_Mul8 runs
# only in battle).
add_test(NAME lockstep_boot COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/lockstep.py
    $<TARGET_FILE:ct_boot> --frames 1500)
add_test(NAME lockstep_leene_square COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> --frames 9210
    --script ${GAME_DIR}/test/replay/leene_square.txt --expect-pc C2940D)
add_test(NAME lockstep_gato_battle COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> --frames 11000
    --script ${GAME_DIR}/test/replay/gato_battle.txt --expect-pc C10089)
# The first battle won: by frame 13400 Gato's HP (battle record 6,
# $7E6133) is 0 and Crono's (BatWork_CurHp, $7E5E30) is not.
add_test(NAME lockstep_gato_win COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> --frames 13950
    --script ${GAME_DIR}/test/replay/gato_win.txt
    --expect-wram 13400:6133=0 --expect-wram 13400:6134=0 --expect-wram 13400:5E30!=0)
# The C stack stays flat through a long stretch of field play (#101): the
# field loop there jumps between compiled functions every frame, which used
# to nest a C frame each time (275 KB by frame 10600; now about 2 KB).
add_test(NAME stack_field_replay COMMAND $<TARGET_FILE:ct_boot> --frames 10600
    --script ${GAME_DIR}/test/replay/gato_win.txt --overlay-strict --max-stack 32768)
list(APPEND ct_game_tests lockstep_boot lockstep_leene_square lockstep_gato_battle
    lockstep_gato_win stack_field_replay)

# port: the world-map menu and an in-game save (replay/world_map_save.txt):
# opening the menu runs decimal-mode ADC/SBC ($C2F15E), fatal before D=1
# was implemented; SRAM must change at the save (frame 8001) and nowhere
# else, through a reset and Continue. Native, then native vs interpreter.
add_test(NAME save_replay COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_save_replay.py
    $<TARGET_FILE:ct_boot>)
add_test(NAME lockstep_world_map_save COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> --frames 10100
    --script ${GAME_DIR}/test/replay/world_map_save.txt --expect-pc C2F15E)
list(APPEND ct_game_tests save_replay lockstep_world_map_save)

# port: a soft reset (replay/soft_reset.txt): L+R+Start+Select in the field
# jumps from deep in native code to the program's start ($C28523), which
# reloads S; the native frames it leaves must give way to the executor, not
# stop the game (fatal before CT_MODULE_ABI 3). Back to the title, native,
# then native vs interpreter.
add_test(NAME soft_reset_replay COMMAND $<TARGET_FILE:ct_boot> --frames 9800
    --script ${GAME_DIR}/test/replay/soft_reset.txt --expect-pc C28523 --require-render)
add_test(NAME lockstep_soft_reset COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> --frames 9800
    --script ${GAME_DIR}/test/replay/soft_reset.txt --expect-pc C28523)
list(APPEND ct_game_tests soft_reset_replay lockstep_soft_reset)

# The mode 7 cutscene engine's bike race (scene 0; 9 is its variant),
# reached from reset by editing the boot script's own `FF 02` (its argument
# at $7F2061, read at frame 60: XOR 2 ^ id; docs/SCENE.md W11). From frame
# 141 the race patches its own main loop (the JSR at $7E3023) while that
# loop runs natively, so the interpreter takes over the rest of it (the
# overlay fallback); this stopped at frame 225 with `more than 64 nested
# overlay functions` until the fallback stopped nesting itself. 1200
# frames, native and interpreted, then native vs interpreter (with the
# fallback: strict mode stops at the patch by design).
set(ct_mode7_race --script ${GAME_DIR}/test/replay/world_map_save.txt --frames 1200
    --overlay-fallback)
add_test(NAME mode7_race COMMAND ct_boot ${ct_mode7_race} --poke-wram 59:12061^2
    --expect-pc C30D9A --require-render)
add_test(NAME mode7_race_interp COMMAND ct_boot ${ct_mode7_race} --poke-wram 59:12061^2
    --interp-only --expect-pc C30D9A --require-render)
add_test(NAME lockstep_mode7_race COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> ${ct_mode7_race}
    --poke-wram 59:12061^2 --expect-pc C30D9A)
add_test(NAME lockstep_mode7_race9 COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> ${ct_mode7_race}
    --poke-wram 59:12061^b --expect-pc C30D6D)
list(APPEND ct_game_tests mode7_race mode7_race_interp lockstep_mode7_race lockstep_mode7_race9)

# port: save states (runtime/savestate.h, test/test_savestate.py): on the
# Leene Square replay, a state saved at frame N and loaded must run on
# exactly as the run that saved it (every frame's state hashes and
# picture), saving must not change that run, and a save-and-load every K
# frames must leave the whole run unchanged. Native (the save lands deep
# in native code; the load resumes in the interpreter) and interpreted.
add_test(NAME savestate_replay COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_savestate.py
    $<TARGET_FILE:ct_boot> ${GAME_DIR}/test/replay/leene_square.txt 8500 9210 7)
add_test(NAME savestate_replay_interp COMMAND ${Python3_EXECUTABLE}
    ${GAME_DIR}/test/test_savestate.py $<TARGET_FILE:ct_boot>
    ${GAME_DIR}/test/replay/leene_square.txt 3000 3600 5 --interp-only)
# The world map (1000 A.D., entered at frame 7252): saved at frame 7600
# exactly (format 1 states waited for an edge with the SPC700 at an
# instruction boundary, one in four; 7600 was not one), and a round trip at
# every frame edge, each with the SPC700 wherever it is in its instruction.
add_test(NAME savestate_world_map COMMAND ${Python3_EXECUTABLE}
    ${GAME_DIR}/test/test_savestate.py $<TARGET_FILE:ct_boot>
    ${GAME_DIR}/test/replay/world_map_save.txt 7600 7720 1 --every-edge)
list(APPEND ct_game_tests savestate_replay savestate_replay_interp savestate_world_map)

# port: always run (game/ct/always_run.c, sched_pref): walking up to the bell
# without the run button, the leader's Y velocity ($7E1D2D) is -$10; with
# --always-run, -$20 (running). The module build takes the same patches.
set(ct_always_run --frames 8900 --script ${GAME_DIR}/test/replay/leene_square.txt)
add_test(NAME always_run COMMAND ct_boot ${ct_always_run} --always-run
    --expect-wram 8900:1D2D=E0)
add_test(NAME always_run_off COMMAND ct_boot ${ct_always_run} --expect-wram 8900:1D2D=F0)
add_test(NAME always_run_module COMMAND $<TARGET_FILE:ct_boot_engine>
    --module $<TARGET_FILE:ct_game_module> ${ct_always_run} --always-run
    --expect-wram 8900:1D2D=E0)
list(APPEND ct_game_tests always_run always_run_off always_run_module)

# port: simulated rumble (game/ct/rumble.c, runtime/rumble.h): the events
# ct_boot --log-rumble lists on the gato_win replay are the battle's hits,
# crits and the kill at the frames HP changes, and Leene's Bell; the
# frame hashes with it equal those without; the module logs the same.
add_test(NAME rumble_replay COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_rumble_replay.py
    $<TARGET_FILE:ct_boot> $<TARGET_FILE:ct_boot_engine> $<TARGET_FILE:ct_game_module>
    ${GAME_DIR}/test/replay/gato_win.txt 13450)
list(APPEND ct_game_tests rumble_replay)

# Battle sprites must not drop out on alternate frames (Gato's fight from
# its start; broken until OAMADD kept its own reload value, test_hw_map).
add_test(NAME battle_no_flicker COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_flicker.py
    $<TARGET_FILE:ct_boot> 10680 11100 --script ${GAME_DIR}/test/replay/gato_battle.txt)
list(APPEND ct_game_tests battle_no_flicker)

# Native coverage must not regress: share of instructions run natively
# through the first battle (50.6% when added; 49.2% once HDMA took its
# CPU time, which shortens native wait loops; ct_boot --profile N lists
# the functions the rest ran in).
add_test(NAME native_coverage COMMAND ct_boot --frames 11000 --min-native 49
    --script ${GAME_DIR}/test/replay/gato_battle.txt)
list(APPEND ct_game_tests native_coverage)

# The field menu's seven screens (replay/menu_tour.txt), native == interpreter.
add_test(NAME lockstep_menu_tour COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lockstep.py $<TARGET_FILE:ct_boot> --frames 11450
    --script ${GAME_DIR}/test/replay/menu_tour.txt)
list(APPEND ct_game_tests lockstep_menu_tour)

# Interpreter share per replay must not rise above its recorded ceiling
# (upstream issue #108; test/interp_ceilings.txt, lower-only).
foreach(r leene_square gato_battle gato_win soft_reset world_map_save
          coop_battle coop_field coop_mash coop_split menu_tour)
    add_test(NAME interp_share_${r} COMMAND ${Python3_EXECUTABLE}
        ${GAME_DIR}/test/test_interp_share.py $<TARGET_FILE:ct_boot>
        ${GAME_DIR}/test/replay ${GAME_DIR}/test/interp_ceilings.txt ${r})
    list(APPEND ct_game_tests interp_share_${r})
endforeach()

# port: co-op (game/ct/coop.c, replay/coop_battle.txt): Gato's fight with
# the girl in the party. With --coop her command menu ($7E95D5 = 1) takes
# pad 2 only: pad 1's A presses and a pad 2 A held across the hand-off pick
# nothing (still open at 14720), pad 2's presses close it (14741). Without
# --coop the same script is a one-player run: pad 1 closes it by 14563. The
# module build takes the same patches.
set(ct_coop_battle --frames 14750 --script ${GAME_DIR}/test/replay/coop_battle.txt)
add_test(NAME coop_battle COMMAND ct_boot ${ct_coop_battle} --coop --expect-wram 14462:95D5=1
    --expect-wram 14720:95D5=1 --expect-wram 14745:95D5=FF)
add_test(NAME coop_battle_off COMMAND ct_boot ${ct_coop_battle} --expect-wram 14462:95D5=1
    --expect-wram 14600:95D5=FF)
add_test(NAME coop_battle_module COMMAND $<TARGET_FILE:ct_boot_engine>
    --module $<TARGET_FILE:ct_game_module> ${ct_coop_battle} --coop
    --expect-wram 14720:95D5=1 --expect-wram 14745:95D5=FF)
list(APPEND ct_game_tests coop_battle coop_battle_off coop_battle_module)
# port: co-op in the field (replay/coop_field.txt): pad 2 walks follower 1
# away (her X high byte $1807 is $1D at frame 12540, $19 without --coop),
# then she rejoins: at 12820 she is where the one-player run has her
# (345.5, 211.94 px).
set(ct_coop_field --frames 12820 --script ${GAME_DIR}/test/replay/coop_field.txt
    --expect-wram 12820:1806=98 --expect-wram 12820:1807=15 --expect-wram 12820:1886=3F
    --expect-wram 12820:1887=0D)
add_test(NAME coop_field COMMAND ct_boot ${ct_coop_field} --coop --expect-wram 12540:1807=1D)
add_test(NAME coop_field_off COMMAND ct_boot ${ct_coop_field} --expect-wram 12540:1807=19)
list(APPEND ct_game_tests coop_field coop_field_off)
# port: the party slots a player has (--coop-chars, settings.ini
# coop_chars): with player 2 given only slot 3, the girl (slot 2) stays
# player 1's, who closes her menu as in a one-player run.
add_test(NAME coop_battle_chars COMMAND ct_boot ${ct_coop_battle} --coop --coop-chars 3
    --expect-wram 14600:95D5=FF)
# port: the experimental split screen (replay/coop_split.txt): a second
# view while she is far from the leader, one view once she is back, none
# without --coop-split.
add_test(NAME coop_split COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_coop_split.py
    $<TARGET_FILE:ct_boot_engine> ${GAME_DIR}/test/replay/coop_split.txt
    --module $<TARGET_FILE:ct_game_module>)
list(APPEND ct_game_tests coop_battle_chars coop_split)
# port: the hand-off guard with pad 2 mashing A across it
# (replay/coop_mash.txt): the press held through the hand-off is not one,
# so her menu closes on the two after it (14479), not at 14470.
add_test(NAME coop_mash COMMAND ct_boot --frames 14500 --coop
    --script ${GAME_DIR}/test/replay/coop_mash.txt --expect-wram 14474:95D5=1
    --expect-wram 14500:95D5=FF)
# port: right in Crono's menu with the girl ready: one player moves to her
# menu ($95D5 = 1 by 14406); with co-op she is player 2's, so it stays.
add_test(NAME coop_cycle COMMAND ct_boot --frames 14420 --coop --script
    ${GAME_DIR}/test/replay/coop_battle.txt --input 14400-14403:right --expect-wram 14420:95D5=0)
add_test(NAME coop_cycle_off COMMAND ct_boot --frames 14420 --script
    ${GAME_DIR}/test/replay/coop_battle.txt --input 14400-14403:right --expect-wram 14420:95D5=1)
list(APPEND ct_game_tests coop_mash coop_cycle coop_cycle_off)

if(SDL2_FOUND)
    # The frontend itself, headless (SDL dummy drivers), unpaced. port: every
    # ct_sdl test runs with --no-save, so none reads or writes a save file.
    add_test(NAME sdl_boot COMMAND ct_sdl --frames 300 --fast --require-render --no-save)
    # --record while playing the Leene Square replay; the recording must
    # reproduce the run frame for frame in ct_boot.
    add_test(NAME sdl_record COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_record.py
        $<TARGET_FILE:ct_sdl> $<TARGET_FILE:ct_boot> ${GAME_DIR}/test/replay/leene_square.txt 9210)
    # 60 s of boot in real time through the SDL audio queue (dummy driver,
    # 960-sample buffer = 20 ms, which it consumes at the true rate): no
    # underrun once playback starts.
    add_test(NAME sdl_audio_60s COMMAND ct_sdl --frames 3606 --require-no-underrun --no-save)
    # Real-time pacing: other tests running beside it starve the audio queue.
    set_tests_properties(sdl_audio_60s PROPERTIES RUN_SERIAL TRUE)
    list(APPEND ct_game_tests sdl_boot sdl_record sdl_audio_60s)
    # port: HD text (frontend/hdtext.h): the dialogue box at frame 4800 with a
    # font found on the system (skipped without one, or without offscreen GL).
    add_test(NAME sdl_hdtext COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_hdtext_shot.py
        $<TARGET_FILE:ct_sdl_engine> --module $<TARGET_FILE:ct_game_module>)
    set_tests_properties(sdl_hdtext PROPERTIES SKIP_RETURN_CODE 77)
    list(APPEND ct_game_tests sdl_hdtext)
    # port: the 3D diorama look only where the console shows the map: a
    # black narration screen over a loaded map stays black (frontend/hd2d).
    add_test(NAME sdl_hidden_map_3d COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_hidden_map_3d.py
        $<TARGET_FILE:ct_sdl_engine> --module $<TARGET_FILE:ct_game_module>)
    set_tests_properties(sdl_hidden_map_3d PROPERTIES SKIP_RETURN_CODE 77)
    list(APPEND ct_game_tests sdl_hidden_map_3d)
    # port: the HD-2D side fill above and below the picture (a tall window).
    add_test(NAME sdl_side_fill_tall COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_side_fill_tall.py
        $<TARGET_FILE:ct_sdl_engine> --module $<TARGET_FILE:ct_game_module>)
    set_tests_properties(sdl_side_fill_tall PROPERTIES SKIP_RETURN_CODE 77)
    list(APPEND ct_game_tests sdl_side_fill_tall)
    # port: quality of life (frontend/qol.h): save states, the resume point
    # (SIGTERM, sleep, the prompt), screenshots, pause, fast-forward,
    # battery mode, save RAM import/export; headless, scripted.
    add_test(NAME sdl_qol COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_sdl_qol.py
        $<TARGET_FILE:ct_sdl> $<TARGET_FILE:ct_boot> ${GAME_DIR}/test/replay/leene_square.txt)
    set_tests_properties(sdl_qol PROPERTIES RUN_SERIAL TRUE)   # real-time parts
    list(APPEND ct_game_tests sdl_qol)
    # port: the in-game menu (frontend/menu.h), opened by the menu action at
    # frame 150, driven by synthetic controller events: the look and the
    # volume change in settings.ini, no frame runs while it is open, and
    # every frame hash matches a run without it. sdl_menu_hd2d does the
    # same through the HD-2D renderer where SDL's offscreen driver has
    # OpenGL (else it skips).
    # port: asset packs with original art (docs/ASSET-PACKS.md): glTF models on
    # the fair's map and the world map, the leader's poses exported as sheets,
    # replaced by a smiley sheet cut back into a pack (2D and 3D), through
    # the engine and the module; where SDL's offscreen driver has OpenGL.
    add_test(NAME sdl_pack_art COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_pack_art.py
        $<TARGET_FILE:ct_sdl_engine> $<TARGET_FILE:ct_game_module> ${GAME_DIR}/test/replay/leene_square.txt)
    list(APPEND ct_game_tests sdl_pack_art)
    # The text box over the 3D diorama (docs/QA-3D.md)
    add_test(NAME sdl_3d_box COMMAND ${Python3_EXECUTABLE} ${GAME_DIR}/test/test_3d_box.py
        $<TARGET_FILE:ct_sdl_engine> $<TARGET_FILE:ct_game_module> ${GAME_DIR}/test/replay/leene_square.txt)
    list(APPEND ct_game_tests sdl_3d_box)
    add_test(NAME sdl_menu COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_menu.py
        $<TARGET_FILE:ct_sdl> 300 150)
    add_test(NAME sdl_menu_hd2d COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_menu.py
        $<TARGET_FILE:ct_sdl> 300 150 --hd2d)
    list(APPEND ct_game_tests sdl_menu sdl_menu_hd2d)
endif()

set_tests_properties(${ct_game_tests} PROPERTIES LABELS rom ENVIRONMENT
    "CT_ROM=${CT_ROM};DSCOTTON_DIR=$ENV{DSCOTTON_DIR};SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CT_TEST_XDG}/data;XDG_CONFIG_HOME=${CT_TEST_XDG}/config")
