/*
 * File:   psg.c
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 23. července 2015, 7:33
 *
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */

/* Emulace PSG - SN76489AN */

#include "mzarch/mzarch_config.h"

#include "psg.h"
#include "hw-generic/gdg/gdg.h"
#include "audio.h"
#include "mzarch/mzarch.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
#include "debugger/trace/hwlog.h"
#include "debugger/bp_event.h"
#endif

st_PSG_MODULE g_psg_module;

void psg_instance_init(st_PSG *psg)
{
    psg->channel[PSG_CHANNEL_0].type = PSG_CHTYPE_TONE;
    psg->channel[PSG_CHANNEL_1].type = PSG_CHTYPE_TONE;
    psg->channel[PSG_CHANNEL_2].type = PSG_CHTYPE_TONE;

    psg->channel[PSG_CHANNEL_3].type = PSG_CHTYPE_NOISE;
    psg->channel[PSG_CHANNEL_3].noise.shiftregister = 1 << 15;

    for (unsigned i = 0; i < PSG_CHANNELS_COUNT; i++)
    {
        psg->channel[i].attn = PSG_OUT_OFF;
    };
}

void psg_init(bool stereo)
{
    g_psg_module.stereo = stereo;
    psg_instance_init(&g_psg_module.psg[0]);
    if (stereo)
        psg_instance_init(&g_psg_module.psg[1]);
}

/* Zapíše byte do registrů jedné PSG instance */
static inline void psg_write_byte_core(st_PSG *psg, uint8_t value, uint64_t total_ticks)
{
    unsigned latch, attn, cs;

    latch = value & (1 << 7);

    if (latch)
    {
        cs = (value >> 5) & 0x03;
        attn = value & (1 << 4);
        psg->latch_cs = cs;
        psg->latch_attn = attn;
    }
    else
    {
        cs = psg->latch_cs;
        attn = psg->latch_attn;
    };

    st_PSG_CHANNEL *channel = &psg->channel[cs];

    if (attn)
    {
        en_ATTENUATOR new_attn = value & 0x0f;
        if (new_attn != channel->attn)
        {
            audio_log_fill_psg(total_ticks);
            channel->attn = new_attn;
        };
    }
    else if ((latch) && (channel->type == PSG_CHTYPE_TONE))
    {
        channel->tone.latch_divider = value & 0x0f;
    }
    else
    {
        if (channel->type == PSG_CHTYPE_TONE)
        {
            unsigned new_divider = (value << 4) | channel->tone.latch_divider;
            if (new_divider != channel->tone.divider)
            {
                audio_log_fill_psg(total_ticks);
                channel->tone.divider = new_divider;
            };
        }
        else
        {
            en_NOISE_DIV_TYPE new_div_type = value & 0x03;
            en_NOISE_TYPE new_type = (value >> 2) & 1;
            if ((new_div_type != channel->noise.div_type) || (new_type != channel->noise.type))
            {
                audio_log_fill_psg(total_ticks);
                channel->noise.div_type = new_div_type;
                channel->noise.type = new_type;
            };
        };
    };
}

void psg_write_byte(unsigned channel, uint8_t value)
{
    mzarch_main_insideop_iorq_psg_write();
    uint64_t total_ticks = gdg_compute_total_ticks(g_gdg.total_elapsed.ticks);

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    /* trace-suite hwlog: zaznamenat write do PSG datového portu.
     *
     * Payload (per HW-log_format_CZ.md):
     *   [0] = raw byte zapsaný uživatelem
     *   [1] = channel mask (mono = 0x01, stereo bitmask PSG_CH_LEFT/RIGHT)
     *   [2] = stereo flag (0 = mono, 1 = stereo)
     *   [3..5] = rezervováno
     *
     * Decoded info (latch_cs, attn flag, čteno tone vs noise vs attn)
     * dopočítá externí parser z initial state + sumace eventů.
     */
    if ( TEST_TRACE_HWLOG_DISPATCH ) {
        uint8_t payload[ 6 ] = {
            value,
            (uint8_t)( channel & 0xff ),
            (uint8_t)( g_psg_module.stereo ? 1 : 0 ),
            0, 0, 0
        };
        hwlog_record ( HWLOG_CHIP_PSG, HWLOG_PSG_REGISTER_WRITE, payload );
    }
    /* HWE: psg:reg_write vyřazený (= IORQ_W na PSG port je expressivnější).
     * psg:int_pa5 vyřazený - PSG INT je viditelný přes irq:pioz80_b nebo
     * IRQ_SIG. */
#endif

    if (!g_psg_module.stereo)
    {
        /* Mono režim — channel se ignoruje */
        psg_write_byte_core(&g_psg_module.psg[0], value, total_ticks);
        return;
    }

    /* Stereo režim — dispatch podle channel bitmask.
     * Obě podmínky jsou nezávislé, aby PSG_CH_LEFT | PSG_CH_RIGHT
     * správně zapsal do obou PSG (broadcast port). */
    if (channel & PSG_CH_LEFT)
    {
        psg_write_byte_core(&g_psg_module.psg[0], value, total_ticks);
    }
    if (channel & PSG_CH_RIGHT)
    {
        psg_write_byte_core(&g_psg_module.psg[1], value, total_ticks);
    }
}

void psg_instance_step(st_PSG *psg)
{
    for (unsigned cs = 0; cs < PSG_CHANNELS_COUNT; cs++)
    {
        st_PSG_CHANNEL *channel = &psg->channel[cs];

        if (channel->attn != PSG_OUT_OFF)
        {
            if (channel->type == PSG_CHTYPE_TONE)
            {
                /* tone */
                st_PSG_TONE *tone = &channel->tone;
                if (tone->divider < 2)
                {
                    channel->output_signal = 1;
                }
                else if (0 == channel->timer--)
                {
                    channel->timer = tone->divider - 1;
                    channel->output_signal = (~channel->output_signal) & 0x01;
                };
            }
            else
            {
                /* noise */
                unsigned bit0, bit3;
                st_PSG_NOISE *noise = &channel->noise;
                if ((noise->div_type == 0x03) && (psg->channel[PSG_CHANNEL_2].tone.divider < 2))
                {
                    channel->output_signal = 1;
                }
                else if (0 == channel->timer--)
                {
                    if (noise->div_type == NOISE_DIV_TYPE3)
                    {
                        channel->timer = psg->channel[PSG_CHANNEL_2].tone.divider - 1;
                    }
                    else
                    {
                        channel->timer = (0x10 << noise->div_type) - 1;
                    };

                    bit0 = noise->shiftregister & 0x01;

                    if (noise->last_noise_type != noise->type)
                    {
                        noise->shiftregister = 1 << 15;
                        noise->last_noise_type = noise->type;
                    }
                    else if (noise->type == NOISE_TYPE_WHITE)
                    {
                        bit3 = (noise->shiftregister >> 3) & 0x01;
                        noise->shiftregister = noise->shiftregister >> 1;
                        noise->shiftregister |= (bit0 ^ bit3) << 15;
                    }
                    else
                    {
                        noise->shiftregister = noise->shiftregister >> 1;
                        noise->shiftregister |= bit0 << 15;
                    };
                    channel->output_signal = noise->shiftregister & 0x01;
                };
            };
        };
    };
}

void psg_step(void)
{
    psg_instance_step(&g_psg_module.psg[0]);
    if (g_psg_module.stereo)
        psg_instance_step(&g_psg_module.psg[1]);
}

/* ========================================================================= */
/* Debug/UI mirror API - side-effect free čtení stavu PSG.                   */
/*                                                                           */
/* Žádný z těchto getterů NEMODIFIKUJE st_PSG. Bezpečné pro volání z UI      */
/* vlákna v paralelním běhu emu vlákna (single-byte / single-field snapshot, */
/* x86 atomic).                                                              */
/* ========================================================================= */

unsigned psg_mirror_latch_cs(const st_PSG *psg)
{
    return psg->latch_cs;
}

unsigned psg_mirror_latch_attn(const st_PSG *psg)
{
    return psg->latch_attn;
}

en_PSG_CHTYPE psg_mirror_channel_type(const st_PSG *psg, unsigned ch)
{
    return psg->channel[ch].type;
}

unsigned psg_mirror_channel_attn(const st_PSG *psg, unsigned ch)
{
    return (unsigned)psg->channel[ch].attn;
}

uint16_t psg_mirror_channel_tone_divider(const st_PSG *psg, unsigned ch)
{
    return (uint16_t)(psg->channel[ch].tone.divider & 0x3FFu);
}

en_NOISE_DIV_TYPE psg_mirror_channel_noise_div_type(const st_PSG *psg, unsigned ch)
{
    return psg->channel[ch].noise.div_type;
}

en_NOISE_TYPE psg_mirror_channel_noise_type(const st_PSG *psg, unsigned ch)
{
    return psg->channel[ch].noise.type;
}
