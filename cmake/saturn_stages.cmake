find_program(SATURN_STAGE_CARGO_BIN cargo)
if(NOT SATURN_STAGE_CARGO_BIN)
    message(FATAL_ERROR "cargo is required to convert the Saturn stages")
endif()
file(GLOB_RECURSE SATURN_STAGE_CONVERTER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/saturn/assets/src/*.rs")
set(SATURN_STAGE_SCALE_MODEL "raster" CACHE STRING
    "Saturn stage horizontal conversion model (raster or tile-groups)")
set_property(CACHE SATURN_STAGE_SCALE_MODEL PROPERTY STRINGS raster tile-groups)
option(SATURN_STAGE_ENTITY_LABELS
    "Draw Saturn stage entity IDs and params at their world positions" OFF)

enable_testing()

# The other arguments are the stage's own sources (src/st/<name>/*.c).
# music "": a stage with no track (WARP); test_stage "": no room test.
function(add_saturn_stage name source music test_stage)
    set(disc "${CMAKE_SOURCE_DIR}/disks/saturn")
    string(TOUPPER "${name}" upper)
    # Host folder for the converter output. On Switch SOTN_RUNTIME_DATA_DIR
    # is the sdmc: path on the console: only the define below uses it.
    set(asset_dir "${CMAKE_BINARY_DIR}/data/ST/${upper}")
    add_overlay(${name} src/pc/stages/stage_saturn.c ${ARGN})
    set(outputs
        "${asset_dir}/conversion-report.json"
        "${asset_dir}/saturn_stage_data.h"
        "${asset_dir}/masonry.chars.bin"
        "${asset_dir}/masonry.pal.bin")
    set(convert_commands
        COMMAND ${SATURN_STAGE_CARGO_BIN} run --release
                --manifest-path "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
                -- map convert-psx
                "${disc}/${source}.PRG"
                "${disc}/${source}.MAP"
                "${asset_dir}"
                --scale-model "${SATURN_STAGE_SCALE_MODEL}")
    set(music_deps)
    if(music)
        list(APPEND outputs
            "${asset_dir}/music/audio.xa"
            "${asset_dir}/music/audio.cue"
            "${asset_dir}/music/xa_config.h"
            "${asset_dir}/music/xa_manifest.json")
        list(APPEND convert_commands
            COMMAND ${SATURN_STAGE_CARGO_BIN} run --release
                --manifest-path "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
                -- audio export-xa
                "${disc}/SD/${music}.PCM"
                "${asset_dir}/music"
                --file-number 25
                --channel 0)
        set(music_deps "${disc}/SD/${music}.PCM")
    endif()
    add_custom_command(
        OUTPUT ${outputs}
        ${convert_commands}
        DEPENDS
            ${SATURN_STAGE_CONVERTER_SOURCES}
            "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
            "${disc}/${source}.PRG"
            "${disc}/${source}.MAP"
            "${disc}/${source}.CHR"
            ${music_deps}
            "${disc}/0.BIN"
            "${disc}/GAME.PRG"
            "${disc}/LOAD.MAP"
            "${disc}/RLOAD.MAP"
        VERBATIM
        COMMENT "Converting Saturn ${source} for its PC overlay slot")
    add_custom_target(${name}_pc_assets ALL DEPENDS ${outputs})
    add_dependencies(${PROJECT_NAME} ${name}_pc_assets)
    add_dependencies(${name} ${name}_pc_assets)
    target_compile_definitions(${name} PRIVATE
        SATURN_STAGE_ASSET_DIR="${SOTN_RUNTIME_DATA_DIR}/ST/${upper}"
        SATURN_STAGE_NATIVE_8X8)
    target_include_directories(${name} PRIVATE "${asset_dir}")
    if(SATURN_STAGE_ENTITY_LABELS)
        target_compile_definitions(${name} PRIVATE SATURN_STAGE_ENTITY_LABELS)
    endif()

    if(NOT test_stage)
        return()
    endif()
    add_executable(${name}_room_test EXCLUDE_FROM_ALL
        src/pc/stages/stage_saturn_test.c
        src/pc/stages/stage_saturn.c)
    add_dependencies(${name}_room_test ${name}_pc_assets)
    target_compile_definitions(${name}_room_test PRIVATE
        SATURN_STAGE_ROOM_TEST
        ${test_stage}
        PLATFORM_PSYZ
        _internal_version_us
        __psyz)
    target_include_directories(${name}_room_test PRIVATE
        include src/dra src/pc src/pc/stages tools/psyz/psyz/include "${asset_dir}")
    add_test(NAME ${name}_rooms COMMAND ${name}_room_test)
endfunction()

# Underground Garden: the PS1 stage framework (create_entity, collision,
# st_update) runs the Saturn enemies; gdn_entities.py converts their sprites,
# palettes and enemy records from the disc into the build folder
set(SATURN_GDN_SOURCES
    src/st/stage15/bss.c
    src/st/stage15/collision.c
    src/st/stage15/create_entity.c
    src/st/stage15/d_prize_drops.c
    src/st/stage15/e_breakable.c
    src/st/stage15/e_collect.c
    src/st/stage15/e_init.c
    src/st/stage15/e_misc.c
    src/st/stage15/e_mod_banner.c
    src/st/stage15/e_particles.c
    src/st/stage15/e_room_bg.c
    src/st/stage15/e_slinger.c
    src/st/stage15/e_thornweed.c
    src/st/stage15/e_water.c
    src/st/stage15/popup.c
    src/st/stage15/prim_helpers.c
    src/st/stage15/st_common.c
    src/st/stage15/st_update.c)
add_saturn_stage(stage15 STAGE_15 SD25 TEST_STAGE15 ${SATURN_GDN_SOURCES})
find_program(SATURN_GDN_PYTHON python3)
if(NOT SATURN_GDN_PYTHON)
    message(FATAL_ERROR "python3 is required to convert the Saturn enemies")
endif()
# gdn_entities.py <disc> <out> <stage>: the stage's enemy sprites, palettes
# and records into the build folder (data/ST/<NAME>)
function(add_saturn_gdn_entities name source)
    string(TOUPPER "${name}" upper)
    set(gdn_dir "${CMAKE_BINARY_DIR}/data/ST/${upper}")
    set(gdn_disc "${CMAKE_SOURCE_DIR}/disks/saturn")
    add_custom_command(
        OUTPUT "${gdn_dir}/gdn_entities.h" "${gdn_dir}/gdn_defs.h"
        COMMAND ${SATURN_GDN_PYTHON} -B
                "${CMAKE_SOURCE_DIR}/tools/saturn/entities/gdn_entities.py"
                "${gdn_disc}" "${gdn_dir}/gdn_entities.h" "${source}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/tools/saturn/entities/gdn_entities.py"
            "${CMAKE_SOURCE_DIR}/tools/saturn/entities/saturn_sprites.py"
            "${CMAKE_SOURCE_DIR}/tools/saturn/entities/rstage15_entities.py"
            "${gdn_disc}/${source}.PRG"
            "${gdn_disc}/${source}.CHR"
            "${gdn_disc}/0.BIN"
            "${gdn_disc}/GAME.PRG"
        VERBATIM
        COMMENT "Converting the Saturn ${source} enemies")
    add_custom_target(${name}_gdn_assets
        DEPENDS "${gdn_dir}/gdn_entities.h" "${gdn_dir}/gdn_defs.h")
    add_dependencies(${name} ${name}_gdn_assets)
    target_compile_definitions(${name} PRIVATE SATURN_STAGE_GDN ${ARGN})
endfunction()
add_saturn_gdn_entities(stage15 STAGE_15)
# Cursed Prison: the same framework (src/st/stage16) with the Red Gargoyle and
# the PS1's Spear Guard, Bone Archer and Slinger
set(SATURN_PRISON_SOURCES
    src/st/stage16/bss.c
    src/st/stage16/collision.c
    src/st/stage16/create_entity.c
    src/st/stage16/d_prize_drops.c
    src/st/stage16/e_breakable.c
    src/st/stage16/e_collect.c
    src/st/stage16/e_init.c
    src/st/stage16/e_misc.c
    src/st/stage16/e_particles.c
    src/st/stage16/e_room_bg.c
    src/st/stage16/e_spear_guard.c
    src/st/stage16/e_bone_archer.c
    src/st/stage16/e_slinger.c
    src/st/stage16/e_mod_banner.c
    src/st/stage16/e_mod_ghosts.c
    src/st/stage16/e_mod_pillar.c
    src/st/stage16/popup.c
    src/st/stage16/prim_helpers.c
    src/st/stage16/st_common.c
    src/st/stage16/st_update.c)
add_saturn_stage(stage16 STAGE_16 SD26 TEST_STAGE16 ${SATURN_PRISON_SOURCES})
add_saturn_gdn_entities(stage16 STAGE_16 SATURN_STAGE_PRISON)
# Reverse Garden: the same framework (src/st/rstage15) with the PS1's Cave
# Troll, Blue Venus Weed, Bitterfly and Cloaked Knight and the Saturn-only
# Copper Armor, Efreet and Skeleton Guardian (rstage15_entities.py)
set(SATURN_RGDN_SOURCES
    src/st/rstage15/bss.c
    src/st/rstage15/collision.c
    src/st/rstage15/create_entity.c
    src/st/rstage15/d_prize_drops.c
    src/st/rstage15/e_bitterfly.c
    src/st/rstage15/e_breakable.c
    src/st/rstage15/e_cave_troll.c
    src/st/rstage15/e_cloaked_knight.c
    src/st/rstage15/e_collect.c
    src/st/rstage15/e_init.c
    src/st/rstage15/e_misc.c
    src/st/rstage15/e_mod_copper.c
    src/st/rstage15/e_mod_efreet.c
    src/st/rstage15/e_mod_guardian.c
    src/st/rstage15/e_particles.c
    src/st/rstage15/e_room_bg.c
    src/st/rstage15/e_venus_weed.c
    src/st/rstage15/e_water.c
    src/st/rstage15/popup.c
    src/st/rstage15/prim_helpers.c
    src/st/rstage15/st_common.c
    src/st/rstage15/st_update.c)
add_saturn_stage(rstage15 RSTAGE15 SD29 TEST_RSTAGE15 ${SATURN_RGDN_SOURCES})
add_saturn_gdn_entities(rstage15 RSTAGE15 SATURN_STAGE_RGARDEN)
# the inverted Cursed Prison: the same framework (src/st/rstage16) with the
# Gargoyle, the PS1's Flying Zombie, Jack O'Bones and Nova Skeleton, and the
# Saturn-only Wight and Wraith
set(SATURN_RPRISON_SOURCES
    src/st/rstage16/bss.c
    src/st/rstage16/collision.c
    src/st/rstage16/create_entity.c
    src/st/rstage16/d_prize_drops.c
    src/st/rstage16/e_bloody_zombie.c
    src/st/rstage16/e_breakable.c
    src/st/rstage16/e_collect.c
    src/st/rstage16/e_flying_zombie.c
    src/st/rstage16/e_init.c
    src/st/rstage16/e_jack_o_bones.c
    src/st/rstage16/e_misc.c
    src/st/rstage16/e_mod_wight.c
    src/st/rstage16/e_mod_wraith.c
    src/st/rstage16/e_nova_skeleton.c
    src/st/rstage16/e_particles.c
    src/st/rstage16/e_room_bg.c
    src/st/rstage16/popup.c
    src/st/rstage16/prim_helpers.c
    src/st/rstage16/st_common.c
    src/st/rstage16/st_update.c)
add_saturn_stage(rstage16 RSTAGE16 SD2C TEST_RSTAGE16 ${SATURN_RPRISON_SOURCES})
add_saturn_gdn_entities(rstage16 RSTAGE16 SATURN_STAGE_RPRISON)
# The sixth warp room (WARP.PRG room 0x1E): the PS1 Warp Room framework
# (src/st/wrp) around the rooms the converter reads from the disc. The pad is
# stage_saturn.c's own, with no texture. The Saturn plays no music here.
set(SATURN_WARP_SOURCES
    src/st/wrp/background_block_init.c
    src/st/wrp/bss.c
    src/st/wrp/collision.c
    src/st/wrp/create_entity.c
    src/st/wrp/d_prize_drops.c
    src/st/wrp/e_breakable.c
    src/st/wrp/e_collect.c
    src/st/wrp/e_init.c
    src/st/wrp/e_lock_camera.c
    src/st/wrp/e_misc.c
    src/st/wrp/e_particles.c
    src/st/wrp/e_room_bg.c
    src/st/wrp/e_room_fg.c
    src/st/wrp/e_stage_name.c
    src/st/wrp/popup.c
    src/st/wrp/prim_helpers.c
    src/st/wrp/st_common.c
    src/st/wrp/st_update.c)
add_saturn_stage(stagewarp WARP "" "" ${SATURN_WARP_SOURCES})
target_compile_definitions(stagewarp PRIVATE SATURN_STAGE_GDN SATURN_STAGE_WARP
    ENABLE_SATURN_STAGES)
target_include_directories(stagewarp BEFORE PRIVATE src/st/wrp)

# Saturn-only sounds and the boss track (tools/saturn/saturn_audio.py): WAVs
# in data/ST/<STAGE>/sfx/ and data/ST/STAGE15/music/, played by
# src/pc/saturn_sfx.c and src/pc/music.c. saturn-sfx (tools/saturn/sound-player
# with Ymir's SCSP) renders the sequenced sounds from the stage CRTs.
# OFF, or no Saturn files: the PS1 stand-ins play.
option(SATURN_STAGE_AUDIO "Convert the Saturn stage sounds and boss track" OFF)
set(SATURN_SOUND_PLAYER_DIR "${CMAKE_SOURCE_DIR}/tools/saturn/sound-player"
    CACHE PATH "saturn-sound-player source (a git submodule, with Ymir)")
if(SATURN_STAGE_AUDIO)
    if(NOT EXISTS "${SATURN_SOUND_PLAYER_DIR}/ymir/CMakeLists.txt")
        message(WARNING "SATURN_STAGE_AUDIO: ${SATURN_SOUND_PLAYER_DIR} has "
            "no source (git submodule update --init --recursive "
            "tools/saturn/sound-player): the PS1 stand-in sounds play")
    else()
        include(ExternalProject)
        set(player_dir "${CMAKE_BINARY_DIR}/saturn-sound-player")
        # a host program: no toolchain file, also in Switch and Windows builds
        ExternalProject_Add(saturn_sound_player
            SOURCE_DIR "${SATURN_SOUND_PLAYER_DIR}"
            BINARY_DIR "${player_dir}"
            CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
            BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target saturn-sfx
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${player_dir}/saturn-sfx")
        add_custom_target(saturn_stage_audio ALL
            COMMAND ${SATURN_GDN_PYTHON} -B
                    "${CMAKE_SOURCE_DIR}/tools/saturn/saturn_audio.py"
                    --disc "${CMAKE_SOURCE_DIR}/disks/saturn"
                    --data "${CMAKE_BINARY_DIR}/data"
                    --player "${player_dir}/saturn-sfx"
                    --cargo "${SATURN_STAGE_CARGO_BIN}"
                    --assets "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
            DEPENDS saturn_sound_player
            VERBATIM
            COMMENT "Converting the Saturn stage sounds and boss track")
    endif()
endif()
