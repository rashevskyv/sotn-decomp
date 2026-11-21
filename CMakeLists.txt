cmake_minimum_required(VERSION 3.21)

project(sotn)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ENABLE_SATURN_STAGES "Build the Saturn stage PC ports and connectors" OFF)

if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<NOT:$<CONFIG:Debug>>:MultiThreadedDLL>")
endif()

# Overlay compatibility
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(ENABLE_ASAN "Enable AddressSanitizer" ON)
set(SOTN_RUNTIME_DATA_DIR "${CMAKE_BINARY_DIR}/data")
set(SOTN_US_DISK_DIR "${CMAKE_SOURCE_DIR}/disks/us")
file(GLOB_RECURSE SOTN_US_DISK_FILES CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${SOTN_US_DISK_DIR}/*")
set(SOTN_RUNTIME_DATA_FILES)
foreach(source_file IN LISTS SOTN_US_DISK_FILES)
    file(RELATIVE_PATH relative_file "${SOTN_US_DISK_DIR}" "${source_file}")
    set(runtime_file "${SOTN_RUNTIME_DATA_DIR}/${relative_file}")
    get_filename_component(runtime_directory "${runtime_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${runtime_directory}")
    add_custom_command(
        OUTPUT "${runtime_file}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source_file}" "${runtime_file}"
        DEPENDS "${source_file}"
        VERBATIM)
    list(APPEND SOTN_RUNTIME_DATA_FILES "${runtime_file}")
endforeach()
add_custom_target(sotn_runtime_data ALL DEPENDS ${SOTN_RUNTIME_DATA_FILES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    if(NOT NINTENDO_SWITCH) # the Switch build is too slow without -O2
        set(CMAKE_BUILD_TYPE Debug)
    endif()
    if(ENABLE_ASAN)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
        set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
    endif()

    # silenced warnings required for PS1 matching
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-braces")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-implicit-function-declaration")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-incompatible-pointer-types")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-int-conversion")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-return-mismatch")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-return-type")

    # Warnings promoted to errors that directly address underlying issues
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=return-local-addr")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=sizeof-array-argument")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=switch-bool")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=unused-result")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=pointer-compare")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=div-by-zero")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=overflow")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=shift-count-negative")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=shift-count-overflow")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=discarded-qualifiers")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=stringop-overflow")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=stringop-overread")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=switch-outside-range")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=switch-unreachable")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=builtin-declaration-mismatch")
    # newer GCC (devkitA64 GCC 16, Linux GCC 13+) reports false positives on
    # PS1 pointer math; keep them as warnings
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=stringop-overflow")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=stringop-overread")
endif()

if(MSVC)
    add_compile_options(/J) # Treat char as unsigned
else()
    add_compile_options(-funsigned-char)
endif()
if(MINGW)
    # direct references instead of .refptr.* COMDATs: a module's local
    # copy would win over the host's (psyz static modules, vita_ovl_link)
    add_compile_options(-mcmodel=small)
endif()

find_program(CARGO_BIN cargo)
if(NOT CARGO_BIN)
    message(FATAL_ERROR
        "cargo is required to compile sotn_str\n"
        "https://doc.rust-lang.org/stable/cargo/getting-started/installation.html")
endif()

# prevent sotn_str binary type conflicts when working with containers
set(SOTN_STR_TARGET_DIR "${CMAKE_BINARY_DIR}/sotn_str")
# sotn_str runs on the build machine: a cross toolchain changes
# CMAKE_EXECUTABLE_SUFFIX (.elf for the Switch), the host suffix does not.
if(CMAKE_HOST_WIN32)
    set(SOTN_HOST_EXE_SUFFIX ".exe")
else()
    set(SOTN_HOST_EXE_SUFFIX "")
endif()
set(SOTN_STR_BIN
    "${SOTN_STR_TARGET_DIR}/release/sotn_str${SOTN_HOST_EXE_SUFFIX}")
add_custom_command(
    OUTPUT "${SOTN_STR_BIN}"
    COMMAND ${CMAKE_COMMAND} -E env
    "CARGO_TARGET_DIR=${SOTN_STR_TARGET_DIR}"
    "${CARGO_BIN}" build --release
    --manifest-path "${CMAKE_SOURCE_DIR}/tools/sotn_str/Cargo.toml"
    DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/sotn_str/Cargo.toml"
    "${CMAKE_SOURCE_DIR}/tools/sotn_str/Cargo.lock"
    "${CMAKE_SOURCE_DIR}/tools/sotn_str/src/main.rs"
    VERBATIM
    COMMENT "Building host sotn_str")
add_custom_target(sotn_str_host DEPENDS "${SOTN_STR_BIN}")

# PSP Maria, a third playable character. Her data is only on the PSP disc:
# make extract_disk_pspeu (or 7z x sotn.pspeu.iso ...PSPBIN/maria.bin).
set(MARIA_BIN
    "${CMAKE_SOURCE_DIR}/disks/pspeu/PSP_GAME/USRDIR/res/ps/PSPBIN/maria.bin")
if(EXISTS "${MARIA_BIN}")
    option(USE_PLAYER_MARIA "PSP Maria as a playable character" ON)
else()
    option(USE_PLAYER_MARIA "PSP Maria as a playable character" OFF)
endif()

if(ENABLE_SATURN_STAGES OR USE_PLAYER_MARIA)
    find_program(GO_BIN go REQUIRED)
    file(GLOB_RECURSE SATURN_ASSETS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/saturn/assets/src/*.rs")
    file(GLOB_RECURSE SOTN_ASSETS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/sotn-assets/*.go")
    list(APPEND SOTN_ASSETS_SOURCES
        "${CMAKE_SOURCE_DIR}/tools/sotn-assets/go.mod"
        "${CMAKE_SOURCE_DIR}/tools/sotn-assets/go.sum")
    set(SATURN_ASSETS_TARGET_DIR
        "${CMAKE_BINARY_DIR}/saturn_assets")
    set(SATURN_ASSETS_BIN
        "${SATURN_ASSETS_TARGET_DIR}/release/saturn-assets${SOTN_HOST_EXE_SUFFIX}")
    add_custom_command(
        OUTPUT "${SATURN_ASSETS_BIN}"
        COMMAND ${CMAKE_COMMAND} -E env
                "CARGO_TARGET_DIR=${SATURN_ASSETS_TARGET_DIR}"
                "${CARGO_BIN}" build --release
                --manifest-path
                "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
            "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.lock"
            ${SATURN_ASSETS_SOURCES}
        VERBATIM
        COMMENT "Building Rust Saturn asset tool")
    set(SOTN_ASSETS_HOST_BIN
        "${CMAKE_BINARY_DIR}/sotn_assets_host/sotn-assets${SOTN_HOST_EXE_SUFFIX}")
    add_custom_command(
        OUTPUT "${SOTN_ASSETS_HOST_BIN}"
        COMMAND ${CMAKE_COMMAND} -E env
                "GOCACHE=${CMAKE_BINARY_DIR}/sotn_assets_host/cache"
                "${GO_BIN}" build -o "${SOTN_ASSETS_HOST_BIN}"
                ./tools/sotn-assets
        DEPENDS ${SOTN_ASSETS_SOURCES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VERBATIM
        COMMENT "Building host sotn-assets")
endif()

set(_sotn_str_args "${SOTN_STR_BIN};cc")

# Allows to run the sotn_str pre-compiler only to selected targets
function(sotn_str_apply target)
    add_dependencies(${target} sotn_str_host)
    if(MSVC)
        sotn_str_apply_msvc(${target})
    else()
        set_target_properties(${target} PROPERTIES C_COMPILER_LAUNCHER "${_sotn_str_args}")
    endif()
endfunction()

# specific target for MSVC
function(sotn_str_apply_msvc target)
    get_target_property(_srcs ${target} SOURCES)
    file(GLOB_RECURSE _sotn_str_headers CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/*.h")
    set(_inc "$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>")
    set(_def "$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>")
    set(_inc_arg "$<$<BOOL:${_inc}>:/I$<JOIN:${_inc},;/I>>")
    set(_def_arg "$<$<BOOL:${_def}>:/D$<JOIN:${_def},;/D>>")

    set(_new_srcs "")
    foreach(_src ${_srcs})
        get_filename_component(_ext "${_src}" EXT)
        if(NOT _ext STREQUAL ".c")
            list(APPEND _new_srcs "${_src}")
            continue()
        endif()

        get_filename_component(_abs "${_src}" ABSOLUTE)
        file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_abs}")
        set(_out "${CMAKE_BINARY_DIR}/sotn_str_gen/${target}/${_rel}")
        get_filename_component(_out_dir "${_out}" DIRECTORY)

        get_source_file_property(_sdefs "${_src}" COMPILE_DEFINITIONS)
        set(_sdef_args "")
        if(_sdefs AND NOT _sdefs STREQUAL "NOTFOUND")
            foreach(_d ${_sdefs})
                list(APPEND _sdef_args "/D${_d}")
            endforeach()
        endif()

        add_custom_command(
            OUTPUT "${_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
            COMMAND "${CMAKE_C_COMPILER}" /nologo /E /DSOTN_STR "${_inc_arg}" "${_def_arg}" ${_sdef_args} "${_abs}" > "${_out}.i"
            COMMAND "${SOTN_STR_BIN}" process --sjis -f "${_out}.i" > "${_out}"
            DEPENDS "${_abs}" "${SOTN_STR_BIN}" ${_sotn_str_headers}
            COMMAND_EXPAND_LISTS
            VERBATIM
            COMMENT "sotn_str: re-encoding ${_rel}")
        list(APPEND _new_srcs "${_out}")
    endforeach()

    set_property(TARGET ${target} PROPERTY SOURCES "${_new_srcs}")
endfunction()

set(SOURCE_FILES_PC
    src/pc/main.c
    src/pc/stubs.c
    src/pc/sotn.c
    src/pc/replay.c
    src/pc/spawn_point.c
    src/pc/room_stitch.c
    src/pc/pc.c
    src/pc/cutscene_pc.c
    src/pc/io.c
    src/pc/sim_pc.c
    src/pc/pl_arc.c
    src/pc/pl_ric.c
    src/pc/servant_pc.c
    src/pc/stages/overlay.c
    src/pc/weapon_pc.c
    src/pc/wide.c
    src/pc/port_menu.c
    src/pc/buttons.c
    src/pc/voice.c
    src/pc/music.c
    src/pc/saturn_sfx.c
    src/pc/lang.c
    src/pc/cheats.c
    src/pc/saturn_items.c
    src/pc/apron.c
    src/pc/editor_net.c
)

set(SOURCE_FILES_DRA
    src/dra/42398.c
    src/dra/game_handlers.c
    src/dra/pads.c
    src/dra/save_mgr.c
    src/dra/4A538.c
    src/dra/4AEA4.c
    src/dra/4B758.c
    src/dra/4CE2C.c
    src/dra/4DA70.c
    src/dra/collider.c
    src/dra/demo.c
    src/dra/5087C.c
    src/dra/lba_stage.c
    src/dra/config_us.c
    src/dra/menu.c
    src/dra/timeattack.c
    src/dra/5D5BC.c
    src/dra/5F60C.c
    src/dra/624DC.c
    src/dra/62DEC.c
    src/dra/d_37d8.c
    src/dra/66590.c
    src/dra/d_CD80.c
    src/dra/692E8.c
    src/dra/6BF64.c
    src/dra/alu_anim.c
    src/dra/6DF70.c
    src/dra/6E42C.c
    src/dra/6FDF8.c
    src/dra/70394.c
    src/dra/704D0.c
    src/dra/71830.c
    src/dra/7879C.c
    src/dra/7E4BC.c
    src/dra/d_E294.c
    src/dra/84B88.c
    src/dra/8A0A4.c
    src/dra/8BEF8.c
    src/dra/wolfForm.c
    src/dra/sound.c
    src/dra/sound_data.c
    src/dra/d_2217C.c
    src/dra/d_24CEC.c
    src/dra/d_2F324.c
    src/dra/d_2FE48.c
    src/dra/d_3B0D4.c
    src/dra/bss.c
)

set(SOURCE_FILES_RIC
    src/ric/pl_header.c
    src/ric/pl_main.c
    src/ric/pl_debug.c
    src/ric/pl_steps.c
    src/ric/pl_utils.c
    src/ric/pl_setstep.c
    src/ric/pl_collision.c
    src/ric/pl_blueprints.c
    src/ric/pl_whip.c
    src/ric/pl_subweapon_dummy.c
    src/ric/pl_subweapon_holywater.c
    src/ric/pl_subweapon_cross.c
    src/ric/pl_subweapons.c
    src/ric/2F8E8.c
    src/ric/319C4.c
    src/ric/spritebanks.c
    src/ric/pl_crash_cross.c
    src/ric/bss.c
    src/ric/d_18568.c
    src/ric/d_1859C.c
    src/ric/pl_anims.c
)

SET(SOURCE_FILES_MARIA
    src/maria/pl_header.c
    src/maria/pl_anims.c
    src/maria/pl_subweapons.c
    src/maria/pl_main.c
    src/maria/pl_debug.c
    src/maria/pl_steps.c
    src/maria/pl_setstep.c
    src/maria/pl_utils.c
    src/maria/pl_sensors.c
    src/maria/pl_collision.c
    src/maria/pl_blueprints.c
    src/maria/spritebanks.c
    src/maria/ovl_load.c
)

set(SOURCE_FILES_TT_000
    src/servant/tt_000/init.c
    src/servant/tt_000/bat.c
    src/servant/tt_000/servant_clutdata.c
    src/servant/tt_000/servant_spriteparts.c
    src/pc/servants/servant_000.c
)

set(SOURCE_FILES_TT_001
    src/servant/tt_001/init.c
    src/servant/tt_001/ghost.c
    src/servant/tt_001/servant_clutdata.c
    src/servant/tt_001/servant_spriteparts.c
    src/pc/servants/servant_001.c
)

set(SOURCE_FILES_TT_002
    src/servant/tt_002/init.c
    src/servant/tt_002/faerie.c
    src/servant/tt_002/faerie_spriteparts.c
    src/pc/servants/servant_002.c
)

set(SOURCE_FILES_TT_003
    src/servant/tt_003/init.c
    src/servant/tt_003/demon.c
    src/servant/tt_003/demon_spriteparts.c
    src/pc/servants/servant_003.c
)

set(SOURCE_FILES_TT_004
    src/servant/tt_004/init.c
    src/servant/tt_004/sword.c
    src/servant/tt_004/servant_clutdata.c
    src/pc/servants/servant_004.c
)

set(SOURCE_FILES_TT_005
    src/servant/tt_005/init.c
    src/servant/tt_005/sprite.c
    src/servant/tt_005/sprite_clutdata.c
    src/servant/tt_005/sprite_spriteparts.c
    src/pc/servants/servant_005.c
)

set(SOURCE_FILES_TT_006
    src/servant/tt_006/init.c
    src/servant/tt_006/nosedevil.c
    src/servant/tt_006/nosedevil_spriteparts.c
    src/servant/tt_006/servant_clutdata.c
    src/pc/servants/servant_006.c
)

set(SOURCE_FILES_STAGE_SEL
    src/pc/stages/stage_sel.c
    src/st/sel/bss.c
    src/st/sel/banks.c
    src/st/sel/764.c
    src/st/sel/C404.c
    src/st/sel/CD54.c
    src/st/sel/sprites.c
    src/st/sel/F9A8.c
    src/st/sel/select.c
    src/st/sel/utils.c
    src/st/sel/memcard.c
    src/st/sel/logo.c
    src/st/sel/title.c
    src/st/sel/ending.c
    src/st/sel/cutscene.c
    src/st/sel/credits.c
    src/st/sel/cutscene_data.c
)

set(SOURCE_FILES_STAGE_NO0
    src/pc/stages/stage_no0.c
    src/st/no0/header.c
    src/st/no0/gen/e_laydef.c
    src/st/no0/e_init.c
    src/st/no0/e_red_door_tiles.c
    src/st/no0/e_room_bg.c
    src/st/no0/e_lock_camera.c
    src/st/no0/e_breakable.c
    src/st/no0/e_elevator.c
    src/st/no0/42A34.c
    src/st/no0/e_explosion_puff_opaque.c
    src/st/no0/d_prize_drops.c
    src/st/no0/st_update.c
    src/st/no0/collision.c
    src/st/no0/create_entity.c
    src/st/no0/e_red_door.c
    src/st/no0/e_room_fg.c
    src/st/no0/e_particles.c
    src/st/no0/st_common.c
    src/st/no0/e_collect.c
    src/st/no0/e_misc.c
    src/st/no0/4C750.c
    src/st/no0/clock_room.c
    src/st/no0/4E2E0.c
    src/st/no0/e_stage_name.c
    src/st/no0/e_diplocephalus.c
    src/st/no0/e_diplocephalus_tail.c
    src/st/no0/e_skelerang.c
    src/st/no0/e_plate_lord.c
    src/st/no0/e_plate_lord_2.c
    src/st/no0/e_ghost.c
    src/st/no0/e_marionette.c
    src/st/no0/e_slinger.c
    src/st/no0/e_stone_rose.c
    src/st/no0/e_ctulhu.c
    src/st/no0/ctulhu_shockwave_data.c
    src/st/no0/e_axe_knight.c
    src/st/no0/e_ouija_table.c
    src/st/no0/e_flea_man.c
    src/st/no0/e_skeleton.c
    src/st/no0/e_sealed_door.c
    src/st/no0/prim_helpers.c
    src/st/no0/popup.c
    src/st/no0/gen/rooms.c
    src/st/no0/gen/e_layout.c
    src/st/no0/stage_data.c
    src/st/no0/gen/sprites.c
)

set(SOURCE_FILES_STAGE_LIB
    src/pc/stages/stage_lib.c
    src/st/lib/header.c
    src/st/lib/palette_def.c
    src/st/lib/layers.c
    src/st/lib/graphics_banks.c
    src/st/lib/gen/e_laydef.c
    src/st/lib/e_init.c
    src/st/lib/background_block_init.c
    src/st/lib/e_red_door_tiles.c
    src/st/lib/d_prize_drops.c
    src/st/lib/gen/rooms.c
    src/st/lib/gen/e_layout.c
    src/st/lib/stage_data.c
    src/st/lib/tilemaps.c
    src/st/lib/gen/sprites.c
    src/st/lib/e_room_bg.c
    src/st/lib/e_lock_camera.c
    src/st/lib/e_breakable.c
    src/st/lib/first_c_file.c
    src/st/lib/e_library_bg.c
    src/st/lib/e_shop.c
    src/st/lib/cutscene.c
    src/st/lib/e_candle_table.c
    src/st/lib/unk_3B200.c
    src/st/lib/unk_3B53C.c
    src/st/lib/e_lesser_demon.c
    src/st/lib/e_chair.c
    src/st/lib/st_update.c
    src/st/lib/collision.c
    src/st/lib/create_entity.c
    src/st/lib/e_red_door.c
    src/st/lib/e_room_fg.c
    src/st/lib/st_common.c
    src/st/lib/e_misc.c
    src/st/lib/e_collect.c
    src/st/lib/e_bloody_zombie.c
    src/st/lib/e_flying_zombie.c
    src/st/lib/e_stage_name.c
    src/st/lib/e_particles.c
    src/st/lib/popup.c
    src/st/lib/e_dhuron.c
    src/st/lib/e_ectoplasm.c
    src/st/lib/e_thornweed_corpseweed.c
    src/st/lib/e_skeleton.c
    src/st/lib/e_mist_door.c
    src/st/lib/e_flea_armor.c
    src/st/lib/e_flea_man.c
    src/st/lib/e_mudman.c
    src/st/lib/prim_helpers.c
    src/st/lib/e_spellbook_magic_tome.c
    src/st/lib/e_life_up.c
)

set(SOURCE_FILES_STAGE_CAT
    src/pc/stages/stage_cat.c
    src/st/cat/header.c
    src/st/cat/sprite_banks.c
    src/st/cat/palette_def.c
    src/st/cat/layers.c
    src/st/cat/graphics_banks.c
    src/st/cat/gen/e_laydef.c
    src/st/cat/e_init.c
    src/st/cat/e_red_door_tiles.c
    src/st/cat/background_block_init.c
    src/st/cat/d_prize_drops.c
    src/st/cat/gen/rooms.c
    src/st/cat/gen/e_layout.c
    src/st/cat/gfx_data.c
    src/st/cat/tilemaps.c
    src/st/cat/unused_sprites.c
    src/st/cat/gen/us/sprites.c
    src/st/cat/e_room_bg.c
    src/st/cat/e_lock_camera.c
    src/st/cat/e_breakable.c
    src/st/cat/e_spike_room_light_switch.c
    src/st/cat/e_spikes.c
    src/st/cat/unk_3872C.c
    src/st/cat/unk_38AD0.c
    src/st/cat/e_elevator.c
    src/st/cat/e_secrets.c
    src/st/cat/unk_3A164.c
    src/st/cat/e_coffin.c
    src/st/cat/unk_3ACF4.c
    src/st/cat/unk_3B4CC.c
    src/st/cat/st_update.c
    src/st/cat/st_collision.c
    src/st/cat/create_entity.c
    src/st/cat/e_red_door.c
    src/st/cat/st_common.c
    src/st/cat/e_misc.c
    src/st/cat/e_collect.c
    src/st/cat/e_lava.c
    src/st/cat/e_cavern_background.c
    src/st/cat/e_bone_ark.c
    src/st/cat/e_stage_name.c
    src/st/cat/e_lava_glow.c
    src/st/cat/e_discus_lord.c
    src/st/cat/e_hellfire_beast.c
    src/st/cat/e_particles.c
    src/st/cat/e_room_fg.c
    src/st/cat/popup.c
    src/st/cat/giantbro_helpers.c
    src/st/cat/e_lossoth.c
    src/st/cat/e_grave_keeper.c
    src/st/cat/e_tombstone.c
    src/st/cat/e_gremlin.c
    src/st/cat/e_slime.c
    src/st/cat/e_wereskeleton.c
    src/st/cat/e_thornweed_corpseweed.c
    src/st/cat/e_blood_skeleton.c
    src/st/cat/prim_helpers.c
)

set(SOURCE_FILES_STAGE_NO2
    src/pc/stages/stage_no2.c
    src/st/no2/header.c
    src/st/no2/gen/e_laydef.c
    src/st/no2/e_init.c
    src/st/no2/gen/rooms.c
    src/st/no2/gen/e_layout.c
    src/st/no2/gen/sprites.c
    src/st/no2/graphics_banks.c
    src/st/no2/layers.c
    src/st/no2/palette_def.c
    src/st/no2/sprite_banks.c
    src/st/no2/tilemaps.c
    src/st/no2/stage_data.c
    src/st/no2/e_room_bg.c
    src/st/no2/e_lock_camera.c
    src/st/no2/e_breakable.c
    src/st/no2/d_prize_drops.c
    src/st/no2/st_update.c
    src/st/no2/collision.c
    src/st/no2/create_entity.c
    src/st/no2/e_red_door_tiles.c
    src/st/no2/e_red_door.c
    src/st/no2/st_common.c
    src/st/no2/e_collect.c
    src/st/no2/e_misc.c
    src/st/no2/e_stage_name.c
    src/st/no2/e_particles.c
    src/st/no2/e_room_fg.c
    src/st/no2/popup.c
    src/st/no2/prim_helpers.c
    src/st/no2/33D8C.c
    src/st/no2/34DA4.c
    src/st/no2/372E8.c
    src/st/no2/37580.c
    src/st/no2/giantbro_helpers_2.c
    src/st/no2/blade.c
    src/st/no2/e_background_house.c
    src/st/no2/e_bloody_zombie.c
    src/st/no2/e_life_up.c
    src/st/no2/e_owl_knight.c
    src/st/no2/e_prisoner.c
    src/st/no2/e_sealed_door.c
    src/st/no2/e_secrets.c
    src/st/no2/e_skelerang.c
    src/st/no2/e_spectral_sword.c
    src/st/no2/e_spikes.c
    src/st/no2/e_valhalla_knight.c
    src/st/no2/giantbro_helpers.c
    src/st/no2/gurkha.c
    src/st/no2/hammer.c
)

set(SOURCE_FILES_STAGE_CHI
    src/pc/stages/stage_chi.c
    src/st/chi/dt_header.c
    src/st/chi/gen/e_laydef.c
    src/st/chi/e_init.c
    src/st/chi/background_block_init.c
    src/st/chi/e_red_door_tiles.c
    src/st/chi/e_lock_camera.c
    src/st/chi/en_breakable.c
    src/st/chi/en_demon_switch_wall.c
    src/st/chi/en_breakable_wall.c
    src/st/chi/en_falling_stairs.c
    src/st/chi/dt_prize_drops.c
    src/st/chi/st_update.c
    src/st/chi/st_collision.c
    src/st/chi/en_red_door.c
    src/st/chi/ut_common.c
    src/st/chi/en_collect.c
    src/st/chi/en_misc.c
    src/st/chi/en_particles.c
    src/st/chi/en_room_foreground.c
    src/st/chi/e_salem_witch.c
    src/st/chi/e_gremlin.c
    src/st/chi/e_thornweed_corpseweed.c
    src/st/chi/en_venus_weed.c
    src/st/chi/gen/rooms.c
    src/st/chi/gen/e_layout.c
    src/st/chi/stage_data.c
    src/st/chi/gen/sprites.c
    src/st/chi/en_debug_cerberus_gate.c
    src/st/chi/en_stage_name.c
    src/st/chi/ut_prim_helpers.c
    src/st/chi/e_room_bg.c
    src/st/chi/ut_create_entity.c
    src/st/chi/en_popup.c
    src/st/chi/bss.c
)

set(SOURCE_FILES_STAGE_DAI
    src/pc/stages/stage_dai.c
    src/st/dai/header.c
    src/st/dai/gen/e_laydef.c
    src/st/dai/e_init.c
    src/st/dai/gen/rooms.c
    src/st/dai/gen/e_layout.c
    src/st/dai/gen/us/sprites.c
    src/st/dai/gfx_data.c
    src/st/dai/graphics_banks.c
    src/st/dai/layers.c
    src/st/dai/palette_def.c
    src/st/dai/pal_stained_glass.c
    src/st/dai/sprite_banks.c
    src/st/dai/tilemaps.c
    src/st/dai/background_block_init.c
    src/st/dai/e_room_bg.c
    src/st/dai/e_room_bg_2.c
    src/st/dai/e_lock_camera.c
    src/st/dai/e_breakable.c
    src/st/dai/d_prize_drops.c
    src/st/dai/cutscene_data.c
    src/st/dai/st_update.c
    src/st/dai/st_collision.c
    src/st/dai/create_entity.c
    src/st/dai/e_red_door_tiles.c
    src/st/dai/e_red_door.c
    src/st/dai/st_common.c
    src/st/dai/e_collect.c
    src/st/dai/e_misc.c
    src/st/dai/e_stage_name.c
    src/st/dai/e_particles.c
    src/st/dai/e_room_fg.c
    src/st/dai/e_room_fg_2.c
    src/st/dai/popup.c
    src/st/dai/prim_helpers.c
    src/st/dai/e_bat.c
    src/st/dai/e_bell.c
    src/st/dai/e_bone_halberd.c
    src/st/dai/e_bone_pillar.c
    src/st/dai/e_chair.c
    src/st/dai/e_clouds.c
    src/st/dai/e_confessional.c
    src/st/dai/e_corner_guard.c
    src/st/dai/e_crow.c
    src/st/dai/e_cutscene_actors.c
    src/st/dai/e_cutscene_dialogue.c
    src/st/dai/e_dai_no2_shortcut.c
    src/st/dai/e_end_chime.c
    src/st/dai/e_gargoyle.c
    src/st/dai/e_hunting_girl.c
    src/st/dai/e_mist_door.c
    src/st/dai/e_sealed_door.c
    src/st/dai/e_skelerang.c
    src/st/dai/e_spectral_sword.c
    src/st/dai/e_spikes.c
    src/st/dai/e_stained_glass.c
    src/st/dai/e_tower_exit.c
    src/st/dai/e_winged_guard.c
)

set(SOURCE_FILES_STAGE_RDAI
    src/pc/stages/stage_rdai.c
    src/st/rdai/header.c
    src/st/rdai/sprite_banks.c
    src/st/rdai/pal_8C.c
    src/st/rdai/palette_def.c
    src/st/rdai/layers.c
    src/st/rdai/graphics_banks.c
    src/st/rdai/gen/e_laydef.c
    src/st/rdai/e_init.c
    src/st/rdai/e_room_bg.c
    src/st/rdai/e_red_door_tiles.c
    src/st/rdai/e_lock_camera.c
    src/st/rdai/e_breakable.c
    src/st/rdai/e_stained_glass.c
    src/st/rdai/d_prize_drops.c
    src/st/rdai/unk_33B94.c
    src/st/rdai/e_spikes.c
    src/st/rdai/st_update.c
    src/st/rdai/st_collision.c
    src/st/rdai/e_red_door.c
    src/st/rdai/st_common.c
    src/st/rdai/e_collect.c
    src/st/rdai/e_misc.c
    src/st/rdai/e_particles.c
    src/st/rdai/e_room_fg.c
    src/st/rdai/e_archer.c
    src/st/rdai/e_spectral_sword.c
    src/st/rdai/e_sniper_of_goth.c
    src/st/rdai/e_imp.c
    src/st/rdai/e_balloon_pod.c
    src/st/rdai/e_black_panther.c
    src/st/rdai/gen/rooms.c
    src/st/rdai/e_clouds.c
    src/st/rdai/gen/e_layout.c
    src/st/rdai/stage_data.c
    src/st/rdai/tilemaps.c
    src/st/rdai/gen/us/sprites.c
    src/st/rdai/e_tower_exit.c
    src/st/rdai/e_stage_name.c
    src/st/rdai/prim_helpers.c
    src/st/rdai/e_room_fg_2.c
    src/st/rdai/e_room_bg_2.c
    src/st/rdai/create_entity.c
    src/st/rdai/popup.c
)

set(SOURCE_FILES_STAGE_RARE
    src/pc/stages/stage_rare.c
    src/st/rare/header.c
    src/st/rare/sprite_banks.c
    src/st/rare/palette_def.c
    src/st/rare/layers.c
    src/st/rare/graphics_banks.c
    src/st/rare/gen/e_laydef.c
    src/st/rare/e_init.c
    src/st/rare/background_block_init.c
    src/st/rare/e_red_door_tiles.c
    src/st/rare/e_lock_camera.c
    src/st/rare/e_breakable.c
    src/st/rare/d_prize_drops.c
    src/st/rare/e_secrets.c
    src/st/rare/e_background_door_rubble.c
    src/st/rare/e_boss_torch.c
    src/st/rare/st_update.c
    src/st/rare/collision.c
    src/st/rare/e_red_door.c
    src/st/rare/st_common.c
    src/st/rare/e_collect.c
    src/st/rare/e_misc.c
    src/st/rare/e_particles.c
    src/st/rare/e_room_fg.c
    src/st/rare/e_minotaur.c
    src/st/rare/e_azaghal.c
    src/st/rare/e_white_dragon.c
    src/st/rare/e_stone_skull.c
    src/st/rare/gen/rooms.c
    src/st/rare/e_werewolf.c
    src/st/rare/gen/e_layout.c
    src/st/rare/stage_data.c
    src/st/rare/tilemaps.c
    src/st/rare/gen/us/sprites.c
    src/st/rare/e_boss_doors.c
    src/st/rare/e_stage_name.c
    src/st/rare/prim_helpers.c
    src/st/rare/e_room_bg.c
    src/st/rare/e_fountain_water.c
    src/st/rare/create_entity.c
    src/st/rare/popup.c
)

set(SOURCE_FILES_STAGE_NO1
    src/pc/stages/stage_no1.c
    src/st/no1/header.c
    src/st/no1/sprite_banks.c
    src/st/no1/palette_def.c
    src/st/no1/layers.c
    src/st/no1/graphics_banks.c
    src/st/no1/gen/e_laydef.c
    src/st/no1/e_init.c
    src/st/no1/e_room_bg.c
    src/st/no1/e_red_door.c
    src/st/no1/e_red_door_tiles.c
    src/st/no1/e_lock_camera.c
    src/st/no1/e_breakable.c
    src/st/no1/unk_36490.c
    src/st/no1/e_mouse.c
    src/st/no1/e_chair.c
    src/st/no1/unk_381E8.c
    src/st/no1/unk_38F50.c
    src/st/no1/unk_39028.c
    src/st/no1/unk_39184.c
    src/st/no1/unk_3A290.c
    src/st/no1/unk_3B5E0.c
    src/st/no1/unk_3D8AC.c
    src/st/no1/e_secrets.c
    src/st/no1/unk_3FA34.c
    src/st/no1/d_prize_drops.c
    src/st/no1/st_update.c
    src/st/no1/collision.c
    src/st/no1/st_common.c
    src/st/no1/e_collect.c
    src/st/no1/e_misc.c
    src/st/no1/e_blue_axe_knight.c
    src/st/no1/e_particles.c
    src/st/no1/e_room_fg.c
    src/st/no1/e_skeleton.c
    src/st/no1/e_wereskeleton.c
    src/st/no1/e_bone_musket.c
    src/st/no1/e_sword_lord.c
    src/st/no1/e_bone_archer.c
    src/st/no1/e_armor_lord.c
    src/st/no1/e_spear_guard.c
    src/st/no1/unk_54400.c
    src/st/no1/e_skeleton_ape.c
    src/st/no1/e_medusa_head.c
    src/st/no1/e_mist_door.c
    src/st/no1/gen/rooms.c
    src/st/no1/gen/e_layout.c
    src/st/no1/stage_data.c
    src/st/no1/tilemaps.c
    src/st/no1/gen/us/sprites.c
    src/st/no1/unk_40E98.c
    src/st/no1/e_stage_name.c
    src/st/no1/prim_helpers.c
    src/st/no1/create_entity.c
    src/st/no1/popup.c
)

set(SOURCE_FILES_STAGE_NP3
    src/pc/stages/stage_np3.c
    src/st/np3/header.c
    src/st/np3/gen/us/e_laydef.c
    src/st/np3/e_init.c
    src/st/np3/gen/rooms.c
    src/st/np3/gen/us/e_layout.c
    src/st/np3/gen/sprites.c
    src/st/np3/stage_data.c
    src/st/np3/background_block_init.c
    src/st/np3/e_room_bg.c
    src/st/np3/e_lock_camera.c
    src/st/np3/e_breakable.c
    src/st/np3/d_prize_drops.c
    src/st/np3/st_update.c
    src/st/np3/collision.c
    src/st/np3/create_entity.c
    src/st/np3/e_red_door_tiles.c
    src/st/np3/e_red_door.c
    src/st/np3/st_common.c
    src/st/np3/e_collect.c
    src/st/np3/e_misc.c
    src/st/np3/e_stage_name.c
    src/st/np3/e_particles.c
    src/st/np3/e_room_fg.c
    src/st/np3/popup.c
    src/st/np3/prim_helpers.c
    src/st/np3/bss.c
    src/st/np3/giantbro_helpers_2.c
    src/st/np3/d_water_data.c
    src/st/np3/water_effects.c
    src/st/np3/entrance_weights.c
    src/st/np3/e_background_bushes_trees.c
    src/st/np3/e_background_lightning.c
    src/st/np3/e_bat.c
    src/st/np3/blade.c
    src/st/np3/e_bloody_zombie.c
    src/st/np3/e_bone_scimitar.c
    src/st/np3/e_castle_door.c
    src/st/np3/e_cavern_door.c
    src/st/np3/e_explosion_puff_opaque.c
    src/st/np3/e_heartroom.c
    src/st/np3/e_jewel_sword_puzzle.c
    src/st/np3/e_merman.c
    src/st/np3/e_merman2.c
    src/st/np3/e_owl_knight.c
    src/st/np3/e_shutting_window.c
    src/st/np3/e_sky_entities.c
    src/st/np3/e_stairway.c
    src/st/np3/e_tilemap_shuffler.c
    src/st/np3/e_transparent_water.c
    src/st/np3/e_trapdoor.c
    src/st/np3/e_unkId16.c
    src/st/np3/e_zombie.c
    src/st/np3/gaibon.c
    src/st/np3/slogra.c
    src/st/np3/giantbro_helpers.c
    src/st/np3/gurkha.c
    src/st/np3/hammer.c
)

set(SOURCE_FILES_STAGE_CEN
    src/pc/stages/stage_cen.c
    src/st/cen/header.c
    src/st/cen/gen/e_laydef.c
    src/st/cen/e_init.c
    src/st/cen/gen/rooms.c
    src/st/cen/gen/e_layout.c
    src/st/cen/cutscene_data.c
    src/st/cen/stage_data.c
    src/st/cen/gen/sprites.c
    src/st/cen/background_block_init.c
    src/st/cen/e_room_bg.c
    src/st/cen/e_lock_camera.c
    src/st/cen/e_breakable.c
    src/st/cen/d_prize_drops.c
    src/st/cen/cutscene.c
    src/st/cen/e_chamber.c
    src/st/cen/e_maria.c
    src/st/cen/e_elevator.c
    src/st/cen/st_update.c
    src/st/cen/collision.c
    src/st/cen/create_entity.c
    src/st/cen/e_red_door_tiles.c
    src/st/cen/e_red_door.c
    src/st/cen/st_common.c
    src/st/cen/e_collect.c
    src/st/cen/e_misc.c
    src/st/cen/e_stage_name.c
    src/st/cen/e_particles.c
    src/st/cen/e_room_fg.c
    src/st/cen/popup.c
    src/st/cen/prim_helpers.c
)

set(SOURCE_FILES_STAGE_NO4
    src/pc/stages/stage_no4.c
    src/st/no4/header.c
    src/st/no4/gen/e_laydef.c
    src/st/no4/e_init.c
    src/st/no4/background_block_init.c
    src/st/no4/e_red_door_tiles.c
    src/st/no4/e_lock_camera.c
    src/st/no4/e_breakable.c
    src/st/no4/first_c_file.c
    src/st/no4/e_ferryman.c
    src/st/no4/48248.c
    src/st/no4/482B8.c
    src/st/no4/e_secrets.c
    src/st/no4/e_bat.c
    src/st/no4/d_prize_drops.c
    src/st/no4/e_killer_fish.c
    src/st/no4/cutscene.c
    src/st/no4/st_update.c
    src/st/no4/collision.c
    src/st/no4/create_entity.c
    src/st/no4/e_red_door.c
    src/st/no4/st_common.c
    src/st/no4/e_collect.c
    src/st/no4/e_misc.c
    src/st/no4/e_stage_name.c
    src/st/no4/e_particles.c
    src/st/no4/e_room_fg.c
    src/st/no4/e_room_bg.c
    src/st/no4/popup.c
    src/st/no4/e_toad.c
    src/st/no4/e_frog.c
    src/st/no4/e_fishhead.c
    src/st/no4/e_frozen_shade.c
    src/st/no4/e_spear_guard.c
    src/st/no4/e_bone_archer.c
    src/st/no4/e_skeleton_ape.c
    src/st/no4/prim_helpers.c
    src/st/no4/gen/rooms.c
    src/st/no4/gen/e_layout.c
    src/st/no4/cutscene_data.c
    src/st/no4/stage_data.c
    src/st/no4/gen/sprites.c
)

set(SOURCE_FILES_STAGE_ARE
    src/pc/stages/stage_are.c
    src/st/are/header.c
    src/st/are/sprite_banks.c
    src/st/are/palette_def.c
    src/st/are/layers.c
    src/st/are/graphics_banks.c
    src/st/are/gen/e_laydef.c
    src/st/are/e_init.c
    src/st/are/background_block_init.c
    src/st/are/e_red_door_tiles.c
    src/st/are/d_prize_drops.c
    src/st/are/gen/rooms.c
    src/st/are/gen/e_layout.c
    src/st/are/gfx_data.c
    src/st/are/tilemaps.c
    src/st/are/gen/us/sprites.c
    src/st/are/e_room_bg.c
    src/st/are/e_lock_camera.c
    src/st/are/e_breakable.c
    src/st/are/e_boss_torch.c
    src/st/are/e_boss_doors.c
    src/st/are/e_cavern_door.c
    src/st/are/e_elevator.c
    src/st/are/e_fountain_water.c
    src/st/are/e_secrets.c
    src/st/are/e_background_door_rubble.c
    src/st/are/e_explosion_puff_opaque.c
    src/st/are/st_update.c
    src/st/are/st_collision.c
    src/st/are/create_entity.c
    src/st/are/e_red_door.c
    src/st/are/st_common.c
    src/st/are/e_collect.c
    src/st/are/e_misc.c
    src/st/are/e_stage_name.c
    src/st/are/e_particles.c
    src/st/are/e_room_fg.c
    src/st/are/popup.c
    src/st/are/e_blade_master.c
    src/st/are/e_blade_soldier.c
    src/st/are/e_bone_musket.c
    src/st/are/e_owl_knight.c
    src/st/are/e_valhalla_knight.c
    src/st/are/e_blue_axe_knight.c
    src/st/are/e_armor_lord.c
    src/st/are/e_hunting_girl.c
    src/st/are/e_paranthropus.c
    src/st/are/e_flea_armor.c
    src/st/are/e_flea_man.c
    src/st/are/e_bone_scimitar.c
    src/st/are/e_plate_lord.c
    src/st/are/e_plate_lord_2.c
    src/st/are/e_grave_keeper.c
    src/st/are/e_mist_door.c
    src/st/are/prim_helpers.c
    src/st/are/bss.c
)

set(SOURCE_FILES_STAGE_TOP
    src/pc/stages/stage_top.c
    src/st/top/header.c
    src/st/top/sprite_banks.c
    src/st/top/palette_def.c
    src/st/top/layers.c
    src/st/top/graphics_banks.c
    src/st/top/gen/e_laydef.c
    src/st/top/e_init.c
    src/st/top/e_room_bg.c
    src/st/top/e_red_door_tiles.c
    src/st/top/e_lock_camera.c
    src/st/top/e_breakable.c
    src/st/top/e_secret_stairs.c
    src/st/top/e_breakable_wall.c
    src/st/top/e_elevator.c
    src/st/top/e_lion_lamp.c
    src/st/top/d_prize_drops.c
    src/st/top/cutscene.c
    src/st/top/post_richter_cutscene.c
    src/st/top/st_update.c
    src/st/top/collision.c
    src/st/top/e_red_door.c
    src/st/top/st_common.c
    src/st/top/e_collect.c
    src/st/top/e_misc.c
    src/st/top/e_particles.c
    src/st/top/e_room_fg.c
    src/st/top/e_flea_rider.c
    src/st/top/e_blue_axe_knight.c
    src/st/top/gen/rooms.c
    src/st/top/e_clouds.c
    src/st/top/e_clock_tower.c
    src/st/top/gen/e_layout.c
    src/st/top/cutscene_data.c
    src/st/top/stage_data.c
    src/st/top/tilemaps.c
    src/st/top/gen/sprites.c
    src/st/top/e_richter_fight_trigger.c
    src/st/top/e_warp_trigger.c
    src/st/top/e_stage_name.c
    src/st/top/prim_helpers.c
    src/st/top/create_entity.c
    src/st/top/popup.c
)

set(SOURCE_FILES_STAGE_NZ0
    src/pc/stages/stage_nz0.c
    src/st/nz0/header.c
    src/st/nz0/bss.c
    src/st/nz0/gen/e_laydef.c
    src/st/nz0/e_init.c
    src/st/nz0/gen/rooms.c
    src/st/nz0/gen/e_layout.c
    src/st/nz0/stage_data.c
    src/st/nz0/gen/sprites.c
    src/st/nz0/background_block_init.c
    src/st/nz0/e_room_bg.c
    src/st/nz0/e_lock_camera.c
    src/st/nz0/e_breakable.c
    src/st/nz0/d_prize_drops.c
    src/st/nz0/e_red_eye_bust.c
    src/st/nz0/e_purple_brick.c
    src/st/nz0/e_left_secret_room_wall.c
    src/st/nz0/e_puzzle.c
    src/st/nz0/e_cannon.c
    src/st/nz0/e_elevator2.c
    src/st/nz0/e_nz0_room2.c
    src/st/nz0/e_life_max_tank.c
    src/st/nz0/e_blue_flame_table.c
    src/st/nz0/e_explosion_puff_opaque.c
    src/st/nz0/bossfight.c
    src/st/nz0/slogra.c
    src/st/nz0/gaibon.c
    src/st/nz0/e_elevator.c
    src/st/nz0/cutscene.c
    src/st/nz0/maria.c
    src/st/nz0/st_update.c
    src/st/nz0/collision.c
    src/st/nz0/create_entity.c
    src/st/nz0/e_red_door_tiles.c
    src/st/nz0/e_red_door.c
    src/st/nz0/st_common.c
    src/st/nz0/e_collect.c
    src/st/nz0/e_misc.c
    src/st/nz0/e_particles.c
    src/st/nz0/e_room_fg.c
    src/st/nz0/popup.c
    src/st/nz0/e_bone_scimitar.c
    src/st/nz0/e_axe_knight.c
    src/st/nz0/e_bloody_zombie.c
    src/st/nz0/e_skeleton.c
    src/st/nz0/e_spittle_bone.c
    src/st/nz0/e_subweapon_container.c
    src/st/nz0/e_blood_skeleton.c
    src/st/nz0/e_sealed_door.c
    src/st/nz0/e_stage_name.c
    src/st/nz0/prim_helpers.c
    src/st/nz0/e_life_up.c
)

set(SOURCE_FILES_STAGE_NZ1
    src/pc/stages/stage_nz1.c
    src/st/nz1/header.c
    src/st/nz1/sprite_banks.c
    src/st/nz1/palette_def.c
    src/st/nz1/layers.c
    src/st/nz1/graphics_banks.c
    src/st/nz1/gen/e_laydef.c
    src/st/nz1/e_init.c
    src/st/nz1/background_block_init.c
    src/st/nz1/e_red_door_tiles.c
    src/st/nz1/e_lock_camera.c
    src/st/nz1/e_breakable.c
    src/st/nz1/e_breakable_bridge.c
    src/st/nz1/e_explosion_puff_opaque.c
    src/st/nz1/e_bg_gears.c
    src/st/nz1/e_gear_horizontal.c
    src/st/nz1/e_gear_vertical.c
    src/st/nz1/e_gear_small.c
    src/st/nz1/e_gear_puzzle.c
    src/st/nz1/e_pendulum.c
    src/st/nz1/e_secrets.c
    src/st/nz1/e_water_fg.c
    src/st/nz1/d_water_data.c
    src/st/nz1/e_spikes.c
    src/st/nz1/d_prize_drops.c
    src/st/nz1/bossfight.c
    src/st/nz1/e_karasuman.c
    src/st/nz1/st_update.c
    src/st/nz1/collision.c
    src/st/nz1/e_red_door.c
    src/st/nz1/st_common.c
    src/st/nz1/e_collect.c
    src/st/nz1/e_misc.c
    src/st/nz1/e_particles.c
    src/st/nz1/e_room_fg.c
    src/st/nz1/water_effects.c
    src/st/nz1/e_skull_lord.c
    src/st/nz1/e_harpy.c
    src/st/nz1/e_cloaked_knight.c
    src/st/nz1/e_sword_lord.c
    src/st/nz1/e_vandal_sword.c
    src/st/nz1/e_phantom_skull.c
    src/st/nz1/e_flail_guard.c
    src/st/nz1/e_flea_armor.c
    src/st/nz1/e_flea_man.c
    src/st/nz1/e_medusa_head.c
    src/st/nz1/gen/rooms.c
    src/st/nz1/e_life_up.c
    src/st/nz1/gen/e_layout.c
    src/st/nz1/stage_data.c
    src/st/nz1/tilemaps.c
    src/st/nz1/gen/sprites.c
    src/st/nz1/e_clouds.c
    src/st/nz1/e_stage_name.c
    src/st/nz1/prim_helpers.c
    src/st/nz1/e_room_bg.c
    src/st/nz1/e_gear_large.c
    src/st/nz1/create_entity.c
    src/st/nz1/popup.c
)

set(SOURCE_FILES_STAGE_WRP
    src/pc/stages/stage_wrp.c
    src/st/wrp/header.c
    src/st/wrp/sprite_banks.c
    src/st/wrp/cluts.c
    src/st/wrp/layers.c
    src/st/wrp/graphics_banks.c
    src/st/wrp/gen/e_laydef.c
    src/st/wrp/e_init.c
    src/st/wrp/background_block_init.c
    src/st/wrp/e_room_bg.c
    src/st/wrp/e_lock_camera.c
    src/st/wrp/e_breakable.c
    src/st/wrp/d_prize_drops.c
    src/st/wrp/gen/rooms.c
    src/st/wrp/gen/e_layout.c
    src/st/wrp/stage_data.c
    src/st/wrp/gen/us/sprites.c
    src/st/wrp/warp.c
    src/st/wrp/st_update.c
    src/st/wrp/collision.c
    src/st/wrp/create_entity.c
    src/st/wrp/e_red_door_tiles.c
    src/st/wrp/e_red_door.c
    src/st/wrp/st_common.c
    src/st/wrp/e_collect.c
    src/st/wrp/e_misc.c
    src/st/wrp/e_stage_name.c
    src/st/wrp/e_particles.c
    src/st/wrp/e_room_fg.c
    src/st/wrp/popup.c
    src/st/wrp/prim_helpers.c
    src/st/wrp/bss.c
)

set(SOURCE_FILES_STAGE_DRE
    src/pc/stages/stage_dre.c
    src/st/dre/header.c
    src/st/dre/gen/e_laydef.c
    src/st/dre/e_init.c
    src/st/dre/gen/rooms.c
    src/st/dre/gen/e_layout.c
    src/st/dre/gen/us/sprites.c
    src/st/dre/gfx_banks.c
    src/st/dre/gfx_data.c
    src/st/dre/layers.c
    src/st/dre/palette_def.c
    src/st/dre/sprite_banks.c
    src/st/dre/tilemaps.c
    src/st/dre/background_block_init.c
    src/st/dre/e_room_bg.c
    src/st/dre/e_lock_camera.c
    src/st/dre/e_breakable.c
    src/st/dre/d_prize_drops.c
    src/st/dre/st_update.c
    src/st/dre/collision.c
    src/st/dre/create_entity.c
    src/st/dre/e_red_door_tiles.c
    src/st/dre/e_red_door.c
    src/st/dre/st_common.c
    src/st/dre/e_collect.c
    src/st/dre/e_misc.c
    src/st/dre/e_stage_name.c
    src/st/dre/e_particles.c
    src/st/dre/e_room_fg.c
    src/st/dre/popup.c
    src/st/dre/prim_helpers.c
    src/st/dre/e_background_clouds.c
    src/st/dre/e_background_house.c
    src/st/dre/e_cutscene_actors.c
    src/st/dre/e_cutscene_dialogue.c
    src/st/dre/e_succubus.c
)

set(SOURCE_FILES_BOSS_MAR
    src/pc/bosses/boss_mar.c
    src/boss/mar/header.c
    src/boss/mar/sprite_banks.c
    src/boss/mar/palette_def.c
    src/boss/mar/layers.c
    src/boss/mar/graphics_banks.c
    src/boss/mar/gen/e_laydef.c
    src/boss/mar/e_init.c
    src/boss/mar/background_block_init.c
    src/boss/mar/e_red_door_tiles.c
    src/boss/mar/e_lock_camera.c
    src/boss/mar/e_breakable.c
    src/boss/mar/cutscene.c
    src/boss/mar/d_prize_drops.c
    src/boss/mar/st_update.c
    src/boss/mar/collision.c
    src/boss/mar/e_red_door.c
    src/boss/mar/st_common.c
    src/boss/mar/e_collect.c
    src/boss/mar/e_misc.c
    src/boss/mar/e_particles.c
    src/boss/mar/e_room_fg.c
    src/boss/mar/clock_room.c
    src/boss/mar/gen/rooms.c
    src/boss/mar/e_life_up.c
    src/boss/mar/gen/e_layout.c
    src/boss/mar/cutscene_data.c
    src/boss/mar/stage_data.c
    src/boss/mar/gen/sprites.c
    src/boss/mar/CA94.c
    src/boss/mar/e_stage_name.c
    src/boss/mar/prim_helpers.c
    src/boss/mar/e_room_bg.c
    src/boss/mar/create_entity.c
    src/boss/mar/popup.c
    src/boss/mar/unk.c
)

SET(SOURCE_FILES_BOSS_BO5
    src/pc/bosses/boss_bo5.c
    src/boss/bo5/background_block_init.c
    src/boss/bo5/collision.c
    src/boss/bo5/create_entity.c
    src/boss/bo5/cutscene_data.c
    src/boss/bo5/d_prize_drops.c
    src/boss/bo5/e_bell.c
    src/boss/bo5/e_breakable.c
    src/boss/bo5/e_camera_bounds.c
    src/boss/bo5/e_clouds.c
    src/boss/bo5/e_collect.c
    src/boss/bo5/e_cutscene_actors.c
    src/boss/bo5/e_cutscene_dialogue.c
    src/boss/bo5/e_dai_no2_shortcut.c
    src/boss/bo5/e_hippogryph.c
    src/boss/bo5/e_init.c
    src/boss/bo5/e_life_up.c
    src/boss/bo5/e_lock_camera.c
    src/boss/bo5/e_misc.c
    src/boss/bo5/e_particles.c
    src/boss/bo5/e_play_boss_music.c
    src/boss/bo5/e_rbo3door.c
    src/boss/bo5/e_red_door.c
    src/boss/bo5/e_red_door_tiles.c
    src/boss/bo5/e_room_bg.c
    src/boss/bo5/e_room_bg_2.c
    src/boss/bo5/e_room_fg.c
    src/boss/bo5/e_room_fg_2.c
    src/boss/bo5/e_stage_name.c
    src/boss/bo5/e_stained_glass.c
    src/boss/bo5/gen/e_laydef.c
    src/boss/bo5/gen/e_layout.c
    src/boss/bo5/gen/rooms.c
    src/boss/bo5/gen/us/sprites.c
    src/boss/bo5/graphics_banks.c
    src/boss/bo5/header.c
    src/boss/bo5/layers.c
    src/boss/bo5/pal_stained_glass.c
    src/boss/bo5/pal_unk.c
    src/boss/bo5/palette_def.c
    src/boss/bo5/popup.c
    src/boss/bo5/prim_helpers.c
    src/boss/bo5/sprite_banks.c
    src/boss/bo5/st_common.c
    src/boss/bo5/st_update.c
    src/boss/bo5/stage_data.c
    src/boss/bo5/tilemaps.c
)

SET(SOURCE_FILES_BOSS_BO3
    src/pc/bosses/boss_bo3.c
    src/boss/bo3/background_block_init.c
    src/boss/bo3/bss.c
    src/boss/bo3/cavern_bg_effects.c
    src/boss/bo3/collision.c
    src/boss/bo3/create_entity.c
    src/boss/bo3/d_prize_drops.c
    src/boss/bo3/e_breakable.c
    src/boss/bo3/e_breakable_wall.c
    src/boss/bo3/e_collect.c
    src/boss/bo3/e_explosion_puff_opaque.c
    src/boss/bo3/e_init.c
    src/boss/bo3/e_life_up.c
    src/boss/bo3/e_lock_camera.c
    src/boss/bo3/e_misc.c
    src/boss/bo3/e_particles.c
    src/boss/bo3/e_red_door.c
    src/boss/bo3/e_red_door_tiles.c
    src/boss/bo3/e_room_bg.c
    src/boss/bo3/e_room_fg.c
    src/boss/bo3/e_scylla.c
    src/boss/bo3/e_scylla_wyrm.c
    src/boss/bo3/e_stage_name.c
    src/boss/bo3/gen/e_laydef.c
    src/boss/bo3/gen/e_layout.c
    src/boss/bo3/gen/rooms.c
    src/boss/bo3/gen/us/sprites.c
    src/boss/bo3/graphics_banks.c
    src/boss/bo3/header.c
    src/boss/bo3/layers.c
    src/boss/bo3/pal_unk.c
    src/boss/bo3/palette_def.c
    src/boss/bo3/popup.c
    src/boss/bo3/prim_helpers.c
    src/boss/bo3/sprite_banks.c
    src/boss/bo3/st_common.c
    src/boss/bo3/st_update.c
    src/boss/bo3/stage_data.c
    src/boss/bo3/tilemaps.c
    src/boss/bo3/unk_26370.c
    src/boss/bo3/water_effects.c
)

SET(SOURCE_FILES_BOSS_BO1
    src/pc/bosses/boss_bo1.c
    src/boss/bo1/background_block_init.c
    src/boss/bo1/bss.c
    src/boss/bo1/collision.c
    src/boss/bo1/create_entity.c
    src/boss/bo1/d_prize_drops.c
    src/boss/bo1/e_breakable.c
    src/boss/bo1/e_collect.c
    src/boss/bo1/e_explosion_flame.c
    src/boss/bo1/e_granfaloon.c
    src/boss/bo1/e_init.c
    src/boss/bo1/e_life_up.c
    src/boss/bo1/e_lock_camera.c
    src/boss/bo1/e_misc.c
    src/boss/bo1/e_particles.c
    src/boss/bo1/e_red_door.c
    src/boss/bo1/e_red_door_tiles.c
    src/boss/bo1/e_room_bg.c
    src/boss/bo1/e_room_fg.c
    src/boss/bo1/e_stage_name.c
    src/boss/bo1/gen/e_laydef.c
    src/boss/bo1/gen/e_layout.c
    src/boss/bo1/gen/rooms.c
    src/boss/bo1/gen/us/sprites.c
    src/boss/bo1/graphics_banks.c
    src/boss/bo1/header.c
    src/boss/bo1/layers.c
    src/boss/bo1/palette_def.c
    src/boss/bo1/popup.c
    src/boss/bo1/prim_helpers.c
    src/boss/bo1/sprite_banks.c
    src/boss/bo1/stage_data.c
    src/boss/bo1/stage_palettes.c
    src/boss/bo1/st_common.c
    src/boss/bo1/st_update.c
    src/boss/bo1/unk_210A8.c
)

set(SOURCE_FILES_STAGE_ST0
    src/pc/stages/stage_st0.c
    src/st/st0/header.c
    src/st/st0/bss.c
    src/st/st0/gen/e_laydef.c
    src/st/st0/e_init.c
    src/st/st0/gen/rooms.c
    src/st/st0/gen/e_layout.c
    src/st/st0/stage_data.c
    src/st/st0/gen/sprites.c
    src/st/st0/background_block_init.c
    src/st/st0/e_room_bg.c
    src/st/st0/e_lock_camera.c
    src/st/st0/2805C.c
    src/st/st0/cutscene.c
    src/st/st0/2A218.c
    src/st/st0/2A8DC.c
    src/st/st0/title_card.c
    src/st/st0/2C564.c
    src/st/st0/2DAC8.c
    src/st/st0/prologue_scroll.c
    src/st/st0/3101C_psx.c
    src/st/st0/st_update.c
    src/st/st0/collision.c
    src/st/st0/create_entity.c
    src/st/st0/e_red_door_tiles.c
    src/st/st0/e_red_door.c
    src/st/st0/st_common.c
    src/st/st0/e_collect.c
    src/st/st0/e_misc.c
    src/st/st0/e_particles.c
    src/st/st0/e_room_fg.c
    src/st/st0/popup.c
    src/st/st0/3AB08.c
    src/st/st0/3C5C0.c
    src/st/st0/prim_helpers.c
    src/st/st0/3D8F0.c
    src/st/st0/e_bg_vortex.c
)

set(SOURCE_FILES_STAGE_RNO0
    src/pc/stages/stage_rno0.c
    src/st/rno0/header.c
    src/st/rno0/sprite_banks.c
    src/st/rno0/palette_def.c
    src/st/rno0/layers.c
    src/st/rno0/graphics_banks.c
    src/st/rno0/gen/e_laydef.c
    src/st/rno0/e_init.c
    src/st/rno0/e_room_bg.c
    src/st/rno0/e_red_door_tiles.c
    src/st/rno0/e_lock_camera.c
    src/st/rno0/e_breakable.c
    src/st/rno0/d_prize_drops.c
    src/st/rno0/e_elevator.c
    src/st/rno0/e_caverns_block.c
    src/st/rno0/st_update.c
    src/st/rno0/collision.c
    src/st/rno0/e_red_door.c
    src/st/rno0/st_common.c
    src/st/rno0/e_collect.c
    src/st/rno0/e_misc.c
    src/st/rno0/e_corridor_decoration.c
    src/st/rno0/e_clock_room.c
    src/st/rno0/e_guardian.c
    src/st/rno0/e_particles.c
    src/st/rno0/e_room_fg.c
    src/st/rno0/e_floor_trap.c
    src/st/rno0/e_thornweed_corpseweed.c
    src/st/rno0/e_stone_skull.c
    src/st/rno0/e_jack_o_bones.c
    src/st/rno0/e_nova_skeleton.c
    src/st/rno0/e_hammer.c
    src/st/rno0/e_gurkha.c
    src/st/rno0/e_blade.c
    src/st/rno0/e_subweapon_container.c
    src/st/rno0/e_gorgon.c
    src/st/rno0/e_medusa_head.c
    src/st/rno0/e_sealed_door.c
    src/st/rno0/gen/rooms.c
    src/st/rno0/gen/e_layout.c
    src/st/rno0/stage_data.c
    src/st/rno0/tilemaps.c
    src/st/rno0/unused_sprites.c
    src/st/rno0/gen/us/sprites.c
    src/st/rno0/e_stage_name.c
    src/st/rno0/giantbro_helpers.c
    src/st/rno0/prim_helpers.c
    src/st/rno0/e_background_clock.c
    src/st/rno0/create_entity.c
    src/st/rno0/popup.c
    src/st/rno0/giantbro_helpers_2.c
)

set(SOURCE_FILES_STAGE_RNO1
    src/pc/stages/stage_rno1.c
    src/st/rno1/header.c
    src/st/rno1/sprite_banks.c
    src/st/rno1/palette_def.c
    src/st/rno1/layers.c
    src/st/rno1/graphics_banks.c
    src/st/rno1/gen/e_laydef.c
    src/st/rno1/e_init.c
    src/st/rno1/e_room_bg.c
    src/st/rno1/e_red_door_tiles.c
    src/st/rno1/e_lock_camera.c
    src/st/rno1/e_breakable.c
    src/st/rno1/d_prize_drops.c
    src/st/rno1/e_weather.c
    src/st/rno1/e_elevator_machine.c
    src/st/rno1/e_secrets.c
    src/st/rno1/e_red_door.c
    src/st/rno1/e_boss_ante_decor.c
    src/st/rno1/e_boss_round_door.c
    src/st/rno1/e_doorway_blocker.c
    src/st/rno1/st_update.c
    src/st/rno1/collision.c
    src/st/rno1/st_common.c
    src/st/rno1/e_collect.c
    src/st/rno1/e_misc.c
    src/st/rno1/e_particles.c
    src/st/rno1/e_room_fg.c
    src/st/rno1/e_paranthropus.c
    src/st/rno1/e_stone_skull.c
    src/st/rno1/e_jack_o_bones.c
    src/st/rno1/e_nova_skeleton.c
    src/st/rno1/gen/rooms.c
    src/st/rno1/gen/e_layout.c
    src/st/rno1/stage_data.c
    src/st/rno1/tilemaps.c
    src/st/rno1/gen/us/sprites.c
    src/st/rno1/e_stage_name.c
    src/st/rno1/prim_helpers.c
    src/st/rno1/create_entity.c
    src/st/rno1/popup.c
)

set(SOURCE_FILES_STAGE_RLIB
    src/pc/stages/stage_rlib.c
    src/st/rlib/header.c
    src/st/rlib/sprite_banks.c
    src/st/rlib/palette_def.c
    src/st/rlib/layers.c
    src/st/rlib/graphics_banks.c
    src/st/rlib/gen/e_laydef.c
    src/st/rlib/e_init.c
    src/st/rlib/e_room_bg.c
    src/st/rlib/e_red_door_tiles.c
    src/st/rlib/e_lock_camera.c
    src/st/rlib/e_breakable.c
    src/st/rlib/e_library_bg.c
    src/st/rlib/e_thick_bookcase.c
    src/st/rlib/e_explosion_puff_opaque.c
    src/st/rlib/d_prize_drops.c
    src/st/rlib/e_candle_table.c
    src/st/rlib/st_update.c
    src/st/rlib/collision.c
    src/st/rlib/e_red_door.c
    src/st/rlib/st_common.c
    src/st/rlib/e_collect.c
    src/st/rlib/e_misc.c
    src/st/rlib/e_particles.c
    src/st/rlib/e_room_fg.c
    src/st/rlib/e_lion.c
    src/st/rlib/e_tinman.c
    src/st/rlib/e_scarecrow_schmoo.c
    src/st/rlib/gen/rooms.c
    src/st/rlib/gen/e_layout.c
    src/st/rlib/stage_data.c
    src/st/rlib/tilemaps.c
    src/st/rlib/gen/us/sprites.c
    src/st/rlib/e_stage_name.c
    src/st/rlib/prim_helpers.c
    src/st/rlib/create_entity.c
    src/st/rlib/popup.c
)

set(SOURCE_FILES_STAGE_RCAT
    src/pc/stages/stage_rcat.c
    src/st/rcat/header.c
    src/st/rcat/sprite_banks.c
    src/st/rcat/palette_def.c
    src/st/rcat/layers.c
    src/st/rcat/graphics_banks.c
    src/st/rcat/gen/e_laydef.c
    src/st/rcat/e_init.c
    src/st/rcat/background_block_init.c
    src/st/rcat/e_red_door_tiles.c
    src/st/rcat/e_lock_camera.c
    src/st/rcat/e_breakable.c
    src/st/rcat/e_lava.c
    src/st/rcat/e_cavern_background.c
    src/st/rcat/e_spikes.c
    src/st/rcat/unk_32118.c
    src/st/rcat/d_prize_drops.c
    src/st/rcat/e_secrets.c
    src/st/rcat/e_bat.c
    src/st/rcat/st_update.c
    src/st/rcat/collision.c
    src/st/rcat/e_red_door.c
    src/st/rcat/st_common.c
    src/st/rcat/e_collect.c
    src/st/rcat/e_misc.c
    src/st/rcat/e_particles.c
    src/st/rcat/e_room_fg.c
    src/st/rcat/e_frozen_half.c
    src/st/rcat/e_salome.c
    src/st/rcat/e_blood_skeleton.c
    src/st/rcat/e_skeleton.c
    src/st/rcat/gen/rooms.c
    src/st/rcat/gen/e_layout.c
    src/st/rcat/gfx_data.c
    src/st/rcat/tilemaps.c
    src/st/rcat/gen/us/sprites.c
    src/st/rcat/unk_31F1C.c
    src/st/rcat/e_stage_name.c
    src/st/rcat/prim_helpers.c
    src/st/rcat/e_room_bg.c
    src/st/rcat/unk_323D8.c
    src/st/rcat/create_entity.c
    src/st/rcat/popup.c
)

set(SOURCE_FILES_STAGE_RNO2
    src/pc/stages/stage_rno2.c
    src/st/rno2/header.c
    src/st/rno2/sprite_banks.c
    src/st/rno2/palette_def.c
    src/st/rno2/layers.c
    src/st/rno2/graphics_banks.c
    src/st/rno2/gen/e_laydef.c
    src/st/rno2/e_init.c
    src/st/rno2/e_room_bg.c
    src/st/rno2/e_red_door_tiles.c
    src/st/rno2/e_lock_camera.c
    src/st/rno2/e_breakable.c
    src/st/rno2/e_room_decor.c
    src/st/rno2/e_background_house.c
    src/st/rno2/e_spikes.c
    src/st/rno2/d_prize_drops.c
    src/st/rno2/e_secrets.c
    src/st/rno2/e_prisoner.c
    src/st/rno2/e_sealed_door.c
    src/st/rno2/st_update.c
    src/st/rno2/collision.c
    src/st/rno2/e_red_door.c
    src/st/rno2/st_common.c
    src/st/rno2/e_collect.c
    src/st/rno2/e_misc.c
    src/st/rno2/e_particles.c
    src/st/rno2/e_room_fg.c
    src/st/rno2/e_ctulhu.c
    src/st/rno2/e_malachi.c
    src/st/rno2/e_karasuman.c
    src/st/rno2/e_bloody_zombie.c
    src/st/rno2/e_flying_zombie.c
    src/st/rno2/e_azaghal.c
    src/st/rno2/e_ghost_dancer.c
    src/st/rno2/e_medusa_head.c
    src/st/rno2/gen/rooms.c
    src/st/rno2/gen/e_layout.c
    src/st/rno2/stage_data.c
    src/st/rno2/tilemaps.c
    src/st/rno2/gen/us/sprites.c
    src/st/rno2/e_house_shader.c
    src/st/rno2/e_stage_name.c
    src/st/rno2/prim_helpers.c
    src/st/rno2/create_entity.c
    src/st/rno2/popup.c
)

set(SOURCE_FILES_STAGE_RCHI
    src/pc/stages/stage_rchi.c
    src/st/rchi/header.c
    src/st/rchi/sprite_banks.c
    src/st/rchi/palette_def.c
    src/st/rchi/layers.c
    src/st/rchi/graphics_banks.c
    src/st/rchi/gen/e_laydef.c
    src/st/rchi/e_init.c
    src/st/rchi/e_red_door_tiles.c
    src/st/rchi/d_prize_drops.c
    src/st/rchi/gen/rooms.c
    src/st/rchi/gen/e_layout.c
    src/st/rchi/stage_data.c
    src/st/rchi/tilemaps.c
    src/st/rchi/gen/us/sprites.c
    src/st/rchi/e_room_bg.c
    src/st/rchi/e_lock_camera.c
    src/st/rchi/e_breakable.c
    src/st/rchi/e_demon_switch_wall.c
    src/st/rchi/e_breakable_wall.c
    src/st/rchi/e_bat.c
    src/st/rchi/st_update.c
    src/st/rchi/collision.c
    src/st/rchi/create_entity.c
    src/st/rchi/e_red_door.c
    src/st/rchi/st_common.c
    src/st/rchi/e_collect.c
    src/st/rchi/e_misc.c
    src/st/rchi/e_stage_name.c
    src/st/rchi/e_particles.c
    src/st/rchi/e_room_fg.c
    src/st/rchi/popup.c
    src/st/rchi/e_slogra.c
    src/st/rchi/e_gaibon.c
    src/st/rchi/e_thornweed.c
    src/st/rchi/prim_helpers.c
    src/st/rchi/bss.c
)

set(SOURCE_FILES_STAGE_RNO3
    src/pc/stages/stage_rno3.c
    src/st/rno3/header.c
    src/st/rno3/sprite_banks.c
    src/st/rno3/palette_def.c
    src/st/rno3/layers.c
    src/st/rno3/graphics_banks.c
    src/st/rno3/gen/e_laydef.c
    src/st/rno3/e_init.c
    src/st/rno3/e_room_bg.c
    src/st/rno3/e_red_door_tiles.c
    src/st/rno3/e_lock_camera.c
    src/st/rno3/e_breakable.c
    src/st/rno3/e_castle_door.c
    src/st/rno3/e_shutting_window.c
    src/st/rno3/e_background_bushes_trees.c
    src/st/rno3/d_prize_drops.c
    src/st/rno3/e_background_lightning.c
    src/st/rno3/e_sky_entities.c
    src/st/rno3/e_cavern_door.c
    src/st/rno3/e_jewel_sword_puzzle.c
    src/st/rno3/e_transparent_water.c
    src/st/rno3/d_water_data.c
    src/st/rno3/st_update.c
    src/st/rno3/collision.c
    src/st/rno3/e_red_door.c
    src/st/rno3/st_common.c
    src/st/rno3/e_collect.c
    src/st/rno3/e_misc.c
    src/st/rno3/e_particles.c
    src/st/rno3/e_room_fg.c
    src/st/rno3/e_fire_warg.c
    src/st/rno3/e_jack_o_bones.c
    src/st/rno3/e_nova_skeleton.c
    src/st/rno3/e_orobourous.c
    src/st/rno3/e_dragon_rider.c
    src/st/rno3/e_blue_venus_weed.c
    src/st/rno3/water_effects.c
    src/st/rno3/e_dodo.c
    src/st/rno3/gen/us/rooms.c
    src/st/rno3/gen/e_layout.c
    src/st/rno3/stage_data.c
    src/st/rno3/tilemaps.c
    src/st/rno3/gen/us/sprites.c
    src/st/rno3/e_stairway.c
    src/st/rno3/e_stage_name.c
    src/st/rno3/prim_helpers.c
    src/st/rno3/entrance_weights.c
    src/st/rno3/e_heartroom.c
    src/st/rno3/create_entity.c
    src/st/rno3/popup.c
)

set(SOURCE_FILES_STAGE_RCEN
    src/pc/stages/stage_rcen.c
    src/st/rcen/header.c
    src/st/rcen/sprite_banks.c
    src/st/rcen/palette_def.c
    src/st/rcen/layers.c
    src/st/rcen/graphics_banks.c
    src/st/rcen/gen/e_laydef.c
    src/st/rcen/e_init.c
    src/st/rcen/e_red_door_tiles.c
    src/st/rcen/gen/rooms.c
    src/st/rcen/gen/e_layout.c
    src/st/rcen/stage_data.c
    src/st/rcen/tilemaps.c
    src/st/rcen/gen/us/sprites.c
    src/st/rcen/e_room_bg.c
    src/st/rcen/e_lock_camera.c
    src/st/rcen/e_breakable.c
    src/st/rcen/d_prize_drops.c
    src/st/rcen/e_shaft.c
    src/st/rcen/e_shaft_death_flames.c
    src/st/rcen/cutscene.c
    src/st/rcen/unk_1F0D8.c
    src/st/rcen/e_elevator.c
    src/st/rcen/st_update.c
    src/st/rcen/collision.c
    src/st/rcen/create_entity.c
    src/st/rcen/e_red_door.c
    src/st/rcen/st_common.c
    src/st/rcen/e_collect.c
    src/st/rcen/e_misc.c
    src/st/rcen/e_stage_name.c
    src/st/rcen/e_particles.c
    src/st/rcen/e_room_fg.c
    src/st/rcen/popup.c
    src/st/rcen/prim_helpers.c
    src/st/rcen/e_shaft_meridian_rings.c
)

set(SOURCE_FILES_STAGE_RNO4
    src/pc/stages/stage_rno4.c
    src/st/rno4/create_entity.c
    src/st/rno4/layers.c
    src/st/rno4/e_red_door.c
    src/st/rno4/e_red_door_tiles.c
    src/st/rno4/e_room_fg.c
    src/st/rno4/st_common.c
    src/st/rno4/st_update.c
    src/st/rno4/e_misc.c
    src/st/rno4/gen/rooms.c
    src/st/rno4/stage_data.c
    src/st/rno4/tilemaps.c
)

set(SOURCE_FILES_STAGE_RTOP
    src/pc/stages/stage_rtop.c
    src/st/rtop/header.c
    src/st/rtop/sprite_banks.c
    src/st/rtop/palette_def.c
    src/st/rtop/layers.c
    src/st/rtop/graphics_banks.c
    src/st/rtop/gen/e_laydef.c
    src/st/rtop/e_init.c
    src/st/rtop/e_room_bg.c
    src/st/rtop/e_red_door_tiles.c
    src/st/rtop/e_lock_camera.c
    src/st/rtop/e_breakable.c
    src/st/rtop/d_prize_drops.c
    src/st/rtop/e_secret_stairs.c
    src/st/rtop/e_breakable_wall.c
    src/st/rtop/e_elevator.c
    src/st/rtop/e_lion_lamp.c
    src/st/rtop/st_update.c
    src/st/rtop/collision.c
    src/st/rtop/e_red_door.c
    src/st/rtop/st_common.c
    src/st/rtop/e_collect.c
    src/st/rtop/e_misc.c
    src/st/rtop/e_particles.c
    src/st/rtop/e_room_fg.c
    src/st/rtop/e_tombstone.c
    src/st/rtop/e_yorick.c
    src/st/rtop/e_skull_lord.c
    src/st/rtop/gen/rooms.c
    src/st/rtop/e_clouds.c
    src/st/rtop/e_clock_tower.c
    src/st/rtop/gen/e_layout.c
    src/st/rtop/stage_data.c
    src/st/rtop/tilemaps.c
    src/st/rtop/gen/sprites.c
    src/st/rtop/e_richter_fight_trigger.c
    src/st/rtop/e_warp_trigger.c
    src/st/rtop/e_stage_name.c
    src/st/rtop/prim_helpers.c
    src/st/rtop/create_entity.c
    src/st/rtop/popup.c
)

set(SOURCE_FILES_STAGE_RNZ0
    src/pc/stages/stage_rnz0.c
    src/st/rnz0/header.c
    src/st/rnz0/sprite_banks.c
    src/st/rnz0/palette_def.c
    src/st/rnz0/layers.c
    src/st/rnz0/gfx_banks.c
    src/st/rnz0/gen/e_laydef.c
    src/st/rnz0/e_init.c
    src/st/rnz0/e_room_bg.c
    src/st/rnz0/e_red_door_tiles.c
    src/st/rnz0/e_lock_camera.c
    src/st/rnz0/e_breakable.c
    src/st/rnz0/e_left_secret_room_wall.c
    src/st/rnz0/d_prize_drops.c
    src/st/rnz0/e_globe_table.c
    src/st/rnz0/e_life_max_tank.c
    src/st/rnz0/e_blue_flame_table.c
    src/st/rnz0/e_cannon.c
    src/st/rnz0/st_update.c
    src/st/rnz0/collision.c
    src/st/rnz0/e_red_door.c
    src/st/rnz0/st_common.c
    src/st/rnz0/e_collect.c
    src/st/rnz0/e_misc.c
    src/st/rnz0/e_particles.c
    src/st/rnz0/e_room_fg.c
    src/st/rnz0/e_floor_trap.c
    src/st/rnz0/e_ctulhu.c
    src/st/rnz0/e_fire_demon.c
    src/st/rnz0/e_lesser_demon.c
    src/st/rnz0/e_sealed_door.c
    src/st/rnz0/e_bitterfly.c
    src/st/rnz0/e_imp.c
    src/st/rnz0/e_gremlin.c
    src/st/rnz0/e_salem_witch.c
    src/st/rnz0/gen/rooms.c
    src/st/rnz0/gen/e_layout.c
    src/st/rnz0/stage_data.c
    src/st/rnz0/gen/us/sprites.c
    src/st/rnz0/e_red_eye_bust.c
    src/st/rnz0/e_stage_name.c
    src/st/rnz0/prim_helpers.c
    src/st/rnz0/e_purple_brick.c
    src/st/rnz0/create_entity.c
    src/st/rnz0/popup.c
)

set(SOURCE_FILES_STAGE_RNZ1
    src/pc/stages/stage_rnz1.c
    src/st/rnz1/header.c
    src/st/rnz1/sprite_banks.c
    src/st/rnz1/palette_def.c
    src/st/rnz1/layers.c
    src/st/rnz1/graphics_banks.c
    src/st/rnz1/gen/e_laydef.c
    src/st/rnz1/e_init.c
    src/st/rnz1/background_block_init.c
    src/st/rnz1/e_red_door_tiles.c
    src/st/rnz1/e_lock_camera.c
    src/st/rnz1/e_breakable.c
    src/st/rnz1/d_prize_drops.c
    src/st/rnz1/e_explosion_puff_opaque.c
    src/st/rnz1/e_bg_gears.c
    src/st/rnz1/e_gear_horizontal.c
    src/st/rnz1/e_gear_vertical.c
    src/st/rnz1/e_gear_small.c
    src/st/rnz1/e_gear_puzzle.c
    src/st/rnz1/e_secrets.c
    src/st/rnz1/e_water_fg.c
    src/st/rnz1/d_water_data.c
    src/st/rnz1/e_spikes.c
    src/st/rnz1/e_darkwing_bat.c
    src/st/rnz1/e_dw_batwings.c
    src/st/rnz1/bossfight.c
    src/st/rnz1/st_update.c
    src/st/rnz1/collision.c
    src/st/rnz1/e_red_door.c
    src/st/rnz1/st_common.c
    src/st/rnz1/e_collect.c
    src/st/rnz1/e_misc.c
    src/st/rnz1/e_particles.c
    src/st/rnz1/e_room_fg.c
    src/st/rnz1/water_effects.c
    src/st/rnz1/e_cloaked_knight.c
    src/st/rnz1/e_medusa_head.c
    src/st/rnz1/e_valhalla_knight.c
    src/st/rnz1/e_bomb_knight.c
    src/st/rnz1/e_crusher.c
    src/st/rnz1/gen/rooms.c
    src/st/rnz1/e_life_up.c
    src/st/rnz1/gen/e_layout.c
    src/st/rnz1/stage_data.c
    src/st/rnz1/tilemaps.c
    src/st/rnz1/gen/us/sprites.c
    src/st/rnz1/e_clouds.c
    src/st/rnz1/e_stage_name.c
    src/st/rnz1/prim_helpers.c
    src/st/rnz1/e_room_bg.c
    src/st/rnz1/e_gear_large.c
    src/st/rnz1/create_entity.c
    src/st/rnz1/popup.c
)

set(SOURCE_FILES_STAGE_RWRP
    src/pc/stages/stage_rwrp.c
    src/st/rwrp/header.c
    src/st/rwrp/sprite_banks.c
    src/st/rwrp/palette_def.c
    src/st/rwrp/layers.c
    src/st/rwrp/graphics_banks.c
    src/st/rwrp/gen/e_laydef.c
    src/st/rwrp/e_init.c
    src/st/rwrp/gen/rooms.c
    src/st/rwrp/gen/e_layout.c
    src/st/rwrp/gen/us/sprites.c
    src/st/rwrp/stage_data.c
    src/st/rwrp/background_block_init.c
    src/st/rwrp/e_room_bg.c
    src/st/rwrp/e_lock_camera.c
    src/st/rwrp/e_breakable.c
    src/st/rwrp/d_prize_drops.c
    src/st/rwrp/warp.c
    src/st/rwrp/st_update.c
    src/st/rwrp/collision.c
    src/st/rwrp/create_entity.c
    src/st/rwrp/e_red_door_tiles.c
    src/st/rwrp/e_red_door.c
    src/st/rwrp/st_common.c
    src/st/rwrp/e_collect.c
    src/st/rwrp/e_misc.c
    src/st/rwrp/e_stage_name.c
    src/st/rwrp/e_particles.c
    src/st/rwrp/e_room_fg.c
    src/st/rwrp/popup.c
    src/st/rwrp/prim_helpers.c
    src/st/rwrp/bss.c
)

SET(SOURCE_FILES_BOSS_RBO7
    src/pc/bosses/boss_rbo7.c
    src/boss/rbo7/header.c
    src/boss/rbo7/sprite_banks.c
    src/boss/rbo7/palette_def.c
    src/boss/rbo7/layers.c
    src/boss/rbo7/graphics_banks.c
    src/boss/rbo7/gen/e_laydef.c
    src/boss/rbo7/e_init.c
    src/boss/rbo7/background_block_init.c
    src/boss/rbo7/e_red_door_tiles.c
    src/boss/rbo7/e_lock_camera.c
    src/boss/rbo7/e_breakable.c
    src/boss/rbo7/d_prize_drops.c
    src/boss/rbo7/e_boss_doors.c
    src/boss/rbo7/bossfight.c
    src/boss/rbo7/e_akmodan_ii.c
    src/boss/rbo7/st_update.c
    src/boss/rbo7/collision.c
    src/boss/rbo7/e_red_door.c
    src/boss/rbo7/st_common.c
    src/boss/rbo7/e_collect.c
    src/boss/rbo7/e_misc.c
    src/boss/rbo7/e_particles.c
    src/boss/rbo7/e_room_fg.c
    src/boss/rbo7/gen/rooms.c
    src/boss/rbo7/e_life_up.c
    src/boss/rbo7/gen/e_layout.c
    src/boss/rbo7/stage_data.c
    src/boss/rbo7/pal_unk.c
    src/boss/rbo7/tilemaps.c
    src/boss/rbo7/gen/us/sprites.c
    src/boss/rbo7/e_stage_name.c
    src/boss/rbo7/prim_helpers.c
    src/boss/rbo7/e_room_bg.c
    src/boss/rbo7/create_entity.c
    src/boss/rbo7/popup.c
    src/boss/rbo7/bss.c
)

SET(SOURCE_FILES_BOSS_BO6
    src/pc/bosses/boss_bo6.c
    src/boss/bo6/collision.c
    src/boss/bo6/create_entity.c
    src/boss/bo6/cutscene.c
    src/boss/bo6/d_prize_drops.c
    src/boss/bo6/e_breakable.c
    src/boss/bo6/e_collect.c
    src/boss/bo6/e_cutscene_actors.c
    src/boss/bo6/e_init.c
    src/boss/bo6/e_life_up.c
    src/boss/bo6/e_lock_camera.c
    src/boss/bo6/e_misc.c
    src/boss/bo6/e_particles.c
    src/boss/bo6/e_red_door.c
    src/boss/bo6/e_red_door_tiles.c
    src/boss/bo6/e_room_bg.c
    src/boss/bo6/e_room_fg.c
    src/boss/bo6/e_stage_name.c
    src/boss/bo6/graphics_banks.c
    src/boss/bo6/header.c
    src/boss/bo6/layers.c
    src/boss/bo6/palette_def.c
    src/boss/bo6/popup.c
    src/boss/bo6/prim_helpers.c
    src/boss/bo6/richter.c
    src/boss/bo6/sprite_banks.c
    src/boss/bo6/st_common.c
    src/boss/bo6/st_update.c
    src/boss/bo6/unused.c
    src/boss/bo6/us_39144.c
    src/boss/bo6/us_3E79C.c
    src/boss/bo6/gen/e_laydef.c
    src/boss/bo6/gen/e_layout.c
    src/boss/bo6/gen/rooms.c
)

SET(SOURCE_FILES_BOSS_RBO6
    src/pc/bosses/boss_rbo6.c
    src/boss/rbo6/collision.c
    src/boss/rbo6/create_entity.c
    src/boss/rbo6/d_prize_drops.c
    src/boss/rbo6/e_breakable.c
    src/boss/rbo6/e_collect.c
    src/boss/rbo6/e_cutscene_dialogue.c
    src/boss/rbo6/e_cutscene_faders.c
    src/boss/rbo6/e_dracula.c
    src/boss/rbo6/e_init.c
    src/boss/rbo6/e_lock_camera.c
    src/boss/rbo6/e_misc.c
    src/boss/rbo6/e_particles.c
    src/boss/rbo6/e_red_door.c
    src/boss/rbo6/e_red_door_tiles.c
    src/boss/rbo6/e_room_bg.c
    src/boss/rbo6/e_room_fg.c
    src/boss/rbo6/e_stage_name.c
    src/boss/rbo6/graphics_banks.c
    src/boss/rbo6/header.c
    src/boss/rbo6/layers.c
    src/boss/rbo6/palette_def.c
    src/boss/rbo6/popup.c
    src/boss/rbo6/prim_helpers.c
    src/boss/rbo6/sprite_banks.c
    src/boss/rbo6/st_common.c
    src/boss/rbo6/st_update.c
    src/boss/rbo6/stage_data.c
    src/boss/rbo6/tilemaps.c
    src/boss/rbo6/unk_1D7C4.c
    src/boss/rbo6/unk_24530.c
    src/boss/rbo6/unk_30D68.c
    src/boss/rbo6/unk_31738.c
    src/boss/rbo6/gen/e_laydef.c
    src/boss/rbo6/gen/e_layout.c
    src/boss/rbo6/gen/rooms.c
    src/boss/rbo6/gen/us/sprites.c
)

SET(SOURCE_FILES_BOSS_BO0
    src/pc/bosses/boss_bo0.c
    src/boss/bo0/3053C.c
    src/boss/bo0/34DA4.c
    src/boss/bo0/372E8.c
    src/boss/bo0/bss.c
    src/boss/bo0/create_entity.c
    src/boss/bo0/d_prize_drops.c
    src/boss/bo0/e_background_house.c
    src/boss/bo0/e_bloody_zombie.c
    src/boss/bo0/e_boss_doors.c
    src/boss/bo0/e_breakable.c
    src/boss/bo0/e_chair.c
    src/boss/bo0/e_collect.c
    src/boss/bo0/e_init.c
    src/boss/bo0/e_laser_attack.c
    src/boss/bo0/e_life_up.c
    src/boss/bo0/e_lock_camera.c
    src/boss/bo0/e_marionette.c
    src/boss/bo0/e_misc.c
    src/boss/bo0/e_normal_form.c
    src/boss/bo0/e_olrox_drool.c
    src/boss/bo0/e_owl_knight.c
    src/boss/bo0/e_particles.c
    src/boss/bo0/e_prisoner.c
    src/boss/bo0/e_red_door.c
    src/boss/bo0/e_red_door_tiles.c
    src/boss/bo0/e_room_bg.c
    src/boss/bo0/e_room_fg.c
    src/boss/bo0/e_sealed_door.c
    src/boss/bo0/e_secrets.c
    src/boss/bo0/e_skelerang.c
    src/boss/bo0/e_spikes.c
    src/boss/bo0/e_stage_name.c
    src/boss/bo0/e_true_form.c
    src/boss/bo0/graphics_banks.c
    src/boss/bo0/header.c
    src/boss/bo0/layers.c
    src/boss/bo0/no2_bg.c
    src/boss/bo0/palette_def.c
    src/boss/bo0/palettes.c
    src/boss/bo0/popup.c
    src/boss/bo0/prim_helpers.c
    src/boss/bo0/sprite_banks.c
    src/boss/bo0/st_collision.c
    src/boss/bo0/st_common.c
    src/boss/bo0/st_update.c
    src/boss/bo0/stage_data.c
    src/boss/bo0/tilemaps.c
    src/boss/bo0/gen/e_laydef.c
    src/boss/bo0/gen/e_layout.c
    src/boss/bo0/gen/rooms.c
    src/boss/bo0/gen/us/sprites.c
)

SET(SOURCE_FILES_BOSS_RBO8
    src/pc/bosses/boss_rbo8.c
    src/boss/rbo8/header.c
    src/boss/rbo8/sprite_banks.c
    src/boss/rbo8/layers.c
    src/boss/rbo8/graphics_banks.c
    src/boss/rbo8/gen/e_laydef.c
    src/boss/rbo8/e_init.c
    src/boss/rbo8/d_g__red_door_tiles.c
    src/boss/rbo8/e_lock_camera.c
    src/boss/rbo8/d_prize_drops.c
    src/boss/rbo8/d_pc_data.c
    src/boss/rbo8/unk_151E8.c
    src/boss/rbo8/unk_1546C.c
    src/boss/rbo8/e_blade.c
    src/boss/rbo8/unk_15868.c
    src/boss/rbo8/st_update.c
    src/boss/rbo8/collision.c
    src/boss/rbo8/create_entity.c
    src/boss/rbo8/e_red_door.c
    src/boss/rbo8/st_common.c
    src/boss/rbo8/e_collect.c
    src/boss/rbo8/e_misc.c
    src/boss/rbo8/e_stage_name.c
    src/boss/rbo8/e_particles.c
    src/boss/rbo8/e_room_fg.c
    src/boss/rbo8/gen/rooms.c
    src/boss/rbo8/e_life_up.c
    src/boss/rbo8/gen/e_layout.c
    src/boss/rbo8/stage_data.c
    src/boss/rbo8/tilemaps.c
    src/boss/rbo8/gen/us/sprites.c
    src/boss/rbo8/popup.c
    src/boss/rbo8/giantbro_helpers.c
    src/boss/rbo8/prim_helpers.c
    src/boss/rbo8/bss.c
)

SET(SOURCE_FILES_BOSS_BO2
    src/pc/bosses/boss_bo2.c
    src/boss/bo2/background_block_init.c
    src/boss/bo2/collision.c
    src/boss/bo2/create_entity.c
    src/boss/bo2/d_prize_drops.c
    src/boss/bo2/e_boss_doors.c
    src/boss/bo2/e_boss_torch.c
    src/boss/bo2/e_collect.c
    src/boss/bo2/e_cutscene_dialogue.c
    src/boss/bo2/e_init.c
    src/boss/bo2/e_life_up.c
    src/boss/bo2/e_lock_camera.c
    src/boss/bo2/e_misc.c
    src/boss/bo2/e_particles.c
    src/boss/bo2/e_red_door.c
    src/boss/bo2/e_red_door_tiles.c
    src/boss/bo2/e_room_fg.c
    src/boss/bo2/e_stage_name.c
    src/boss/bo2/gen/e_laydef.c
    src/boss/bo2/gen/e_layout.c
    src/boss/bo2/gen/rooms.c
    src/boss/bo2/gen/us/sprites.c
    src/boss/bo2/graphics_banks.c
    src/boss/bo2/header.c
    src/boss/bo2/layers.c
    src/boss/bo2/palette_def.c
    src/boss/bo2/popup.c
    src/boss/bo2/prim_helpers.c
    src/boss/bo2/sprite_banks.c
    src/boss/bo2/st_common.c
    src/boss/bo2/st_update.c
    src/boss/bo2/stage_data.c
    src/boss/bo2/unk_22258.c
    src/boss/bo2/unk_224DC.c
    src/boss/bo2/unk_26EF8.c
    src/boss/bo2/unk_337D0.c
)

SET(SOURCE_FILES_BOSS_RBO2
    src/pc/bosses/boss_rbo2.c
    src/boss/rbo2/header.c
    src/boss/rbo2/sprite_banks.c
    src/boss/rbo2/palette_def.c
    src/boss/rbo2/layers.c
    src/boss/rbo2/graphics_banks.c
    src/boss/rbo2/gen/e_laydef.c
    src/boss/rbo2/e_init.c
    src/boss/rbo2/background_block_init.c
    src/boss/rbo2/e_red_door_tiles.c
    src/boss/rbo2/e_lock_camera.c
    src/boss/rbo2/d_prize_drops.c
    src/boss/rbo2/unk_1B000.c
    src/boss/rbo2/unk_1B284.c
    src/boss/rbo2/e_debug_cerberus_gate.c
    src/boss/rbo2/e_cutscene_dialogue.c
    src/boss/rbo2/e_cutscene_actors.c
    src/boss/rbo2/func_us_801a17c8.c
    src/boss/rbo2/st_update.c
    src/boss/rbo2/collision.c
    src/boss/rbo2/create_entity.c
    src/boss/rbo2/e_red_door.c
    src/boss/rbo2/st_common.c
    src/boss/rbo2/e_collect.c
    src/boss/rbo2/e_misc.c
    src/boss/rbo2/e_stage_name.c
    src/boss/rbo2/e_particles.c
    src/boss/rbo2/e_room_fg.c
    src/boss/rbo2/gen/rooms.c
    src/boss/rbo2/e_life_up.c
    src/boss/rbo2/gen/e_layout.c
    src/boss/rbo2/stage_data.c
    src/boss/rbo2/tilemaps.c
    src/boss/rbo2/gen/us/sprites.c
    src/boss/rbo2/popup.c
    src/boss/rbo2/prim_helpers.c
)

SET(SOURCE_FILES_BOSS_RBO1
    src/pc/bosses/boss_rbo1.c
    src/boss/rbo1/header.c
    src/boss/rbo1/sprite_banks.c
    src/boss/rbo1/palette_def.c
    src/boss/rbo1/layers.c
    src/boss/rbo1/graphics_banks.c
    src/boss/rbo1/gen/e_laydef.c
    src/boss/rbo1/e_init.c
    src/boss/rbo1/background_block_init.c
    src/boss/rbo1/e_red_door_tiles.c
    src/boss/rbo1/e_lock_camera.c
    src/boss/rbo1/e_boss_room.c
    src/boss/rbo1/d_prize_drops.c
    src/boss/rbo1/st_update.c
    src/boss/rbo1/collision.c
    src/boss/rbo1/e_red_door.c
    src/boss/rbo1/st_common.c
    src/boss/rbo1/e_collect.c
    src/boss/rbo1/e_misc.c
    src/boss/rbo1/e_particles.c
    src/boss/rbo1/e_room_fg.c
    src/boss/rbo1/gen/rooms.c
    src/boss/rbo1/e_life_up.c
    src/boss/rbo1/gen/e_layout.c
    src/boss/rbo1/stage_data.c
    src/boss/rbo1/pal_unk.c
    src/boss/rbo1/tilemaps.c
    src/boss/rbo1/gen/us/sprites.c
    src/boss/rbo1/unk_11FD8.c
    src/boss/rbo1/unk_12274.c
    src/boss/rbo1/create_entity.c
    src/boss/rbo1/e_stage_name.c
    src/boss/rbo1/popup.c
    src/boss/rbo1/prim_helpers.c
    src/boss/rbo1/bss.c
)

SET(SOURCE_FILES_BOSS_BO7
    src/pc/bosses/boss_bo7.c
    src/boss/bo7/background_block_init.c
    src/boss/bo7/collision.c
    src/boss/bo7/create_entity.c
    src/boss/bo7/d_prize_drops.c
    src/boss/bo7/e_breakable_wall.c
    src/boss/bo7/e_collect.c
    src/boss/bo7/e_debug_cerberus_gate.c
    src/boss/bo7/e_demon_switch_wall.c
    src/boss/bo7/e_init.c
    src/boss/bo7/e_life_up.c
    src/boss/bo7/e_lock_camera.c
    src/boss/bo7/e_misc.c
    src/boss/bo7/e_particles.c
    src/boss/bo7/e_red_door.c
    src/boss/bo7/e_red_door_tiles.c
    src/boss/bo7/e_room_fg.c
    src/boss/bo7/e_stage_name.c
    src/boss/bo7/gen/e_laydef.c
    src/boss/bo7/gen/e_layout.c
    src/boss/bo7/gen/rooms.c
    src/boss/bo7/gen/us/sprites.c
    src/boss/bo7/graphics_banks.c
    src/boss/bo7/header.c
    src/boss/bo7/layers.c
    src/boss/bo7/palette_def.c
    src/boss/bo7/popup.c
    src/boss/bo7/prim_helpers.c
    src/boss/bo7/sprite_banks.c
    src/boss/bo7/st_common.c
    src/boss/bo7/st_update.c
    src/boss/bo7/stage_data.c
    src/boss/bo7/unk_12FBC.c
    src/boss/bo7/unk_13258.c
    src/boss/bo7/unk_14CE0.c
)

set(SOURCE_FILES_BOSS_RBO5
    src/pc/bosses/boss_rbo5.c
    src/boss/rbo5/header.c
    src/boss/rbo5/sprite_banks.c
    src/boss/rbo5/layers.c
    src/boss/rbo5/graphics_banks.c
    src/boss/rbo5/gen/e_laydef.c
    src/boss/rbo5/e_init.c
    src/boss/rbo5/background_block_init.c
    src/boss/rbo5/e_red_door_tiles.c
    src/boss/rbo5/e_lock_camera.c
    src/boss/rbo5/e_breakable.c
    src/boss/rbo5/doors.c
    src/boss/rbo5/unk_354F0.c
    src/boss/rbo5/d_prize_drops.c
    src/boss/rbo5/st_update.c
    src/boss/rbo5/collision.c
    src/boss/rbo5/e_red_door.c
    src/boss/rbo5/st_common.c
    src/boss/rbo5/e_collect.c
    src/boss/rbo5/e_misc.c
    src/boss/rbo5/e_particles.c
    src/boss/rbo5/e_room_fg.c
    src/boss/rbo5/gen/rooms.c
    src/boss/rbo5/doppleganger.c
    src/boss/rbo5/dop_collision.c
    src/boss/rbo5/dop_anim.c
    src/boss/rbo5/unk_44C48.c
    src/boss/rbo5/unk_45C00.c
    src/boss/rbo5/unk_4907C.c
    src/boss/rbo5/doppleganger_anim.c
    src/boss/rbo5/dop_weapon_anim.c
    src/boss/rbo5/e_life_up.c
    src/boss/rbo5/gen/e_layout.c
    src/boss/rbo5/spritesheets.c
    src/boss/rbo5/stage_data.c
    src/boss/rbo5/spriteparts.c
    src/boss/rbo5/e_stage_name.c
    src/boss/rbo5/prim_helpers.c
    src/boss/rbo5/e_room_bg.c
    src/boss/rbo5/create_entity.c
    src/boss/rbo5/popup.c
    src/boss/rbo5/unk_45650.c
    src/boss/rbo5/unk_502F8.c
    src/boss/rbo5/bss.c
)

SET(SOURCE_FILES_BOSS_RBO4
    src/pc/bosses/boss_rbo4.c
    src/boss/rbo4/header.c
    src/boss/rbo4/sprite_banks.c
    src/boss/rbo4/palette_def.c
    src/boss/rbo4/layers.c
    src/boss/rbo4/graphics_banks.c
    src/boss/rbo4/gen/e_laydef.c
    src/boss/rbo4/e_init.c
    src/boss/rbo4/background_block_init.c
    src/boss/rbo4/e_red_door_tiles.c
    src/boss/rbo4/e_lock_camera.c
    src/boss/rbo4/e_breakable.c
    src/boss/rbo4/d_prize_drops.c
    src/boss/rbo4/e_thecreature.c
    src/boss/rbo4/e_thecreature_flames.c
    src/boss/rbo4/st_update.c
    src/boss/rbo4/collision.c
    src/boss/rbo4/e_red_door.c
    src/boss/rbo4/st_common.c
    src/boss/rbo4/e_collect.c
    src/boss/rbo4/e_misc.c
    src/boss/rbo4/e_particles.c
    src/boss/rbo4/e_room_fg.c
    src/boss/rbo4/gen/rooms.c
    src/boss/rbo4/e_life_up.c
    src/boss/rbo4/gen/e_layout.c
    src/boss/rbo4/stage_data.c
    src/boss/rbo4/pal_unk.c
    src/boss/rbo4/tilemaps.c
    src/boss/rbo4/gen/us/sprites.c
    src/boss/rbo4/bossfight.c
    src/boss/rbo4/e_boss_doors.c
    src/boss/rbo4/e_stage_name.c
    src/boss/rbo4/prim_helpers.c
    src/boss/rbo4/e_room_bg.c
    src/boss/rbo4/unk_18A18.c
    src/boss/rbo4/create_entity.c
    src/boss/rbo4/popup.c
    src/boss/rbo4/bss.c
)

SET(SOURCE_FILES_BOSS_RBO3
    src/pc/bosses/boss_rbo3.c
    src/boss/rbo3/header.c
    src/boss/rbo3/sprite_banks.c
    src/boss/rbo3/palette_def.c
    src/boss/rbo3/layers.c
    src/boss/rbo3/graphics_banks.c
    src/boss/rbo3/gen/e_laydef.c
    src/boss/rbo3/e_init.c
    src/boss/rbo3/background_block_init.c
    src/boss/rbo3/e_red_door_tiles.c
    src/boss/rbo3/e_lock_camera.c
    src/boss/rbo3/e_breakable.c
    src/boss/rbo3/d_prize_drops.c
    src/boss/rbo3/e_medusa.c
    src/boss/rbo3/unk_12998.c
    src/boss/rbo3/unk_12B38.c
    src/boss/rbo3/12D64.c
    src/boss/rbo3/st_update.c
    src/boss/rbo3/collision.c
    src/boss/rbo3/e_red_door.c
    src/boss/rbo3/st_common.c
    src/boss/rbo3/e_collect.c
    src/boss/rbo3/e_misc.c
    src/boss/rbo3/e_particles.c
    src/boss/rbo3/e_room_fg.c
    src/boss/rbo3/gen/rooms.c
    src/boss/rbo3/e_life_up.c
    src/boss/rbo3/e_clouds.c
    src/boss/rbo3/gen/e_layout.c
    src/boss/rbo3/stage_data.c
    src/boss/rbo3/pal_unk.c
    src/boss/rbo3/tilemaps.c
    src/boss/rbo3/gen/us/sprites.c
    src/boss/rbo3/unk_11438.c
    src/boss/rbo3/e_stage_name.c
    src/boss/rbo3/prim_helpers.c
    src/boss/rbo3/e_room_bg.c
    src/boss/rbo3/unk_13050.c
    src/boss/rbo3/create_entity.c
    src/boss/rbo3/popup.c
    src/boss/rbo3/bss.c
)

SET(SOURCE_FILES_BOSS_RBO0
    src/pc/bosses/boss_rbo0.c
    src/boss/rbo0/header.c
    src/boss/rbo0/sprite_banks.c
    src/boss/rbo0/palette_def.c
    src/boss/rbo0/layers.c
    src/boss/rbo0/graphics_banks.c
    src/boss/rbo0/gen/e_laydef.c
    src/boss/rbo0/e_init.c
    src/boss/rbo0/background_block_init.c
    src/boss/rbo0/e_red_door_tiles.c
    src/boss/rbo0/e_lock_camera.c
    src/boss/rbo0/e_breakable.c
    src/boss/rbo0/d_prize_drops.c
    src/boss/rbo0/e_boss_torch.c
    src/boss/rbo0/e_boss_doors.c
    src/boss/rbo0/e_boss.c
    src/boss/rbo0/e_fake_ralph.c
    src/boss/rbo0/e_fake_grant.c
    src/boss/rbo0/e_fake_sypha.c
    src/boss/rbo0/unk_19B94.c
    src/boss/rbo0/st_update.c
    src/boss/rbo0/st_collision.c
    src/boss/rbo0/e_red_door.c
    src/boss/rbo0/st_common.c
    src/boss/rbo0/e_collect.c
    src/boss/rbo0/e_misc.c
    src/boss/rbo0/e_particles.c
    src/boss/rbo0/e_room_fg.c
    src/boss/rbo0/gen/rooms.c
    src/boss/rbo0/e_life_up.c
    src/boss/rbo0/e_coffin.c
    src/boss/rbo0/gen/e_layout.c
    src/boss/rbo0/stage_data.c
    src/boss/rbo0/palettes.c
    src/boss/rbo0/tiledef.c
    src/boss/rbo0/gen/us/sprites.c
    src/boss/rbo0/e_stage_name.c
    src/boss/rbo0/prim_helpers.c
    src/boss/rbo0/e_room_bg.c
    src/boss/rbo0/create_entity.c
    src/boss/rbo0/popup.c
    src/boss/rbo0/bss.c
)

set(SOURCE_FILES_STAGE_MAD
    src/pc/stages/stage_mad.c
    src/st/mad/header.c
    src/st/mad/sprite_banks.c
    src/st/mad/palette_def.c
    src/st/mad/layers.c
    src/st/mad/graphics_banks.c
    src/st/mad/gen/e_laydef.c
    src/st/mad/e_init.c
    src/st/mad/background_block_init.c
    src/st/mad/e_red_door_tiles.c
    src/st/mad/e_lock_camera.c
    src/st/mad/e_breakable.c
    src/st/mad/st_update.c
    src/st/mad/collision.c
    src/st/mad/e_red_door.c
    src/st/mad/st_common.c
    src/st/mad/e_collect.c
    src/st/mad/d_prize_drops.c
    src/st/mad/e_misc.c
    src/st/mad/e_particles.c
    src/st/mad/e_room_fg.c
    src/st/mad/gen/rooms.c
    src/st/mad/gen/e_layout.c
    src/st/mad/stage_data.c
    src/st/mad/pal.c
    src/st/mad/tilemaps.c
    src/st/mad/tiledef_1.c
    src/st/mad/gen/sprites.c
    src/st/mad/D8C8.c
    src/st/mad/e_room_bg.c
    src/st/mad/create_entity.c
    src/st/mad/e_stage_name.c
    src/st/mad/popup.c
    src/st/mad/prim_helpers.c
    src/st/mad/bss.c
)

set(SOURCE_FILES_STAGE_NO3
    src/pc/stages/stage_no3.c
    src/st/no3/header.c
    src/st/no3/gen/e_laydef.c
    src/st/no3/e_init.c
    src/st/no3/gen/rooms.c
    src/st/no3/gen/e_layout.c
    src/st/no3/gen/sprites.c
    src/st/no3/graphics_banks.c
    src/st/no3/layers.c
    src/st/no3/palette_def.c
    src/st/no3/sprite_banks.c
    src/st/no3/tilemaps.c
    src/st/no3/stage_data.c
    src/st/no3/background_block_init.c
    src/st/no3/e_room_bg.c
    src/st/no3/e_lock_camera.c
    src/st/no3/e_breakable.c
    src/st/no3/d_prize_drops.c
    src/st/no3/cutscene.c
    src/st/no3/st_update.c
    src/st/no3/collision.c
    src/st/no3/create_entity.c
    src/st/no3/e_red_door_tiles.c
    src/st/no3/e_red_door.c
    src/st/no3/st_common.c
    src/st/no3/e_collect.c
    src/st/no3/e_misc.c
    src/st/no3/e_stage_name.c
    src/st/no3/e_particles.c
    src/st/no3/e_room_fg.c
    src/st/no3/popup.c
    src/st/no3/prim_helpers.c
    src/st/no3/d_water_data.c
    src/st/no3/e_background_bushes_trees.c
    src/st/no3/e_background_lightning.c
    src/st/no3/e_bat.c
    src/st/no3/e_bone_scimitar.c
    src/st/no3/e_castle_door.c
    src/st/no3/e_cavern_door.c
    src/st/no3/e_death.c
    src/st/no3/e_death_sky_swirl.c
    src/st/no3/e_explosion_puff_opaque.c
    src/st/no3/e_fire_warg.c
    src/st/no3/e_heartroom.c
    src/st/no3/e_jewel_sword_puzzle.c
    src/st/no3/e_merman.c
    src/st/no3/e_merman2.c
    src/st/no3/entrance_weights.c
    src/st/no3/e_outdoor_ents.c
    src/st/no3/e_shutting_window.c
    src/st/no3/e_sky_entities.c
    src/st/no3/e_stairway.c
    src/st/no3/e_tilemap_shuffler.c
    src/st/no3/e_transparent_water.c
    src/st/no3/e_trapdoor.c
    src/st/no3/e_unkId16.c
    src/st/no3/e_warg.c
    src/st/no3/e_zombie.c
    src/st/no3/water_effects.c
)

set(SOURCE_FILES_CORE
    ${SOURCE_FILES_DRA}
    ${SOURCE_FILES_RIC}
)
if(USE_PLAYER_MARIA)
    if(NOT EXISTS "${MARIA_BIN}")
        message(FATAL_ERROR "USE_PLAYER_MARIA needs ${MARIA_BIN}")
    endif()
    add_compile_definitions(USE_PLAYER_MARIA)
    list(APPEND SOURCE_FILES_CORE
        ${SOURCE_FILES_MARIA}
        src/pc/pl_maria.c)
    # her gen/*.h headers, made from maria.bin in the build dir (not in git)
    set(MARIA_GEN_DIR "${CMAKE_BINARY_DIR}/maria")
    add_custom_command(
        OUTPUT "${MARIA_GEN_DIR}/gen/maria.h"
        COMMAND sh tools/maria_gen.sh "${SOTN_ASSETS_HOST_BIN}" "${MARIA_GEN_DIR}"
        DEPENDS "${SOTN_ASSETS_HOST_BIN}" "${MARIA_BIN}"
                tools/maria_gen.sh config/assets.pspeu.yaml
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VERBATIM
        COMMENT "Maria headers from the PSP disc")
    set_source_files_properties(${SOURCE_FILES_MARIA} PROPERTIES
        INCLUDE_DIRECTORIES "${MARIA_GEN_DIR}"
        OBJECT_DEPENDS "${MARIA_GEN_DIR}/gen/maria.h")
    # her voices: tools/maria-sfx packs sd_mar2.spk (PSP disc) into the
    # layout of Richter's bank SD_RIH.VH/VB; pl_maria.c embeds the result
    set(MARIA_SPK "${CMAKE_SOURCE_DIR}/disks/pspeu/PSP_GAME/USRDIR/res/ps/snd_e/SE/sd_mar2.spk")
    if(NOT EXISTS "${MARIA_SPK}")
        message(FATAL_ERROR "USE_PLAYER_MARIA needs ${MARIA_SPK}")
    endif()
    set(MARIA_SFX_DIR "${MARIA_GEN_DIR}/sfx")
    add_custom_command(
        OUTPUT "${MARIA_SFX_DIR}/maria_vo.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MARIA_SFX_DIR}"
        COMMAND ${CMAKE_COMMAND} -E env
            "CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/maria-sfx"
            "${CARGO_BIN}" run -q --release
            --manifest-path "${CMAKE_SOURCE_DIR}/tools/maria-sfx/Cargo.toml"
            -- build "${MARIA_SPK}" "${MARIA_SFX_DIR}"
        COMMAND sh -c "cd \"$0\" && xxd -i SD_RIH.VH > maria_vo.h && xxd -i SD_RIH.VB >> maria_vo.h"
            "${MARIA_SFX_DIR}"
        DEPENDS "${MARIA_SPK}"
                "${CMAKE_SOURCE_DIR}/tools/maria-sfx/Cargo.toml"
                "${CMAKE_SOURCE_DIR}/tools/maria-sfx/src/main.rs"
        VERBATIM
        COMMENT "Maria voice bank from sd_mar2.spk")
    set_source_files_properties(src/pc/pl_maria.c PROPERTIES
        INCLUDE_DIRECTORIES "${MARIA_GEN_DIR}"
        OBJECT_DEPENDS "${MARIA_SFX_DIR}/maria_vo.h")
endif()

# The port draws with sdl3_gl only: the HD textures and the vignette darken
# mode (Psyz_HdTex*, Psyz_VideoSetDarken, Psyz_VideoKeepCanvas) are not in
# sdl3_gpu, so a plain `cmake -B dir` must not take the psyz default.
set(PSYZ_RENDERER "sdl3_gl" CACHE STRING "Rendering backend: sdl3_gl")
if(NOT PSYZ_RENDERER STREQUAL "sdl3_gl")
    message(FATAL_ERROR "the port needs PSYZ_RENDERER=sdl3_gl, not '${PSYZ_RENDERER}'")
endif()
add_subdirectory(tools/psyz/psyz)

# the port version: the NACP and the boot line of the log
set(SOTN_VERSION "0.8.65")
set_source_files_properties(src/pc/main.c PROPERTIES
    COMPILE_DEFINITIONS "SOTN_VERSION=\"${SOTN_VERSION}\"")

if(NINTENDO_SWITCH)
    # the game data is copied to the SD card next to the NRO. Not
    # switch/sotn: other SOTN ports already use that folder.
    set(SOTN_SWITCH_DIR "sdmc:/switch/sotn-native")
    set(SOTN_RUNTIME_DATA_DIR "${SOTN_SWITCH_DIR}/data")
    list(APPEND SOURCE_FILES_PC src/pc/switch.c src/pc/switch_crash.c
        src/pc/switch_nx.c)
    set_source_files_properties(src/pc/switch.c PROPERTIES
        COMPILE_DEFINITIONS "SWITCH_DIR=\"${SOTN_SWITCH_DIR}\"")
endif()
if(WIN32)
    list(APPEND SOURCE_FILES_PC src/pc/win.c)
endif()
# build.py sets "data": the PC program then reads data/ from the folder it
# runs in, so out/pc can move. Empty: the absolute <build>/data.
set(SOTN_PC_DATA_DIR "" CACHE STRING "Data folder compiled into the PC program")
if(SOTN_PC_DATA_DIR AND NOT NINTENDO_SWITCH)
    set(SOTN_RUNTIME_DATA_DIR "${SOTN_PC_DATA_DIR}")
endif()

add_executable(${PROJECT_NAME}
    ${SOURCE_FILES_PC}
    ${SOURCE_FILES_CORE})
add_dependencies(${PROJECT_NAME} sotn_runtime_data)

target_include_directories(${PROJECT_NAME} PUBLIC
    include
    src/dra
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    _USE_MATH_DEFINES # needed for msvc
    PLATFORM_PSYZ
    PERMUTER
    NON_MATCHING
    HARD_LINK
    _internal_version_us
    __psyz
    SOTN_DATA_DIR="${SOTN_RUNTIME_DATA_DIR}"
)

target_include_directories(psyz PUBLIC tools/psyz/psyz/include)
target_compile_definitions(psyz PRIVATE __psyz)
psyz_title(${PROJECT_NAME} "Castlevania: Symphony of the Night")
psyz_strip_file_prefix(${PROJECT_NAME})
sotn_str_apply(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} PRIVATE psyz)
if(NOT MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()
if(NOT WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MINGW)
    # no console window (the log goes to sotn.log, src/pc/win.c); one
    # exe without libgcc/libwinpthread DLLs
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--subsystem,windows -static)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread) # clock_gettime
endif()

psyz_exports(${PROJECT_NAME})

# Castle room table for the teleport map and the editor page. It is built
# from the git-ignored assets into the build dir and never enters git.
find_program(SOTN_PYTHON3 python3)
if(NOT SOTN_PYTHON3)
    set(SOTN_PYTHON3 python3)
endif()
set(SOTN_GEN_DIR "${CMAKE_BINARY_DIR}/gen")
file(GLOB SOTN_CASTLE_ROOMS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/st/*/rooms.json"
    "${CMAKE_SOURCE_DIR}/assets/st/*/*/rooms.json"
    "${CMAKE_SOURCE_DIR}/assets/boss/*/rooms.json"
    "${CMAKE_SOURCE_DIR}/disks/us/F_MAP.BIN")
# Saturn builds add the Saturn stage rooms, read from the disc PRGs
set(SOTN_CASTLE_SATURN "")
if(ENABLE_SATURN_STAGES)
    set(SOTN_CASTLE_SATURN "${CMAKE_SOURCE_DIR}/disks/saturn")
endif()
add_custom_command(
    OUTPUT "${SOTN_GEN_DIR}/castle_data.c"
    COMMAND "${SOTN_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/editor/export_castle.py"
        "${CMAKE_SOURCE_DIR}/assets" "${SOTN_GEN_DIR}/castle_data.c"
        ${SOTN_CASTLE_SATURN}
    DEPENDS tools/editor/export_castle.py ${SOTN_CASTLE_ROOMS}
    VERBATIM)
# Apron patches (data/apron/*.txt, tile numbers only) and the editor page,
# built into the program as text.
file(GLOB SOTN_APRON_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/data/apron/*.txt")
add_custom_command(
    OUTPUT "${SOTN_GEN_DIR}/apron_data.c"
    COMMAND "${SOTN_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/editor/bake_apron.py"
        "${SOTN_GEN_DIR}/apron_data.c" g_ApronBaked ${SOTN_APRON_FILES}
    DEPENDS tools/editor/bake_apron.py ${SOTN_APRON_FILES}
    VERBATIM)
add_custom_command(
    OUTPUT "${SOTN_GEN_DIR}/editor_page.c"
    COMMAND "${SOTN_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/editor/bake_apron.py"
        "${SOTN_GEN_DIR}/editor_page.c" g_EditorPage
        "${CMAKE_SOURCE_DIR}/tools/editor/index.html"
    DEPENDS tools/editor/bake_apron.py tools/editor/index.html
    VERBATIM)
# The Ukrainian runtime pack (tools/lang, formats in tools/lang/FORMAT.md).
# It is made from tools/lang/uk/ and the extracted US disc into the build dir
# and the program. Optional: SOTN_LANG_MEDIA, the folder with ps1vram_uk.bin
# of the Xbox 360 uk mod. Only it has the labels that keep stock pixels;
# without it those labels stay English.
set(SOTN_LANG_MEDIA "$ENV{SOTN_LANG_MEDIA}" CACHE PATH "Folder with ps1vram_uk.bin of the Xbox 360 uk mod (optional)")
file(GLOB_RECURSE SOTN_LANG_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/lang/uk/*"
    "${CMAKE_SOURCE_DIR}/tools/lang/en/*" "${CMAKE_SOURCE_DIR}/tools/lang/fonts/*")
if(SOTN_LANG_MEDIA AND EXISTS "${SOTN_LANG_MEDIA}/ps1vram_uk.bin")
    list(APPEND SOTN_LANG_FILES "${SOTN_LANG_MEDIA}/ps1vram_uk.bin")
endif()
# The PSP text of the owner's image (tools/lang/disc_text.py; not in git):
# the English PSP script table script_psp_en.bin
file(GLOB SOTN_DISC_TEXT CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/disks/text/*.json")
list(APPEND SOTN_LANG_FILES ${SOTN_DISC_TEXT})
add_custom_command(
    OUTPUT "${SOTN_GEN_DIR}/lang_pack_uk.c"
    COMMAND "${CMAKE_COMMAND}" -E env
        "SOTN_DATA_ROOT=${CMAKE_SOURCE_DIR}"
        "SOTN_LANG_MEDIA=${SOTN_LANG_MEDIA}"
        "SOTN_LANG_OUT=${CMAKE_BINARY_DIR}/lang/uk"
        "${SOTN_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/lang/sotn_lang.py" pack
        --c "${SOTN_GEN_DIR}/lang_pack_uk.c"
    DEPENDS tools/lang/sotn_lang.py tools/lang/serif_latin.py ${SOTN_LANG_FILES}
    VERBATIM)
target_sources(${PROJECT_NAME} PRIVATE "${SOTN_GEN_DIR}/castle_data.c"
    "${SOTN_GEN_DIR}/apron_data.c" "${SOTN_GEN_DIR}/editor_page.c"
    "${SOTN_GEN_DIR}/lang_pack_uk.c")
# The Ukrainian pictures (art.bin) follow every LoadImage: src/pc/lang.c.
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=LoadImage)
# The English names of the two Saturn familiars come from the PSP English
# menu table (make extract_disk_pspeu, or 7z x sotn.pspeu.iso ...PSPBIN/dra.bin;
# not in git). Without dra.bin they keep the Japanese names of the US data.
set(PSP_DRA_BIN
    "${CMAKE_SOURCE_DIR}/disks/pspeu/PSP_GAME/USRDIR/res/ps/PSPBIN/dra.bin")
if(EXISTS "${PSP_DRA_BIN}")
    set(PSP_NAMES_H "${SOTN_GEN_DIR}/psp_familiar_names.h")
    add_custom_command(
        OUTPUT "${PSP_NAMES_H}"
        COMMAND "${SOTN_PYTHON3}" tools/psp_familiar_names.py
            "${PSP_DRA_BIN}" src/config_us.h "${PSP_NAMES_H}"
        DEPENDS "${PSP_DRA_BIN}" tools/psp_familiar_names.py
                tools/lang/sotn_lang.py src/config_us.h
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VERBATIM
        COMMENT "Familiar names from the PSP disc")
    set_source_files_properties(src/dra/menu.c PROPERTIES
        INCLUDE_DIRECTORIES "${SOTN_GEN_DIR}"
        COMPILE_DEFINITIONS SOTN_PSP_FAMILIAR_NAMES
        OBJECT_DEPENDS "${PSP_NAMES_H}")
endif()

# The Saturn's own hand items (include/items.h ITEM_ASTRAL_DAGGER..): their
# numbers from the Saturn GAME.PRG. Without the disc the PS1 stand-in rows
# are used. Konami's data: build dir only.
set(_satitems_disc "${CMAKE_SOURCE_DIR}/disks/saturn")
set(_satitems_h "${SOTN_GEN_DIR}/saturn_items/saturn_items.h")
set(_satitems_deps tools/saturn/saturn_items.py assets/dra/us/equipment.yaml)
if(EXISTS "${_satitems_disc}/GAME.PRG")
    list(APPEND _satitems_deps "${_satitems_disc}/GAME.PRG")
endif()
add_custom_command(
    OUTPUT "${_satitems_h}"
    COMMAND "${SOTN_PYTHON3}" tools/saturn/saturn_items.py
        "${_satitems_disc}" assets/dra/us/equipment.yaml "${_satitems_h}"
    DEPENDS ${_satitems_deps}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
    COMMENT "Saturn hand items from GAME.PRG")
set_source_files_properties(src/dra/config_us.c PROPERTIES
    INCLUDE_DIRECTORIES "${SOTN_GEN_DIR}/saturn_items"
    OBJECT_DEPENDS "${_satitems_h}")

# PC copies of overlay data that is still raw asm on PSX (tools/pcdata, from
# sotn-vita). They hold disc bytes, so they are generated from disks/us into
# the build dir and never enter git. sotn_pcdata(var src/boss/bo6/x.c ...)
# sets var to the generated files.
function(sotn_pcdata out)
    set(_files "")
    foreach(rel IN LISTS ARGN)
        set(_o "${SOTN_GEN_DIR}/pcdata/${rel}")
        add_custom_command(
            OUTPUT "${_o}"
            COMMAND "${SOTN_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/pcdata/pcdata.py"
                --root "${CMAKE_SOURCE_DIR}" generate
                "${CMAKE_SOURCE_DIR}/tools/pcdata/${rel}.in" "${_o}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_o}"
            DEPENDS tools/pcdata/pcdata.py tools/pcdata/${rel}.in
            VERBATIM)
        list(APPEND _files "${_o}")
    endforeach()
    set(${out} ${_files} PARENT_SCOPE)
endfunction()

function(add_overlay name)
    psyz_add_module(${name} SOURCES ${ARGN} HOST ${PROJECT_NAME})
    set(_target ${name})

    target_include_directories(${_target} BEFORE PRIVATE
        include
        src/dra
        src/pc
        src/st/${name}
        src/boss/${name}
        tools/psyz/psyz/include
    )

    target_compile_definitions(${_target} PRIVATE
        _USE_MATH_DEFINES
        PLATFORM_PSYZ
        PERMUTER
        NON_MATCHING
        HARD_LINK
        _internal_version_us
        __psyz
        OVERLAY_BUILD
        VERSION_PC # selects the PC C bodies of functions still asm on PSX
    )

    if(NOT MSVC AND NOT APPLE)
        target_link_options(${name} PRIVATE -Wl,-Bsymbolic)
    endif()

    psyz_strip_file_prefix(${_target})
    sotn_str_apply(${_target})
    set(${name}_COMPILE_TARGET ${_target} PARENT_SCOPE)
endfunction()

add_overlay(sel ${SOURCE_FILES_STAGE_SEL})
# The Player select art (src/st/sel/charsel.h): the portraits from the
# owner's art in SOTN_CHARSEL_ART (else the Saturn's), the Saturn's header
# and gothic names. It is Konami's art: made into the build dir, never into
# git. The frame is drawn by tools/saturn/charsel_frame.py.
set(SOTN_CHARSEL_ART "$ENV{SOTN_CHARSEL_ART}" CACHE PATH
    "folder of the owner's select screen portraits; empty: the Saturn's")
set(_charsel_disc "${CMAKE_SOURCE_DIR}/disks/saturn")
set(_charsel_h "${SOTN_GEN_DIR}/charsel/charsel_gfx.h")
set(_charsel_deps tools/saturn/charsel_gfx.py tools/saturn/charsel_frame.py
    tools/saturn/tile_dumper.py)
foreach(_f TITLE.PRG TITLE.CHR TITLE.MAP)
    if(EXISTS "${_charsel_disc}/${_f}")
        list(APPEND _charsel_deps "${_charsel_disc}/${_f}")
    endif()
endforeach()
if(SOTN_CHARSEL_ART)
    file(GLOB _charsel_art "${SOTN_CHARSEL_ART}/*.png" "${SOTN_CHARSEL_ART}/*.webp")
    list(APPEND _charsel_deps ${_charsel_art})
endif()
add_custom_command(
    OUTPUT "${_charsel_h}"
    COMMAND "${SOTN_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/saturn/charsel_gfx.py"
        "${_charsel_disc}" "${SOTN_CHARSEL_ART}" "${_charsel_h}"
    DEPENDS ${_charsel_deps}
    VERBATIM)
add_custom_target(charsel_gfx DEPENDS "${_charsel_h}")
add_dependencies(sel charsel_gfx)
target_include_directories(sel PRIVATE "${SOTN_GEN_DIR}/charsel")
add_overlay(no0 ${SOURCE_FILES_STAGE_NO0})
add_overlay(no1 ${SOURCE_FILES_STAGE_NO1})
add_overlay(lib ${SOURCE_FILES_STAGE_LIB})
add_overlay(cat ${SOURCE_FILES_STAGE_CAT})
add_overlay(no2 ${SOURCE_FILES_STAGE_NO2})
add_overlay(chi ${SOURCE_FILES_STAGE_CHI})
add_overlay(dai ${SOURCE_FILES_STAGE_DAI})
add_overlay(rdai ${SOURCE_FILES_STAGE_RDAI})
add_overlay(np3 ${SOURCE_FILES_STAGE_NP3})
add_overlay(cen ${SOURCE_FILES_STAGE_CEN})
add_overlay(no4 ${SOURCE_FILES_STAGE_NO4})
add_overlay(are ${SOURCE_FILES_STAGE_ARE})
add_overlay(rare ${SOURCE_FILES_STAGE_RARE})
add_overlay(top ${SOURCE_FILES_STAGE_TOP})
add_overlay(nz0 ${SOURCE_FILES_STAGE_NZ0})
add_overlay(nz1 ${SOURCE_FILES_STAGE_NZ1})
add_overlay(wrp ${SOURCE_FILES_STAGE_WRP})
add_overlay(dre ${SOURCE_FILES_STAGE_DRE})
add_overlay(mar ${SOURCE_FILES_BOSS_MAR})
add_overlay(bo5 ${SOURCE_FILES_BOSS_BO5})
add_overlay(bo3 ${SOURCE_FILES_BOSS_BO3})
add_overlay(bo1 ${SOURCE_FILES_BOSS_BO1})
add_overlay(st0 ${SOURCE_FILES_STAGE_ST0})
# The uk prologue scroll text (src/st/st0/3101C_psx.c)
add_custom_command(
    OUTPUT "${SOTN_GEN_DIR}/st0/prologue_uk.h"
    COMMAND "${SOTN_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/lang/uk/prologue_text.py"
        "${CMAKE_SOURCE_DIR}/tools/lang/uk/prologue.txt"
        "${SOTN_GEN_DIR}/st0/prologue_uk.h"
    DEPENDS tools/lang/uk/prologue_text.py tools/lang/uk/prologue.txt
    VERBATIM)
target_sources(st0 PRIVATE "${SOTN_GEN_DIR}/st0/prologue_uk.h")
target_include_directories(st0 PRIVATE "${SOTN_GEN_DIR}/st0")
add_overlay(rno0 ${SOURCE_FILES_STAGE_RNO0})
add_overlay(rno1 ${SOURCE_FILES_STAGE_RNO1})
add_overlay(rlib ${SOURCE_FILES_STAGE_RLIB})
add_overlay(rcat ${SOURCE_FILES_STAGE_RCAT})
add_overlay(rno2 ${SOURCE_FILES_STAGE_RNO2})
add_overlay(rchi ${SOURCE_FILES_STAGE_RCHI})
add_overlay(rno3 ${SOURCE_FILES_STAGE_RNO3})
add_overlay(rcen ${SOURCE_FILES_STAGE_RCEN})
add_overlay(rno4 ${SOURCE_FILES_STAGE_RNO4})
add_overlay(rtop ${SOURCE_FILES_STAGE_RTOP})
add_overlay(rnz0 ${SOURCE_FILES_STAGE_RNZ0})
add_overlay(rnz1 ${SOURCE_FILES_STAGE_RNZ1})
add_overlay(rwrp ${SOURCE_FILES_STAGE_RWRP})
add_overlay(rbo7 ${SOURCE_FILES_BOSS_RBO7})
add_overlay(rbo6 ${SOURCE_FILES_BOSS_RBO6})
add_overlay(bo0 ${SOURCE_FILES_BOSS_BO0})
sotn_pcdata(RBO8_PCDATA src/boss/rbo8/palette_def.c)
add_overlay(rbo8 ${SOURCE_FILES_BOSS_RBO8} ${RBO8_PCDATA})
sotn_pcdata(BO2_PCDATA src/boss/bo2/d_pc_data.c)
add_overlay(bo2 ${SOURCE_FILES_BOSS_BO2} ${BO2_PCDATA})
add_overlay(rbo2 ${SOURCE_FILES_BOSS_RBO2})
add_overlay(rbo1 ${SOURCE_FILES_BOSS_RBO1})
add_overlay(bo7 ${SOURCE_FILES_BOSS_BO7})
sotn_pcdata(BO6_PCDATA src/boss/bo6/d_pc_data.c src/boss/bo6/d_pc_sprites.c
    src/boss/bo6/d_pc_bss.c)
add_overlay(bo6 ${SOURCE_FILES_BOSS_BO6} ${BO6_PCDATA})
add_overlay(rbo5 ${SOURCE_FILES_BOSS_RBO5})
set(SOURCE_FILES_BOSS_BO4
    src/pc/bosses/boss_bo4.c
    src/boss/bo4/background_block_init.c
    src/boss/bo4/bss.c
    src/boss/bo4/collision.c
    src/boss/bo4/create_entity.c
    src/boss/bo4/d_prize_drops.c
    src/boss/bo4/doors.c
    src/boss/bo4/dop_anim.c
    src/boss/bo4/dop_collision.c
    src/boss/bo4/doppleganger.c
    src/boss/bo4/doppleganger_anim.c
    src/boss/bo4/e_breakable.c
    src/boss/bo4/e_collect.c
    src/boss/bo4/e_init.c
    src/boss/bo4/e_life_up.c
    src/boss/bo4/e_lock_camera.c
    src/boss/bo4/e_misc.c
    src/boss/bo4/e_particles.c
    src/boss/bo4/e_red_door.c
    src/boss/bo4/e_red_door_tiles.c
    src/boss/bo4/e_room_bg.c
    src/boss/bo4/e_room_fg.c
    src/boss/bo4/e_stage_name.c
    src/boss/bo4/graphics_banks.c
    src/boss/bo4/header.c
    src/boss/bo4/layers.c
    src/boss/bo4/palette_def.c
    src/boss/bo4/popup.c
    src/boss/bo4/prim_helpers.c
    src/boss/bo4/sprite_banks.c
    src/boss/bo4/spritesheets.c
    src/boss/bo4/st_common.c
    src/boss/bo4/st_update.c
    src/boss/bo4/stage_data.c
    src/boss/bo4/unk_365FC.c
    src/boss/bo4/unk_45648.c
    src/boss/bo4/unk_46040.c
    src/boss/bo4/unk_465F0.c
    src/boss/bo4/unk_49A6C.c
    src/boss/bo4/unk_50D4C.c
    src/boss/bo4/gen/e_laydef.c
    src/boss/bo4/gen/e_layout.c
    src/boss/bo4/gen/rooms.c
)
# the Doppleganger sprite tables and unarmed animations are still raw on PSX
sotn_pcdata(BO4_PCDATA src/boss/bo4/d_pc_sprites.c)
add_overlay(bo4 ${SOURCE_FILES_BOSS_BO4} ${BO4_PCDATA})
add_overlay(rbo4 ${SOURCE_FILES_BOSS_RBO4})
add_overlay(rbo3 ${SOURCE_FILES_BOSS_RBO3})
add_overlay(rbo0 ${SOURCE_FILES_BOSS_RBO0})
add_overlay(mad ${SOURCE_FILES_STAGE_MAD})
add_overlay(no3 ${SOURCE_FILES_STAGE_NO3})

if(ENABLE_SATURN_STAGES)
    set(SATURN_STITCH_DIR "${CMAKE_BINARY_DIR}/saturn_stitches")

    function(add_saturn_stitch stage command prg map assets tileset rooms layers layouts)
        set(output "${SATURN_STITCH_DIR}/${stage}.stamp")
        set(asset_dir "${SATURN_STITCH_DIR}/assets/${stage}")
        set(source_dir "${SATURN_STITCH_DIR}/src/${stage}")
        get_filename_component(layout_dir "${layouts}" DIRECTORY)
        if(layout_dir STREQUAL "")
            set(layout_gen_dir "${source_dir}/gen")
        else()
            set(layout_gen_dir "${source_dir}/gen/${layout_dir}")
        endif()
        set(packed_stamp "${source_dir}/packed.stamp")
        set(packed_sources
            "${source_dir}/gen/${rooms}.c"
            "${layout_gen_dir}/e_laydef.c"
            "${layout_gen_dir}/e_layout.c")
        file(GLOB_RECURSE source_assets CONFIGURE_DEPENDS "${assets}/*")
        file(GLOB entity_layout_assets CONFIGURE_DEPENDS
            "${assets}/entity_layouts.json"
            "${assets}/us/entity_layouts.json"
            "${assets}/../entity_layouts.json")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${SATURN_ASSETS_BIN}" map "${command}"
                    "${prg}" "${map}" "${assets}" "${tileset}" "${output}"
            DEPENDS "${SATURN_ASSETS_BIN}" "${prg}" "${map}" "${tileset}"
                    ${source_assets} ${entity_layout_assets}
            VERBATIM
            COMMENT "Generating ${stage} Saturn-to-PSX stitch")
        add_custom_command(
            OUTPUT "${packed_stamp}" ${packed_sources}
            COMMAND "${SOTN_ASSETS_HOST_BIN}" pack-stage
                    "${stage}" "${asset_dir}" "${source_dir}"
                    "${rooms}" "${layers}" "${layouts}"
            DEPENDS "${SOTN_ASSETS_HOST_BIN}" "${output}"
            VERBATIM
            COMMENT "Packing ${stage} stitched stage assets")
        set_source_files_properties(${packed_sources} PROPERTIES GENERATED TRUE)
        add_custom_target(${stage}_stitch DEPENDS "${output}" "${packed_stamp}")
        add_dependencies(${stage} ${stage}_stitch)
        add_dependencies(${PROJECT_NAME} ${stage}_stitch)
        get_target_property(stage_sources ${stage} SOURCES)
        list(FILTER stage_sources EXCLUDE REGEX
            "/gen/(us/)?(rooms|e_laydef|e_layout)\\.c$")
        set_property(TARGET ${stage} PROPERTY SOURCES "${stage_sources}")
        target_sources(${stage} PRIVATE ${packed_sources})
        target_include_directories(${stage} BEFORE PRIVATE
            "${source_dir}" "${CMAKE_SOURCE_DIR}/src/st/${stage}")
        target_compile_definitions(${stage} PRIVATE
            ENABLE_SATURN_STAGES GEN_VERSION_ANGLE)
    endfunction()

    add_saturn_stitch(
        np3 stitch-np3
        "${CMAKE_SOURCE_DIR}/disks/saturn/STAGE_01.PRG"
        "${CMAKE_SOURCE_DIR}/disks/saturn/STAGE_01.MAP"
        "${CMAKE_SOURCE_DIR}/assets/st/np3"
        "${CMAKE_SOURCE_DIR}/disks/us/ST/NP3/F_NP3.BIN"
        rooms layers us/entity_layouts)
    add_saturn_stitch(
        rno3 stitch-rno3
        "${CMAKE_SOURCE_DIR}/disks/saturn/RSTAGE01.PRG"
        "${CMAKE_SOURCE_DIR}/disks/saturn/RSTAGE01.MAP"
        "${CMAKE_SOURCE_DIR}/assets/st/rno3"
        "${CMAKE_SOURCE_DIR}/disks/us/ST/RNO3/F_RNO3.BIN"
        us/rooms us/layers entity_layouts)
    add_saturn_stitch(
        no0 stitch-no0
        "${CMAKE_SOURCE_DIR}/disks/saturn/STAGE_03.PRG"
        "${CMAKE_SOURCE_DIR}/disks/saturn/STAGE_03.MAP"
        "${CMAKE_SOURCE_DIR}/assets/st/no0"
        "${CMAKE_SOURCE_DIR}/disks/us/ST/NO0/F_NO0.BIN"
        rooms layers entity_layouts)
    add_saturn_stitch(
        no4 stitch-no4
        "${CMAKE_SOURCE_DIR}/disks/saturn/STAGE_09.PRG"
        "${CMAKE_SOURCE_DIR}/disks/saturn/STAGE_9B.MAP"
        "${CMAKE_SOURCE_DIR}/assets/st/no4"
        "${CMAKE_SOURCE_DIR}/disks/us/ST/NO4/F_NO4.BIN"
        rooms layers entity_layouts)
    add_saturn_stitch(
        rno0 stitch-rno0
        "${CMAKE_SOURCE_DIR}/disks/saturn/RSTAGE03.PRG"
        "${CMAKE_SOURCE_DIR}/disks/saturn/RSTAGE03.MAP"
        "${CMAKE_SOURCE_DIR}/assets/st/rno0"
        "${CMAKE_SOURCE_DIR}/disks/us/ST/RNO0/F_RNO0.BIN"
        rooms layers entity_layouts)
    add_saturn_stitch(
        rno4 stitch-rno4
        "${CMAKE_SOURCE_DIR}/disks/saturn/RSTAGE09.PRG"
        "${CMAKE_SOURCE_DIR}/disks/saturn/RSTAGE9B.MAP"
        "${CMAKE_SOURCE_DIR}/assets/st/rno4"
        "${CMAKE_SOURCE_DIR}/disks/us/ST/RNO4/F_RNO4.BIN"
        rooms layers entity_layouts)

    target_sources(${PROJECT_NAME} PRIVATE src/pc/room_teleport.c)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_SATURN_STAGES)
    # Switch: no host path in the NRO. The six stitched F_*.BIN replace the
    # retail files in sdmc:/switch/sotn-native/data/ST/<STAGE>/ on deploy.
    # The same for a PC program with SOTN_PC_DATA_DIR (build.py copies them).
    if(NOT NINTENDO_SWITCH AND NOT SOTN_PC_DATA_DIR)
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            PC_ASSET_OVERRIDE_DIR="${SATURN_STITCH_DIR}/disks/us")
    endif()
endif()

# All 59 weapons. 41 of them use data that is still raw on PSX
# (g_WeaponCluts and D_<addr> tables). Their typed PC copy is generated from
# disks/us by tools/pcdata/src/pc/weapons/w_NNN.c.in (no disc data in git).
foreach(id 000 001 002 003 004 005 006 007 008 009 010 011 012 013 014 015 016 017 018 019 020 021 022 023 024 025 026 027 028 029 030 031 032 033 034 035 036 037 038 039 040 041 042 043 044 045 046 047 048 049 050 051 052 053 054 055 056 057 058)
    set(_data "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/pcdata/src/pc/weapons/w_${id}.c.in")
        sotn_pcdata(_data src/pc/weapons/w_${id}.c)
    endif()
    add_overlay(w0_${id} src/weapon/w_init.c src/weapon/right_hand.c src/weapon/w_${id}.c ${_data})
    target_compile_definitions(${w0_${id}_COMPILE_TARGET} PRIVATE WEAPON0)
    add_overlay(w1_${id} src/weapon/w_init.c src/weapon/left_hand.c src/weapon/w_${id}.c ${_data})
    target_compile_definitions(${w1_${id}_COMPILE_TARGET} PRIVATE WEAPON1)
endforeach()

add_overlay(tt_000 ${SOURCE_FILES_TT_000})
add_overlay(tt_001 ${SOURCE_FILES_TT_001})
add_overlay(tt_002 ${SOURCE_FILES_TT_002})
add_overlay(tt_003 ${SOURCE_FILES_TT_003})
add_overlay(tt_004 ${SOURCE_FILES_TT_004})
add_overlay(tt_005 ${SOURCE_FILES_TT_005})
add_overlay(tt_006 ${SOURCE_FILES_TT_006})

if(ENABLE_SATURN_STAGES)
    include(cmake/saturn_stages.cmake)
endif()

option(SOTN_FRAME_STATS "Log fps and busy ms per frame to stderr" ${NINTENDO_SWITCH})
if(SOTN_FRAME_STATS)
    target_compile_definitions(psyz PRIVATE PSYZ_FRAME_STATS)
endif()

if(NINTENDO_SWITCH)
    nx_generate_nacp(sotn.nacp
        NAME "Castlevania: SOTN"
        AUTHOR "sotn-decomp"
        VERSION "${SOTN_VERSION}")
    nx_create_nro(${PROJECT_NAME} NACP sotn.nacp)
endif()
