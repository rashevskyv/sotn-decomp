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

# The other arguments are the stage's own sources (src/st/<name>/*.c)
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
        "${asset_dir}/masonry.pal.bin"
        "${asset_dir}/music/audio.xa"
        "${asset_dir}/music/audio.cue"
        "${asset_dir}/music/xa_config.h"
        "${asset_dir}/music/xa_manifest.json")
    add_custom_command(
        OUTPUT ${outputs}
        COMMAND ${SATURN_STAGE_CARGO_BIN} run --release
                --manifest-path "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
                -- map convert-psx
                "${disc}/${source}.PRG"
                "${disc}/${source}.MAP"
                "${asset_dir}"
                --scale-model "${SATURN_STAGE_SCALE_MODEL}"
        COMMAND ${SATURN_STAGE_CARGO_BIN} run --release
                --manifest-path "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
                -- audio export-xa
                "${disc}/SD/${music}.PCM"
                "${asset_dir}/music"
                --file-number 25
                --channel 0
        DEPENDS
            ${SATURN_STAGE_CONVERTER_SOURCES}
            "${CMAKE_SOURCE_DIR}/tools/saturn/assets/Cargo.toml"
            "${disc}/${source}.PRG"
            "${disc}/${source}.MAP"
            "${disc}/${source}.CHR"
            "${disc}/SD/${music}.PCM"
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
add_saturn_stage(rstage15 RSTAGE15 SD29 TEST_RSTAGE15)
add_saturn_stage(rstage16 RSTAGE16 SD2C TEST_RSTAGE16)
