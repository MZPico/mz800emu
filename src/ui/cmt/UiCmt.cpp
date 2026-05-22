#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include <stdio.h>
#include <glib.h>
#include <vector>
#include <string>

// Lokalizace
#include "i18n.h"

#include "emulator/hw-generic/cmt/cmt.h"
#include "libs/cmtspeed/cmtspeed.h"
#include "hw-generic/cmt/cmtext_block.h"
#include "hw-generic/cmt/cmt_mzf.h"
#include "hw-generic/cmt/cmt_tap.h"

#include "UiCmt.hpp"

std::vector<TapeFileEntry_t> g_tapeFileList;

std::string UiCmt::getSpeedTxt(en_CMTEXT_BLOCK_TYPE bltype, en_CMTEXT_BLOCK_SPEED blspeed, en_CMTSPEED cmtspeed, bool get_rateiospeed)
{
    uint16_t base_bdspeed;

    if (blspeed == CMTEXT_BLOCK_SPEED_NONE)
    {
        return std::string("");
    };

    if (blspeed == CMTEXT_BLOCK_SPEED_DEFAULT)
    {
        return std::string(_("Default"));
    };

    if (bltype == CMTEXT_BLOCK_TYPE_MZF)
    {
        base_bdspeed = MZTAPE_DEFAULT_BDSPEED;
    }
    else if ((bltype == CMTEXT_BLOCK_TYPE_TAPHEADER) || (bltype == CMTEXT_BLOCK_TYPE_TAPDATA))
    {
        base_bdspeed = ZXTAPE_DEFAULT_BDSPEED;
    }
    else
    {
        return std::string(_("UNKNOWN"));
    };

    char buff[50];

    if (get_rateiospeed)
    {
        cmtspeed_get_ratiospeedtxt(buff, sizeof(buff), cmtspeed, base_bdspeed);
    }
    else
    {
        cmtspeed_get_speedtxt(buff, sizeof(buff), cmtspeed, base_bdspeed);
    };

    return std::string(buff);
}

std::vector<UiCmtSpeedList_t> createSpeedList(en_CMTEXT_BLOCK_TYPE bltype)
{
    const en_CMTSPEED *src_cmtspeed = nullptr;

    if (bltype == CMTEXT_BLOCK_TYPE_MZF)
    {
        src_cmtspeed = g_mztape_speed;
    }
    else if ((bltype == CMTEXT_BLOCK_TYPE_TAPHEADER) || (bltype == CMTEXT_BLOCK_TYPE_TAPDATA))
    {
        src_cmtspeed = g_zxtape_speed;
    }
    else
    {
        return std::vector<UiCmtSpeedList_t>();
    };

    std::vector<UiCmtSpeedList_t> speed_list;
    while (*src_cmtspeed != CMTSPEED_NONE)
    {
        std::string speed_txt = UiCmt::getSpeedTxt(bltype, CMTEXT_BLOCK_SPEED_SET, *src_cmtspeed, true);
        speed_list.push_back({*src_cmtspeed, speed_txt});
        src_cmtspeed++;
    };

    return speed_list;
}

std::vector<UiCmtSpeedList_t> UiCmt::getMzSpeedList(void)
{
    return createSpeedList(CMTEXT_BLOCK_TYPE_MZF);
}

std::vector<UiCmtSpeedList_t> UiCmt::getZxSpeedList(void)
{
    return createSpeedList(CMTEXT_BLOCK_TYPE_TAPHEADER);
}

std::vector<UiCmtSpeedList_t> UiCmt::getMzSpeedListDefault(void)
{
    std::vector<UiCmtSpeedList_t> speed_list = UiCmt::getMzSpeedList();
    std::vector<UiCmtSpeedList_t> default_speed_list;

    std::string speed_txt = getSpeedTxt(CMTEXT_BLOCK_TYPE_MZF, CMTEXT_BLOCK_SPEED_DEFAULT, CMTSPEED_NONE, true);
    default_speed_list.push_back({CMTSPEED_NONE, speed_txt});

    // zbytek zkopirujeme ze speed_list
    for (const auto &item : speed_list)
    {
        default_speed_list.push_back(item);
    };

    return default_speed_list;
}

int UiCmt::getLengthInSec(en_CMTEXT_BLOCK_TYPE bltype, en_CMTEXT_BLOCK_SPEED blspeed, en_CMTSPEED cmtspeed, int fsize)
{
    uint16_t base_bdspeed;

    if (blspeed == CMTEXT_BLOCK_SPEED_NONE)
    {
        return -1;
    };

    if (bltype == CMTEXT_BLOCK_TYPE_MZF)
    {
        base_bdspeed = MZTAPE_DEFAULT_BDSPEED;
    }
    else if ((bltype == CMTEXT_BLOCK_TYPE_TAPHEADER) || (bltype == CMTEXT_BLOCK_TYPE_TAPDATA))
    {
        base_bdspeed = ZXTAPE_DEFAULT_BDSPEED;
    }
    else
    {
        // UNKNOWN
        return -1;
    };

    if (cmtspeed == CMTSPEED_NONE)
    {
        cmtspeed = CMTSPEED_1_1;
    };

    uint16_t bdspeed = cmtspeed_get_bdspeed(cmtspeed, base_bdspeed);

    return (fsize * 9) / bdspeed;
}

void UiCmt::updateTapeFilelist(void)
{
    g_tapeFileList.clear();

    if (!CMT_TEST_FILLED)
    {
        return;
    };

    st_CMTEXT_CONTAINER *container = cmtext_get_container(g_cmt.ext);

    if (cmtext_container_get_type(container) != CMTEXT_CONTAINER_TYPE_SIMPLE_TAPE)
    {
        return;
    };

    int count_blocks = cmtext_container_get_count_blocks(container);
    int play_block = cmtext_block_get_block_id(g_cmt.ext->block);

    int i;
    for (i = 0; i < count_blocks; i++)
    {
        gboolean ready = (i == play_block) ? TRUE : FALSE;
        gboolean played = (ready && (CMT_TEST_PLAY)) ? TRUE : FALSE;
        gboolean paused = (played && CMT_TEST_PAUSED) ? TRUE : FALSE;

        en_CMTEXT_BLOCK_TYPE bltype = cmtext_container_get_block_type(container, i);

        const char *ftype_txt;
        int ftype = cmtext_container_get_block_ftype(container, i);
        if ((bltype == CMTEXT_BLOCK_TYPE_TAPHEADER) || (bltype == CMTEXT_BLOCK_TYPE_TAPDATA))
        {
            ftype_txt = cmttap_get_block_code_txt((en_CMTTAP_HEADER_CODE)ftype);
        }
        else
        {
            char ftype_num_txt[5];
            ftype_num_txt[0] = 0x00;
            if (ftype != -1)
            {
                snprintf(ftype_num_txt, sizeof(ftype_num_txt), "0x%02X", ftype);
            };
            ftype_txt = ftype_num_txt;
        };

        int fsize = cmtext_container_get_block_fsize(container, i);
        char fsize_txt[7];
        fsize_txt[0] = 0x00;
        if (fsize != -1)
        {
            snprintf(fsize_txt, sizeof(fsize_txt), "0x%04X", fsize);
        };

        int fstrt = cmtext_container_get_block_fstrt(container, i);
        char fstrt_txt[7];
        fstrt_txt[0] = 0x00;
        if (fstrt != -1)
        {
            snprintf(fstrt_txt, sizeof(fstrt_txt), "0x%04X", fstrt);
        };

        int fexec = cmtext_container_get_block_fexec(container, i);
        char fexec_txt[7];
        fexec_txt[0] = 0x00;
        if (fexec != -1)
        {
            snprintf(fexec_txt, sizeof(fexec_txt), "0x%04X", fexec);
        };

        en_CMTEXT_BLOCK_SPEED blspeed = cmtext_container_get_block_speed(container, i);

        gboolean cmtspeed_mz_visible = ((bltype == CMTEXT_BLOCK_TYPE_MZF) && (blspeed != CMTEXT_BLOCK_SPEED_NONE)) ? TRUE : FALSE;
        gboolean cmtspeed_zx_visible = (((bltype == CMTEXT_BLOCK_TYPE_TAPHEADER) || (bltype == CMTEXT_BLOCK_TYPE_TAPDATA)) && (blspeed != CMTEXT_BLOCK_SPEED_NONE)) ? TRUE : FALSE;
        // gboolean cmtspeed_sensitive = (played) ? FALSE : TRUE;

        en_CMTSPEED cmtspeed = cmtext_container_get_block_cmt_speed(container, i);

        std::string cmtspeed_txt = UiCmt::getSpeedTxt(bltype, blspeed, cmtspeed, false);

        GString *gs = g_string_new(0);
        int length = UiCmt::getLengthInSec(bltype, blspeed, cmtspeed, fsize);
        if (length > 0)
        {
            g_string_printf(gs, "%02d:%02d", (length / 60), (length % 60));
        };

        en_PLAY_STATE play_state = PLAY_STATE_NONE;

        if (paused)
        {
            play_state = PLAY_STATE_PAUSED;
        }
        else if (played)
        {
            play_state = PLAY_STATE_PLAYING;
        }
        else if (ready)
        {
            play_state = PLAY_STATE_STOPPED;
        };

        TapeFileEntry_t entry;
        entry.id = i;
        entry.ready = ready;
        entry.played = played;
        entry.paused = paused;
        entry.play_state = play_state;
        entry.name = cmtext_container_get_block_fname(container, i);
        entry.ftype_txt = ftype_txt;
        entry.fsize_txt = fsize_txt;
        entry.fstrt_txt = fstrt_txt;
        entry.fexec_txt = fexec_txt;
        entry.block_type = bltype;
        entry.block_speed = blspeed;
        entry.cmtspeed = cmtspeed;
        entry.cmtspeed_txt = cmtspeed_txt;

        if (cmtspeed_mz_visible)
        {
            entry.cmtspeed_type = CMT_SPEED_TYPE_MZ;
        }
        else if (cmtspeed_zx_visible)
        {
            entry.cmtspeed_type = CMT_SPEED_TYPE_ZX;
        }
        else
        {
            entry.cmtspeed_type = CMT_SPEED_TYPE_NONE;
        };

        entry.length_txt = gs->str;
        g_string_free(gs, TRUE);
        g_tapeFileList.push_back(entry);
    };
}
