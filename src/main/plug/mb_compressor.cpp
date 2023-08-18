/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-mb-compressor
 * Created on: 3 авг. 2021 г.
 *
 * lsp-plugins-mb-compressor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-mb-compressor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-mb-compressor. If not, see <https://www.gnu.org/licenses/>.
 */

#include <private/plugins/mb_compressor.h>
#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/bits.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/misc/envelope.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/shared/id_colors.h>

#define MBC_BUFFER_SIZE         0x400

namespace lsp
{
    namespace plugins
    {
        static plug::IPort *TRACE_PORT(plug::IPort *p)
        {
            lsp_trace("  port id=%s", (p)->metadata()->id);
            return p;
        }

        //-------------------------------------------------------------------------
        // Plugin factory
        typedef struct plugin_settings_t
        {
            const meta::plugin_t   *metadata;
            bool                    sc;
            uint8_t                 mode;
        } plugin_settings_t;

        static const meta::plugin_t *plugins[] =
        {
            &meta::mb_compressor_mono,
            &meta::mb_compressor_stereo,
            &meta::mb_compressor_lr,
            &meta::mb_compressor_ms,
            &meta::sc_mb_compressor_mono,
            &meta::sc_mb_compressor_stereo,
            &meta::sc_mb_compressor_lr,
            &meta::sc_mb_compressor_ms
        };

        static const plugin_settings_t plugin_settings[] =
        {
            { &meta::mb_compressor_mono,        false, mb_compressor::MBCM_MONO         },
            { &meta::mb_compressor_stereo,      false, mb_compressor::MBCM_STEREO       },
            { &meta::mb_compressor_lr,          false, mb_compressor::MBCM_LR           },
            { &meta::mb_compressor_ms,          false, mb_compressor::MBCM_MS           },
            { &meta::sc_mb_compressor_mono,     true,  mb_compressor::MBCM_MONO         },
            { &meta::sc_mb_compressor_stereo,   true,  mb_compressor::MBCM_STEREO       },
            { &meta::sc_mb_compressor_lr,       true,  mb_compressor::MBCM_LR           },
            { &meta::sc_mb_compressor_ms,       true,  mb_compressor::MBCM_MS           },

            { NULL, 0, false }
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            for (const plugin_settings_t *s = plugin_settings; s->metadata != NULL; ++s)
                if (s->metadata == meta)
                    return new mb_compressor(s->metadata, s->sc, s->mode);
            return NULL;
        }

        static plug::Factory factory(plugin_factory, plugins, 8);

        //-------------------------------------------------------------------------
        mb_compressor::mb_compressor(const meta::plugin_t *metadata, bool sc, size_t mode):
            plug::Module(metadata)
        {
            nMode           = mode;
            bSidechain      = sc;
            bEnvUpdate      = true;
            enXOver         = XOVER_MODERN;
            bStereoSplit    = false;
            nEnvBoost       = meta::mb_compressor_metadata::FB_DEFAULT;
            vChannels       = NULL;
            fInGain         = GAIN_AMP_0_DB;
            fDryGain        = GAIN_AMP_M_INF_DB;
            fWetGain        = GAIN_AMP_0_DB;
            fZoom           = GAIN_AMP_0_DB;
            pData           = NULL;
            vTr             = NULL;
            vPFc            = NULL;
            vRFc            = NULL;
            vFreqs          = NULL;
            vCurve          = NULL;
            vIndexes        = NULL;
            pIDisplay       = NULL;
            vSc[0]          = NULL;
            vSc[1]          = NULL;
            vAnalyze[0]     = NULL;
            vAnalyze[1]     = NULL;
            vAnalyze[2]     = NULL;
            vAnalyze[3]     = NULL;
            vBuffer         = NULL;
            vEnv            = NULL;

            pBypass         = NULL;
            pMode           = NULL;
            pInGain         = NULL;
            pDryGain        = NULL;
            pWetGain        = NULL;
            pOutGain        = NULL;
            pReactivity     = NULL;
            pShiftGain      = NULL;
            pZoom           = NULL;
            pEnvBoost       = NULL;
            pStereoSplit    = NULL;
        }

        mb_compressor::~mb_compressor()
        {
        }

        bool mb_compressor::compare_bands_for_sort(const comp_band_t *b1, const comp_band_t *b2)
        {
            if (b1->fFreqStart != b2->fFreqStart)
                return (b1->fFreqStart > b2->fFreqStart);
            return b1 < b2;
        }

        dspu::compressor_mode_t mb_compressor::decode_mode(int mode)
        {
            switch (mode)
            {
                case meta::mb_compressor_metadata::CM_DOWNWARD: return dspu::CM_DOWNWARD;
                case meta::mb_compressor_metadata::CM_UPWARD:   return dspu::CM_UPWARD;
                case meta::mb_compressor_metadata::CM_BOOSTING: return dspu::CM_BOOSTING;
                default: break;
            }
            return dspu::CM_DOWNWARD;
        }

        dspu::sidechain_source_t mb_compressor::decode_sidechain_source(int source, bool split, size_t channel)
        {
            if (!split)
            {
                switch (source)
                {
                    case 0: return dspu::SCS_MIDDLE;
                    case 1: return dspu::SCS_SIDE;
                    case 2: return dspu::SCS_LEFT;
                    case 3: return dspu::SCS_RIGHT;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }

            if (channel == 0)
            {
                switch (source)
                {
                    case 0: return dspu::SCS_LEFT;
                    case 1: return dspu::SCS_RIGHT;
                    case 2: return dspu::SCS_MIDDLE;
                    case 3: return dspu::SCS_SIDE;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }
            else
            {
                switch (source)
                {
                    case 0: return dspu::SCS_RIGHT;
                    case 1: return dspu::SCS_LEFT;
                    case 2: return dspu::SCS_SIDE;
                    case 3: return dspu::SCS_MIDDLE;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }

            return dspu::SCS_MIDDLE;
        }

        void mb_compressor::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Initialize plugin
            plug::Module::init(wrapper, ports);

            // Determine number of channels
            size_t channels     = (nMode == MBCM_MONO) ? 1 : 2;

            // Allocate channels
            vChannels       = new channel_t[channels];
            if (vChannels == NULL)
                return;

            // Initialize analyzer
            size_t an_cid       = 0;
            if (!sAnalyzer.init(2*channels, meta::mb_compressor_metadata::FFT_RANK,
                                MAX_SAMPLE_RATE, meta::mb_compressor_metadata::REFRESH_RATE))
                return;

            sAnalyzer.set_rank(meta::mb_compressor_metadata::FFT_RANK);
            sAnalyzer.set_activity(false);
            sAnalyzer.set_envelope(dspu::envelope::WHITE_NOISE);
            sAnalyzer.set_window(meta::mb_compressor_metadata::FFT_WINDOW);
            sAnalyzer.set_rate(meta::mb_compressor_metadata::REFRESH_RATE);

            size_t filter_mesh_size = align_size(meta::mb_compressor_metadata::FFT_MESH_POINTS * sizeof(float), DEFAULT_ALIGN);

            // Allocate float buffer data
            size_t to_alloc =
                    // Global buffers
                    2 * filter_mesh_size + // vTr (both complex and real)
                    2 * filter_mesh_size + // vFc (both complex and real)
                    2 * filter_mesh_size + // vSig (both complex and real)
                    meta::mb_compressor_metadata::CURVE_MESH_SIZE * sizeof(float) + // Curve
                    meta::mb_compressor_metadata::FFT_MESH_POINTS * sizeof(float) + // vFreqs array
                    meta::mb_compressor_metadata::FFT_MESH_POINTS * sizeof(uint32_t) + // vIndexes array
                    MBC_BUFFER_SIZE * sizeof(float) + // Global vBuffer for band signal processing
                    MBC_BUFFER_SIZE * sizeof(float) + // Global vEnv for band signal processing
                    // Channel buffers
                    (
                        MBC_BUFFER_SIZE * sizeof(float) + // Global vSc[] for each channel
                        2 * filter_mesh_size + // vTr of each channel
                        filter_mesh_size + // vTrMem of each channel
                        MBC_BUFFER_SIZE * sizeof(float) + // vInBuffer for each channel
                        MBC_BUFFER_SIZE * sizeof(float) + // vBuffer for each channel
                        MBC_BUFFER_SIZE * sizeof(float) + // vScBuffer for each channel
                        ((bSidechain) ? MBC_BUFFER_SIZE * sizeof(float) : 0) + // vExtScBuffer for each channel
                        MBC_BUFFER_SIZE * sizeof(float) * 2 + // vInAnalyze + vOutAnalyze for each channel
                        // Band buffers
                        (
                            MBC_BUFFER_SIZE * sizeof(float) + // vBuffer of each band
                            MBC_BUFFER_SIZE * sizeof(float) + // vVCA of each band
                            meta::mb_compressor_metadata::FFT_MESH_POINTS * 2 * sizeof(float) // vTr transfer function for each band
                        ) * meta::mb_compressor_metadata::BANDS_MAX
                    ) * channels;

            uint8_t *ptr    = alloc_aligned<uint8_t>(pData, to_alloc);
            if (ptr == NULL)
                return;
            lsp_guard_assert(uint8_t *save   = ptr);

            // Remember the pointer to frequencies buffer
            vTr             = reinterpret_cast<float *>(ptr);
            ptr            += filter_mesh_size * 2;
            vPFc             = reinterpret_cast<float *>(ptr);
            ptr            += filter_mesh_size * 2;
            vRFc            = reinterpret_cast<float *>(ptr);
            ptr            += filter_mesh_size * 2;
            vFreqs          = reinterpret_cast<float *>(ptr);
            ptr            += meta::mb_compressor_metadata::FFT_MESH_POINTS * sizeof(float);
            vCurve          = reinterpret_cast<float *>(ptr);
            ptr            += meta::mb_compressor_metadata::CURVE_MESH_SIZE * sizeof(float);
            vIndexes        = reinterpret_cast<uint32_t *>(ptr);
            ptr            += meta::mb_compressor_metadata::FFT_MESH_POINTS * sizeof(uint32_t);
            vSc[0]          = reinterpret_cast<float *>(ptr);
            ptr            += MBC_BUFFER_SIZE * sizeof(float);
            if (channels > 1)
            {
                vSc[1]          = reinterpret_cast<float *>(ptr);
                ptr            += MBC_BUFFER_SIZE * sizeof(float);
            }
            else
                vSc[1]          = NULL;
            vBuffer         = reinterpret_cast<float *>(ptr);
            ptr            += MBC_BUFFER_SIZE * sizeof(float);
            vEnv            = reinterpret_cast<float *>(ptr);
            ptr            += MBC_BUFFER_SIZE * sizeof(float);

            // Initialize filters according to number of bands
            if (sFilters.init(meta::mb_compressor_metadata::BANDS_MAX * channels) != STATUS_OK)
                return;
            size_t filter_cid = 0;

            // Initialize channels
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                c->sBypass.construct();
                c->sEnvBoost[0].construct();
                c->sEnvBoost[1].construct();
                c->sDelay.construct();
                c->sDryDelay.construct();
                c->sAnDelay.construct();
                c->sDryEq.construct();
                c->sFFTXOver.construct();

                c->sDryEq.init(meta::mb_compressor_metadata::BANDS_MAX-1, 0);
                c->sDryEq.set_mode(dspu::EQM_IIR);

                if (!c->sEnvBoost[0].init(NULL))
                    return;
                if (bSidechain)
                {
                    if (!c->sEnvBoost[1].init(NULL))
                        return;
                }

                c->nPlanSize    = 0;
                c->vIn          = NULL;
                c->vOut         = NULL;
                c->vScIn        = NULL;

                c->vInBuffer    = reinterpret_cast<float *>(ptr);
                ptr            += MBC_BUFFER_SIZE * sizeof(float);
                c->vBuffer      = reinterpret_cast<float *>(ptr);
                ptr            += MBC_BUFFER_SIZE * sizeof(float);
                c->vScBuffer    = reinterpret_cast<float *>(ptr);
                ptr            += MBC_BUFFER_SIZE * sizeof(float);
                c->vExtScBuffer = NULL;
                if (bSidechain)
                {
                    c->vExtScBuffer = reinterpret_cast<float *>(ptr);
                    ptr            += MBC_BUFFER_SIZE * sizeof(float);
                }
                c->vTr          = reinterpret_cast<float *>(ptr);
                ptr            += 2 * filter_mesh_size;
                c->vTrMem       = reinterpret_cast<float *>(ptr);
                ptr            += filter_mesh_size;
                c->vInAnalyze   = reinterpret_cast<float *>(ptr);
                ptr            += MBC_BUFFER_SIZE * sizeof(float);
                c->vOutAnalyze  = reinterpret_cast<float *>(ptr);
                ptr            += MBC_BUFFER_SIZE * sizeof(float);

                c->nAnInChannel = an_cid++;
                c->nAnOutChannel= an_cid++;
                vAnalyze[c->nAnInChannel]   = c->vInAnalyze;
                vAnalyze[c->nAnOutChannel]  = c->vOutAnalyze;

                c->bInFft       = false;
                c->bOutFft      = false;

                c->pIn          = NULL;
                c->pOut         = NULL;
                c->pScIn        = NULL;
                c->pFftIn       = NULL;
                c->pFftInSw     = NULL;
                c->pFftOut      = NULL;
                c->pFftOutSw    = NULL;

                c->pAmpGraph    = NULL;
                c->pInLvl       = NULL;
                c->pOutLvl      = NULL;

                // Initialize bands
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b  = &c->vBands[j];

                    if (!b->sSC.init(channels, meta::mb_compressor_metadata::REACTIVITY_MAX))
                        return;
                    if (!b->sPassFilter.init(NULL))
                        return;
                    if (!b->sRejFilter.init(NULL))
                        return;
                    if (!b->sAllFilter.init(NULL))
                        return;

                    // Initialize sidechain equalizers
                    b->sEQ[0].init(2, 6);
                    b->sEQ[0].set_mode(dspu::EQM_IIR);
                    if (channels > 1)
                    {
                        b->sEQ[1].init(2, 6);
                        b->sEQ[1].set_mode(dspu::EQM_IIR);
                    }

                    b->vBuffer      = reinterpret_cast<float *>(ptr);
                    ptr            += MBC_BUFFER_SIZE * sizeof(float);
                    b->vVCA         = reinterpret_cast<float *>(ptr);
                    ptr            += MBC_BUFFER_SIZE * sizeof(float);
                    b->vTr          = reinterpret_cast<float *>(ptr);
                    ptr            += meta::mb_compressor_metadata::FFT_MESH_POINTS * sizeof(float) * 2;

                    b->fScPreamp    = GAIN_AMP_0_DB;

                    b->fFreqStart   = 0.0f;
                    b->fFreqEnd     = 0.0f;

                    b->fFreqHCF     = 0.0f;
                    b->fFreqLCF     = 0.0f;
                    b->fMakeup      = GAIN_AMP_0_DB;
                    b->fGainLevel   = GAIN_AMP_0_DB;
                    b->bEnabled     = j < meta::mb_compressor_metadata::BANDS_DFL;
                    b->bCustHCF     = false;
                    b->bCustLCF     = false;
                    b->bMute        = false;
                    b->bSolo        = false;
                    b->bExtSc       = false;
                    b->nSync        = S_ALL;
                    b->nFilterID    = filter_cid++;

                    b->pExtSc       = NULL;
                    b->pScSource    = NULL;
                    b->pScSpSource  = NULL;
                    b->pScMode      = NULL;
                    b->pScLook      = NULL;
                    b->pScReact     = NULL;
                    b->pScPreamp    = NULL;
                    b->pScLpfOn     = NULL;
                    b->pScHpfOn     = NULL;
                    b->pScLcfFreq   = NULL;
                    b->pScHcfFreq   = NULL;
                    b->pScFreqChart = NULL;

                    b->pMode        = NULL;
                    b->pEnable      = NULL;
                    b->pSolo        = NULL;
                    b->pMute        = NULL;
                    b->pAttLevel    = NULL;
                    b->pAttTime     = NULL;
                    b->pRelLevel    = NULL;
                    b->pRelTime     = NULL;
                    b->pRatio       = NULL;
                    b->pKnee        = NULL;
                    b->pBThresh     = NULL;
                    b->pBoost       = NULL;
                    b->pMakeup      = NULL;
                    b->pFreqEnd     = NULL;
                    b->pCurveGraph  = NULL;
                    b->pRelLevelOut = NULL;
                    b->pEnvLvl      = NULL;
                    b->pCurveLvl    = NULL;
                    b->pMeterGain   = NULL;
                }

                // Initialize split
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX-1; ++j)
                {
                    split_t *s      = &c->vSplit[j];

                    s->bEnabled     = false;
                    s->fFreq        = 0.0f;

                    s->pEnabled     = NULL;
                    s->pFreq        = NULL;
                }
            }

            lsp_assert(ptr <= &save[to_alloc]);

            // Bind ports
            size_t port_id              = 0;

            // Input ports
            lsp_trace("Binding input ports");
            for (size_t i=0; i<channels; ++i)
                vChannels[i].pIn        = TRACE_PORT(ports[port_id++]);

            // Input ports
            lsp_trace("Binding output ports");
            for (size_t i=0; i<channels; ++i)
                vChannels[i].pOut       = TRACE_PORT(ports[port_id++]);

            // Input ports
            if (bSidechain)
            {
                lsp_trace("Binding sidechain ports");
                for (size_t i=0; i<channels; ++i)
                    vChannels[i].pScIn      = TRACE_PORT(ports[port_id++]);
            }

            // Common ports
            lsp_trace("Binding common ports");
            pBypass                 = TRACE_PORT(ports[port_id++]);
            pMode                   = TRACE_PORT(ports[port_id++]);
            pInGain                 = TRACE_PORT(ports[port_id++]);
            pOutGain                = TRACE_PORT(ports[port_id++]);
            pDryGain                = TRACE_PORT(ports[port_id++]);
            pWetGain                = TRACE_PORT(ports[port_id++]);
            pReactivity             = TRACE_PORT(ports[port_id++]);
            pShiftGain              = TRACE_PORT(ports[port_id++]);
            pZoom                   = TRACE_PORT(ports[port_id++]);
            pEnvBoost               = TRACE_PORT(ports[port_id++]);
            TRACE_PORT(ports[port_id++]); // Skip band selector

            lsp_trace("Binding channel ports");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                if ((i == 0) || (nMode == MBCM_LR) || (nMode == MBCM_MS))
                    TRACE_PORT(ports[port_id++]); // Skip filter switch

                c->pAmpGraph            = TRACE_PORT(ports[port_id++]);
            }
            if (nMode == MBCM_STEREO)
                pStereoSplit            = TRACE_PORT(ports[port_id++]);

            lsp_trace("Binding meters");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                c->pFftInSw             = TRACE_PORT(ports[port_id++]);
                c->pFftOutSw            = TRACE_PORT(ports[port_id++]);
                c->pFftIn               = TRACE_PORT(ports[port_id++]);
                c->pFftOut              = TRACE_PORT(ports[port_id++]);
                c->pInLvl               = TRACE_PORT(ports[port_id++]);
                c->pOutLvl              = TRACE_PORT(ports[port_id++]);
            }

            // Split frequencies
            lsp_trace("Binding split frequencies");
            for (size_t i=0; i<channels; ++i)
            {
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX-1; ++j)
                {
                    split_t *s      = &vChannels[i].vSplit[j];

                    if ((i > 0) && (nMode == MBCM_STEREO))
                    {
                        split_t *sc     = &vChannels[0].vSplit[j];
                        s->pEnabled     = sc->pEnabled;
                        s->pFreq        = sc->pFreq;
                    }
                    else
                    {
                        s->pEnabled     = TRACE_PORT(ports[port_id++]);
                        s->pFreq        = TRACE_PORT(ports[port_id++]);
                    }
                }
            }

            // Compressor bands
            lsp_trace("Binding compressor bands");
            for (size_t i=0; i<channels; ++i)
            {
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b  = &vChannels[i].vBands[j];

                    if ((i > 0) && (nMode == MBCM_STEREO))
                    {
                        comp_band_t *sb     = &vChannels[0].vBands[j];

                        b->pExtSc       = sb->pExtSc;
                        b->pScSource    = sb->pScSource;
                        b->pScSpSource  = sb->pScSpSource;
                        b->pScMode      = sb->pScMode;
                        b->pScLook      = sb->pScLook;
                        b->pScReact     = sb->pScReact;
                        b->pScPreamp    = sb->pScPreamp;
                        b->pScLpfOn     = sb->pScLpfOn;
                        b->pScHpfOn     = sb->pScHpfOn;
                        b->pScLcfFreq   = sb->pScLcfFreq;
                        b->pScHcfFreq   = sb->pScHcfFreq;
                        b->pScFreqChart = sb->pScFreqChart;

                        b->pMode        = sb->pMode;
                        b->pEnable      = sb->pEnable;
                        b->pSolo        = sb->pSolo;
                        b->pMute        = sb->pMute;
                        b->pAttLevel    = sb->pAttLevel;
                        b->pAttTime     = sb->pAttTime;
                        b->pRelLevel    = sb->pRelLevel;
                        b->pRelTime     = sb->pRelTime;
                        b->pRatio       = sb->pRatio;
                        b->pKnee        = sb->pKnee;
                        b->pBThresh     = sb->pBThresh;
                        b->pBoost       = sb->pBoost;
                        b->pMakeup      = sb->pMakeup;

                        b->pFreqEnd     = sb->pFreqEnd;
                        b->pCurveGraph  = sb->pCurveGraph;
                        b->pRelLevelOut = sb->pRelLevelOut;
                    }
                    else
                    {
                        if (bSidechain)
                            b->pExtSc       = TRACE_PORT(ports[port_id++]);
                        if (nMode != MBCM_MONO)
                            b->pScSource    = TRACE_PORT(ports[port_id++]);
                        if (nMode == MBCM_STEREO)
                            b->pScSpSource  = TRACE_PORT(ports[port_id++]);

                        b->pScMode      = TRACE_PORT(ports[port_id++]);
                        b->pScLook      = TRACE_PORT(ports[port_id++]);
                        b->pScReact     = TRACE_PORT(ports[port_id++]);
                        b->pScPreamp    = TRACE_PORT(ports[port_id++]);
                        b->pScLpfOn     = TRACE_PORT(ports[port_id++]);
                        b->pScHpfOn     = TRACE_PORT(ports[port_id++]);
                        b->pScLcfFreq   = TRACE_PORT(ports[port_id++]);
                        b->pScHcfFreq   = TRACE_PORT(ports[port_id++]);
                        b->pScFreqChart = TRACE_PORT(ports[port_id++]);

                        b->pMode        = TRACE_PORT(ports[port_id++]);
                        b->pEnable      = TRACE_PORT(ports[port_id++]);
                        b->pSolo        = TRACE_PORT(ports[port_id++]);
                        b->pMute        = TRACE_PORT(ports[port_id++]);
                        b->pAttLevel    = TRACE_PORT(ports[port_id++]);
                        b->pAttTime     = TRACE_PORT(ports[port_id++]);
                        b->pRelLevel    = TRACE_PORT(ports[port_id++]);
                        b->pRelTime     = TRACE_PORT(ports[port_id++]);
                        b->pRatio       = TRACE_PORT(ports[port_id++]);
                        b->pKnee        = TRACE_PORT(ports[port_id++]);
                        b->pBThresh     = TRACE_PORT(ports[port_id++]);
                        b->pBoost       = TRACE_PORT(ports[port_id++]);
                        b->pMakeup      = TRACE_PORT(ports[port_id++]);

                        TRACE_PORT(ports[port_id++]); // Skip hue

                        b->pFreqEnd     = TRACE_PORT(ports[port_id++]);
                        b->pCurveGraph  = TRACE_PORT(ports[port_id++]);
                        b->pRelLevelOut = TRACE_PORT(ports[port_id++]);
                    }
                }
            }

            // Compressor band meters
            lsp_trace("Binding compressor band meters");
            for (size_t i=0; i<channels; ++i)
            {
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b  = &vChannels[i].vBands[j];

                    b->pEnvLvl      = TRACE_PORT(ports[port_id++]);
                    b->pCurveLvl    = TRACE_PORT(ports[port_id++]);
                    b->pMeterGain   = TRACE_PORT(ports[port_id++]);
                }
            }

            // Initialize curve (logarithmic) in range of -72 .. +24 db
            float delta = (meta::mb_compressor_metadata::CURVE_DB_MAX - meta::mb_compressor_metadata::CURVE_DB_MIN) / (meta::mb_compressor_metadata::CURVE_MESH_SIZE-1);
            for (size_t i=0; i<meta::mb_compressor_metadata::CURVE_MESH_SIZE; ++i)
                vCurve[i]   = dspu::db_to_gain(meta::mb_compressor_metadata::CURVE_DB_MIN + delta * i);
        }

        void mb_compressor::destroy()
        {
            // Determine number of channels
            size_t channels     = (nMode == MBCM_MONO) ? 1 : 2;

            // Destroy channels
            if (vChannels != NULL)
            {
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c    = &vChannels[i];

                    c->sEnvBoost[0].destroy();
                    c->sEnvBoost[1].destroy();
                    c->sDelay.destroy();
                    c->sDryDelay.destroy();
                    c->sAnDelay.destroy();
                    c->sDryEq.destroy();
                    c->sFFTXOver.destroy();

                    c->vBuffer      = NULL;

                    for (size_t i=0; i<meta::mb_compressor_metadata::BANDS_MAX; ++i)
                    {
                        comp_band_t *b  = &c->vBands[i];

                        b->sEQ[0].destroy();
                        b->sEQ[1].destroy();
                        b->sSC.destroy();
                        b->sScDelay.destroy();

                        b->sPassFilter.destroy();
                        b->sRejFilter.destroy();
                        b->sAllFilter.destroy();
                    }
                }

                delete [] vChannels;
                vChannels       = NULL;
            }

            // Destroy dynamic filters
            sFilters.destroy();

            // Destroy data
            if (pData != NULL)
                free_aligned(pData);

            if (pIDisplay != NULL)
            {
                pIDisplay->destroy();
                pIDisplay   = NULL;
            }

            // Destroy analyzer
            sAnalyzer.destroy();

            // Destroy plugin
            plug::Module::destroy();
        }

        void mb_compressor::update_settings()
        {
            dspu::filter_params_t fp;

            // Determine number of channels
            size_t channels     = (nMode == MBCM_MONO) ? 1 : 2;
            int active_channels = 0;
            size_t env_boost    = pEnvBoost->value();

            // Determine work mode: classic or modern
            xover_mode_t xover  = xover_mode_t(pMode->value());
            if (xover != enXOver)
            {
                enXOver             = xover;
                for (size_t i=0; i<channels; ++i)
                    vChannels[i].nPlanSize      = 0;
            }
            bStereoSplit        = (pStereoSplit != NULL) ? pStereoSplit->value() >= 0.5f : false;

            // Store gain
            float out_gain      = pOutGain->value();
            fInGain             = pInGain->value();
            fDryGain            = out_gain * pDryGain->value();
            fWetGain            = out_gain * pWetGain->value();
            fZoom               = pZoom->value();

            // Configure channels
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update bypass settings
                c->sBypass.set_bypass(pBypass->value());

                // Update frequency split bands
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX-1; ++j)
                {
                    split_t *s      = &c->vSplit[j];

                    bool enabled    = s->bEnabled;
                    s->bEnabled     = s->pEnabled->value() >= 0.5f;
                    if (enabled != s->bEnabled)
                        c->nPlanSize    = 0;

                    float v         = s->fFreq;
                    s->fFreq        = s->pFreq->value();
                    if (v != s->fFreq)
                        c->nPlanSize    = 0;
                }

                // Update analyzer settings
                c->bInFft       = c->pFftInSw->value() >= 0.5f;
                c->bOutFft      = c->pFftOutSw->value() >= 0.5f;

                sAnalyzer.enable_channel(c->nAnInChannel, c->bInFft);
                sAnalyzer.enable_channel(c->nAnOutChannel, c->pFftOutSw->value()  >= 0.5f);

                if (sAnalyzer.channel_active(c->nAnInChannel))
                    active_channels ++;
                if (sAnalyzer.channel_active(c->nAnOutChannel))
                    active_channels ++;

                // Update envelope boost filters
                if ((env_boost != nEnvBoost) || (bEnvUpdate))
                {
                    fp.fFreq        = meta::mb_compressor_metadata::FREQ_BOOST_MIN;
                    fp.fFreq2       = 0.0f;
                    fp.fGain        = 1.0f;
                    fp.fQuality     = 0.0f;

                    switch (env_boost)
                    {
                        case meta::mb_compressor_metadata::FB_BT_3DB:
                            fp.nType        = dspu::FLT_BT_RLC_ENVELOPE;
                            fp.nSlope       = 1;
                            break;
                        case meta::mb_compressor_metadata::FB_MT_3DB:
                            fp.nType        = dspu::FLT_MT_RLC_ENVELOPE;
                            fp.nSlope       = 1;
                            break;
                        case meta::mb_compressor_metadata::FB_BT_6DB:
                            fp.nType        = dspu::FLT_BT_RLC_ENVELOPE;
                            fp.nSlope       = 2;
                            break;
                        case meta::mb_compressor_metadata::FB_MT_6DB:
                            fp.nType        = dspu::FLT_MT_RLC_ENVELOPE;
                            fp.nSlope       = 2;
                            break;
                        case meta::mb_compressor_metadata::FB_OFF:
                        default:
                            fp.nType        = dspu::FLT_NONE;
                            fp.nSlope       = 1;
                            break;
                    }

                    c->sEnvBoost[0].update(fSampleRate, &fp);
                    if (bSidechain)
                        c->sEnvBoost[1].update(fSampleRate, &fp);
                }
            }

            // Update analyzer parameters
            sAnalyzer.set_reactivity(pReactivity->value());
            if (pShiftGain != NULL)
                sAnalyzer.set_shift(pShiftGain->value() * 100.0f);
            sAnalyzer.set_activity(active_channels > 0);

            // Update analyzer
            if (sAnalyzer.needs_reconfiguration())
            {
                sAnalyzer.reconfigure();
                sAnalyzer.get_frequencies(vFreqs, vIndexes, SPEC_FREQ_MIN, SPEC_FREQ_MAX, meta::mb_compressor_metadata::MESH_POINTS);
            }

            size_t latency = 0;
            bool solo_on = false;

            // Configure channels
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update compressor bands
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b  = &c->vBands[j];

                    float attack    = b->pAttLevel->value();
                    float release   = b->pRelLevel->value() * attack;
                    float makeup    = b->pMakeup->value();
                    dspu::compressor_mode_t mode = decode_mode(b->pMode->value());
                    bool enabled    = b->pEnable->value() >= 0.5f;
                    if (enabled && (j > 0))
                        enabled         = c->vSplit[j-1].bEnabled;
                    bool cust_lcf   = b->pScLpfOn->value() >= 0.5f;
                    bool cust_hcf   = b->pScHpfOn->value() >= 0.5f;
                    float sc_gain   = b->pScPreamp->value();
                    bool mute       = (b->pMute->value() >= 0.5f);
                    bool solo       = (enabled) && (b->pSolo->value() >= 0.5f);
                    plug::IPort *sc = (bStereoSplit) ? b->pScSpSource : b->pScSource;
                    size_t sc_src   = (sc != NULL) ? sc->value() : dspu::SCS_MIDDLE;

                    b->pRelLevelOut->set_value(release);

                    b->bExtSc       = (b->pExtSc != NULL) ? b->pExtSc->value() >= 0.5f : false;

                    b->sSC.set_mode(b->pScMode->value());
                    b->sSC.set_reactivity(b->pScReact->value());
                    b->sSC.set_stereo_mode((nMode == MBCM_MS) ? dspu::SCSM_MIDSIDE : dspu::SCSM_STEREO);
                    b->sSC.set_source(decode_sidechain_source(sc_src, bStereoSplit, i));

                    if (sc_gain != b->fScPreamp)
                    {
                        b->fScPreamp    = sc_gain;
                        b->nSync       |= S_EQ_CURVE;
                    }

                    b->sComp.set_mode(mode);
                    b->sComp.set_threshold(attack, release);
                    b->sComp.set_timings(b->pAttTime->value(), b->pRelTime->value());
                    b->sComp.set_ratio(b->pRatio->value());
                    b->sComp.set_knee(b->pKnee->value());
                    b->sComp.set_boost_threshold((mode != dspu::CM_BOOSTING) ? b->pBThresh->value() : b->pBoost->value());

                    if (b->sComp.modified())
                    {
                        b->sComp.update_settings();
                        b->nSync       |= S_COMP_CURVE;
                    }
                    if (b->fMakeup != makeup)
                    {
                        b->fMakeup      = makeup;
                        b->nSync       |= S_COMP_CURVE;
                    }
                    if (b->bEnabled != enabled)
                    {
                        b->bEnabled     = enabled;
                        b->nSync       |= S_COMP_CURVE;
                        if (!enabled)
                            b->sScDelay.clear(); // Clear delay buffer from artifacts
                    }
                    if (b->bSolo != solo)
                    {
                        b->bSolo        = solo;
                        b->nSync       |= S_COMP_CURVE;
                    }
                    if (b->bMute != mute)
                    {
                        b->bMute        = mute;
                        b->nSync       |= S_COMP_CURVE;
                    }
                    if (b->bCustLCF != cust_lcf)
                    {
                        b->bCustLCF     = cust_lcf;
                        b->nSync       |= S_COMP_CURVE;
                        c->nPlanSize    = 0;
                    }
                    if (b->bCustHCF != cust_hcf)
                    {
                        b->bCustHCF     = cust_hcf;
                        b->nSync       |= S_COMP_CURVE;
                        c->nPlanSize    = 0;
                    }
                    if (cust_lcf)
                    {
                        float lcf       = b->pScLcfFreq->value();
                        if (lcf != b->fFreqLCF)
                        {
                            b->fFreqLCF     = lcf;
                            c->nPlanSize    = 0;
                        }
                    }
                    if (cust_hcf)
                    {
                        float hcf       = b->pScHcfFreq->value();
                        if (hcf != b->fFreqHCF)
                        {
                            b->fFreqHCF     = hcf;
                            c->nPlanSize    = 0;
                        }
                    }

                    if (b->bSolo)
                        solo_on         = true;

                    // Estimate lookahead buffer size
                    b->nLookahead   = dspu::millis_to_samples(fSampleRate, b->pScLook->value());
                }
            }

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Check muting option
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b      = &c->vBands[j];
                    if ((!b->bMute) && (solo_on))
                        b->bMute    = !b->bSolo;
                }

                // Rebuild compression plan
                if (c->nPlanSize <= 0)
                {
                    c->nPlanSize                = 0;
                    c->vBands[0].fFreqStart     = 0;
                    c->vPlan[c->nPlanSize++]    = &c->vBands[0];

                    for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX-1; ++j)
                    {
                        comp_band_t *b      = &c->vBands[j+1];
                        b->fFreqStart       = c->vSplit[j].fFreq;

                        if (c->vSplit[j].bEnabled)
                            c->vPlan[c->nPlanSize++]    = b;
                    }

                    // Do simple sort of PLAN items by frequency
                    if (c->nPlanSize > 1)
                    {
                        // Sort in ascending order
                        for (size_t si=0; si < c->nPlanSize-1; ++si)
                            for (size_t sj=si+1; sj < c->nPlanSize; ++sj)
                                if (compare_bands_for_sort(c->vPlan[si], c->vPlan[sj]))
                                {
                                    comp_band_t *tmp = c->vPlan[si];
                                    c->vPlan[si]    = c->vPlan[sj];
                                    c->vPlan[sj]    = tmp;
                                }

                        for (size_t j=1; j<c->nPlanSize; ++j)
                            c->vPlan[j-1]->fFreqEnd     = c->vPlan[j]->fFreqStart;
                    }
                    c->vPlan[c->nPlanSize-1]->fFreqEnd       = (fSampleRate >> 1);

                    // Configure equalizers
                    lsp_trace("Reordered bands according to frequency grow");
                    for (size_t j=0; j<c->nPlanSize; ++j)
                    {
                        comp_band_t *b  = c->vPlan[j];
                        size_t band     = b - c->vBands;
                        b->pFreqEnd->set_value(b->fFreqEnd);
                        b->nSync       |= S_EQ_CURVE;

    //                    lsp_trace("plan[%d] start=%f, end=%f, fft=%s",
    //                            int(j), b->fFreqStart, b->fFreqEnd,
    //                            (b->bFFT) ? "true" : "false");

                        lsp_trace("plan[%d] start=%f, end=%f", int(j), b->fFreqStart, b->fFreqEnd);

                        // Configure equalizer for the sidechain
                        for (size_t k=0; k<channels; ++k)
                        {
                            // Configure lo-pass filter
                            fp.nType        = ((j != (c->nPlanSize-1)) || (b->bCustHCF)) ? dspu::FLT_BT_LRX_LOPASS : dspu::FLT_NONE;
                            fp.fFreq        = (b->bCustHCF) ? b->pScHcfFreq->value() : b->pFreqEnd->value();
                            fp.fFreq2       = fp.fFreq;
                            fp.fQuality     = 0.0f;
                            fp.fGain        = 1.0f;
                            fp.fQuality     = 0.0f;
                            fp.nSlope       = 2;

                            b->sEQ[k].set_params(0, &fp);

                            // Configure hi-pass filter
                            fp.nType        = ((j != 0) || (b->bCustLCF)) ? dspu::FLT_BT_LRX_HIPASS : dspu::FLT_NONE;
                            fp.fFreq        = (b->bCustLCF) ? b->pScLcfFreq->value() : b->fFreqStart;
                            fp.fFreq2       = fp.fFreq;
                            fp.fQuality     = 0.0f;
                            fp.fGain        = 1.0f;
                            fp.fQuality     = 0.0f;
                            fp.nSlope       = 2;

                            b->sEQ[k].set_params(1, &fp);
                        }

                        // Update transfer function for equalizer
                        b->sEQ[0].freq_chart(size_t(0), b->vTr, vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        b->sEQ[0].freq_chart(size_t(1), vTr, vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::pcomplex_mul2(b->vTr, vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::pcomplex_mod(b->vTr, b->vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);

                        // Update filter parameters, depending on operating mode
                        if (enXOver == XOVER_MODERN)
                        {
                            // Configure filter for band
                            if (j <= 0)
                            {
                                fp.nType        = (c->nPlanSize > 1) ? dspu::FLT_BT_LRX_LOSHELF : dspu::FLT_BT_AMPLIFIER;
                                fp.fFreq        = b->fFreqEnd;
                                fp.fFreq2       = b->fFreqEnd;
                            }
                            else if (j >= (c->nPlanSize - 1))
                            {
                                fp.nType        = dspu::FLT_BT_LRX_HISHELF;
                                fp.fFreq        = b->fFreqStart;
                                fp.fFreq2       = b->fFreqStart;
                            }
                            else
                            {
                                fp.nType        = dspu::FLT_BT_LRX_LADDERPASS;
                                fp.fFreq        = b->fFreqStart;
                                fp.fFreq2       = b->fFreqEnd;
                            }

                            fp.fGain        = 1.0f;
                            fp.nSlope       = 2;
                            fp.fQuality     = 0.0;

                            lsp_trace("Filter type=%d, from=%f, to=%f", int(fp.nType), fp.fFreq, fp.fFreq2);

                            sFilters.set_params(b->nFilterID, &fp);
                        }
                        else if (enXOver == XOVER_CLASSIC)
                        {
                            fp.fGain        = 1.0f;
                            fp.nSlope       = 2;
                            fp.fQuality     = 0.0;
                            fp.fFreq        = b->fFreqEnd;
                            fp.fFreq2       = b->fFreqEnd;

                            // We're going from low frequencies to high frequencies
                            if (j >= (c->nPlanSize - 1))
                            {
                                fp.nType    = dspu::FLT_NONE;
                                b->sPassFilter.update(fSampleRate, &fp);
                                b->sRejFilter.update(fSampleRate, &fp);
                                b->sAllFilter.update(fSampleRate, &fp);
                            }
                            else
                            {
                                fp.nType    = dspu::FLT_BT_LRX_LOPASS;
                                b->sPassFilter.update(fSampleRate, &fp);
                                fp.nType    = dspu::FLT_BT_LRX_HIPASS;
                                b->sRejFilter.update(fSampleRate, &fp);
                                fp.nType    = (j == 0) ? dspu::FLT_NONE : dspu::FLT_BT_LRX_ALLPASS;
                                b->sAllFilter.update(fSampleRate, &fp);
                            }
                        }
                        else // enXOver == XOVER_LINEAR_PHASE
                        {
                            if (j > 0)
                            {
                                c->sFFTXOver.enable_hpf(band, true);
                                c->sFFTXOver.set_hpf_frequency(band, b->fFreqStart);
                                c->sFFTXOver.set_hpf_slope(band, -48.0f);
                            }
                            else
                                c->sFFTXOver.disable_hpf(band);

                            if (j < c->nPlanSize)
                            {
                                c->sFFTXOver.enable_lpf(band, true);
                                c->sFFTXOver.set_lpf_frequency(band, b->fFreqEnd);
                                c->sFFTXOver.set_lpf_slope(band, -48.0f);
                            }
                            else
                                c->sFFTXOver.disable_lpf(band);
                        }
                    }
                } // nPlanSize

                // Enable/disable dynamic filters and bands
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b  = &c->vBands[j];
                    size_t band     = b - c->vBands;
                    sFilters.set_filter_active(b->nFilterID, b->bEnabled);
                    c->sFFTXOver.enable_band(j, (band > 0) ? c->vSplit[band-1].bEnabled : true);
                }

                // Set-up all-pass filters for the 'dry' chain which can be mixed with the 'wet' chain.
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX-1; ++j)
                {
                    comp_band_t *b  = (j < (c->nPlanSize-1)) ? c->vPlan[j] : NULL;
                    fp.nType        = (b != NULL) ? dspu::FLT_BT_LRX_ALLPASS : dspu::FLT_NONE;
                    fp.fFreq        = (b != NULL) ? b->fFreqEnd : 0.0f;
                    fp.fFreq2       = fp.fFreq;
                    fp.fQuality     = 0.0f;
                    fp.fGain        = 1.0f;
                    fp.fQuality     = 0.0f;
                    fp.nSlope       = 2;

                    c->sDryEq.set_params(j, &fp);
                }

                // Calculate latency
                for (size_t j=0; j<c->nPlanSize; ++j)
                {
                    comp_band_t *b  = c->vPlan[j];
                    latency         = lsp_max(latency, b->nLookahead);
                }
            }

            // Update latency
            size_t xover_latency = (enXOver == XOVER_LINEAR_PHASE) ? vChannels[0].sFFTXOver.latency() : 0;

            set_latency(latency + xover_latency);
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update latency
                for (size_t j=0; j<c->nPlanSize; ++j)
                {
                    comp_band_t *b  = c->vPlan[j];
                    b->sScDelay.set_delay(latency + xover_latency - b->nLookahead);
                }
                c->sDelay.set_delay(latency);
                c->sDryDelay.set_delay(latency + xover_latency);
                c->sAnDelay.set_delay(xover_latency);
            }

            // Debug:
    #ifdef LSP_TRACE
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                for (size_t j=0; j<c->nPlanSize; ++j)
                {
                    comp_band_t *b  = c->vPlan[j];
                    dspu::filter_params_t fp;
                    sFilters.get_params(b->nFilterID, &fp);

                    lsp_trace("plan[%d, %d] start=%f, end=%f, filter={id=%d, type=%d, slope=%d}, solo=%s, mute=%s, lookahead=%d",
                            int(i), int(j),
                            b->fFreqStart, b->fFreqEnd,
                            int(b->nFilterID), int(fp.nType), int(fp.nSlope),
                            (b->bSolo) ? "true" : "false",
                            (b->bMute) ? "true" : "false",
                            int(b->nLookahead)
                        );
                }
            }
    #endif /* LSP_TRACE */

            nEnvBoost       = env_boost;
            bEnvUpdate      = false;
        }

        size_t mb_compressor::select_fft_rank(size_t sample_rate)
        {
            const size_t k = (sample_rate + meta::mb_compressor_metadata::FFT_XOVER_FREQ_MIN/2) / meta::mb_compressor_metadata::FFT_XOVER_FREQ_MIN;
            const size_t n = int_log2(k);
            return meta::mb_compressor_metadata::FFT_XOVER_RANK_MIN << n;
        }

        void mb_compressor::update_sample_rate(long sr)
        {
            // Determine number of channels
            size_t channels     = (nMode == MBCM_MONO) ? 1 : 2;
            size_t fft_rank     = select_fft_rank(sr);
            size_t bins         = 1 << fft_rank;
            size_t max_delay    = bins + dspu::millis_to_samples(sr, meta::mb_compressor_metadata::LOOKAHEAD_MAX);

            // Update analyzer's sample rate
            sAnalyzer.set_sample_rate(sr);
            sFilters.set_sample_rate(sr);
            bEnvUpdate          = true;

            // Update channels
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];
                c->sBypass.init(sr);
                c->sDelay.init(max_delay);
                c->sDryDelay.init(max_delay);
                c->sAnDelay.init(bins);
                c->sDryEq.set_sample_rate(sr);

                // Need to re-initialize FFT crossover?
                if (fft_rank != c->sFFTXOver.rank())
                {
                    c->sFFTXOver.init(fft_rank, meta::mb_compressor_metadata::BANDS_MAX);
                    for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                        c->sFFTXOver.set_handler(j, process_band, this, c);
                    c->sFFTXOver.set_rank(fft_rank);
                    c->sFFTXOver.set_phase(float(i) / float(channels));
                }
                c->sFFTXOver.set_sample_rate(sr);

                // Update bands
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b  = &c->vBands[j];

                    b->sSC.set_sample_rate(sr);
                    b->sComp.set_sample_rate(sr);
                    b->sScDelay.init(max_delay);

                    b->sPassFilter.set_sample_rate(sr);
                    b->sRejFilter.set_sample_rate(sr);
                    b->sAllFilter.set_sample_rate(sr);

                    b->sEQ[0].set_sample_rate(sr);
                    if (channels > 1)
                        b->sEQ[1].set_sample_rate(sr);
                }

                c->nPlanSize        = 0; // Force to rebuild plan
            }
        }

        void mb_compressor::ui_activated()
        {
            size_t channels     = (nMode == MBCM_MONO) ? 1 : 2;

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c        = &vChannels[i];

                for (size_t j=0; j<c->nPlanSize; ++j)
                {
                    comp_band_t *b      = c->vPlan[j];
                    b->nSync            = S_ALL;
                }
            }
        }

        /*
             The overall schema of signal processing in 'classic' mode for 4 bands:


            s   ┌─────┐     ┌─────┐     ┌─────┐     ┌─────┐     ┌─────┐     ┌─────┐     ┌─────┐  s' (wet)
           ──┬─►│LPF 1│────►│VCA 1│────►│APF 2│────►│  +  │────►│APF 3│────►│  +  │────►│  +  │────►
             │  └─────┘     └─────┘     └─────┘     └─────┘     └─────┘     └─────┘     └─────┘
             │                                         ▲                       ▲           ▲
             │                                         │                       │           │
             │  ┌─────┐                 ┌─────┐     ┌─────┐     ┌─────┐     ┌─────┐        │
             ├─►│HPF 1│──────────────┬─►│LPF 2│────►│VCA 2│  ┌─►│LPF 3│────►│VCA 3│        │
             │  └─────┘              │  └─────┘     └─────┘  │  └─────┘     └─────┘        │
             │                       │                       │                             │
             │                       │                       │                             │
             │                       │  ┌─────┐              │  ┌─────┐     ┌─────┐        │
             │                       └─►│HPF 2│──────────────┴─►│HPF 3│────►│VCA 4│────────┘
             │                          └─────┘                 └─────┘     └─────┘
             │
             │  ┌─────┐                 ┌─────┐                 ┌─────┐                          s" (dry)
             └─►│APF 1│────────────────►│APF 2│────────────────►│APF 3│────────────────────────────►
                └─────┘                 └─────┘                 └─────┘
         */

        void mb_compressor::process_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count)
        {
            channel_t *c            = static_cast<channel_t *>(subject);
            comp_band_t *b          = &c->vBands[band];

            // Store data to band's buffer
            dsp::copy(&b->vBuffer[sample], data, count);
        }

        void mb_compressor::process(size_t samples)
        {
            size_t channels     = (nMode == MBCM_MONO) ? 1 : 2;

            // Bind input signal
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c        = &vChannels[i];

                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();
                c->vScIn            = (c->pScIn != NULL) ? c->pScIn->buffer<float>() : NULL;
            }

            // Do processing
            while (samples > 0)
            {
                // Determine buffer size for processing
                size_t to_process   = (samples > MBC_BUFFER_SIZE) ? MBC_BUFFER_SIZE : samples;

                // Measure input signal level
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];
                    float level         = dsp::abs_max(c->vIn, to_process) * fInGain;
                    c->pInLvl->set_value(level);
                }

                // Pre-process channel data
                if (nMode == MBCM_MS)
                {
                    dsp::lr_to_ms(vChannels[0].vBuffer, vChannels[1].vBuffer, vChannels[0].vIn, vChannels[1].vIn, to_process);
                    dsp::mul_k2(vChannels[0].vBuffer, fInGain, to_process);
                    dsp::mul_k2(vChannels[1].vBuffer, fInGain, to_process);
                }
                else if (nMode == MBCM_MONO)
                    dsp::mul_k3(vChannels[0].vBuffer, vChannels[0].vIn, fInGain, to_process);
                else
                {
                    dsp::mul_k3(vChannels[0].vBuffer, vChannels[0].vIn, fInGain, to_process);
                    dsp::mul_k3(vChannels[1].vBuffer, vChannels[1].vIn, fInGain, to_process);
                }
                if (bSidechain)
                {
                    if (nMode == MBCM_MS)
                    {
                        dsp::lr_to_ms(vChannels[0].vExtScBuffer, vChannels[1].vExtScBuffer, vChannels[0].vScIn, vChannels[1].vScIn, to_process);
                        dsp::mul_k2(vChannels[0].vExtScBuffer, fInGain, to_process);
                        dsp::mul_k2(vChannels[1].vExtScBuffer, fInGain, to_process);
                    }
                    else if (nMode == MBCM_MONO)
                        dsp::mul_k3(vChannels[0].vExtScBuffer, vChannels[0].vScIn, fInGain, to_process);
                    else
                    {
                        dsp::mul_k3(vChannels[0].vExtScBuffer, vChannels[0].vScIn, fInGain, to_process);
                        dsp::mul_k3(vChannels[1].vExtScBuffer, vChannels[1].vScIn, fInGain, to_process);
                    }
                }

                // Do frequency boost and input channel analysis
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];
                    c->sEnvBoost[0].process(c->vScBuffer, c->vBuffer, to_process);
                    if (bSidechain)
                        c->sEnvBoost[1].process(c->vExtScBuffer, c->vExtScBuffer, to_process);

                    c->sAnDelay.process(c->vInAnalyze, c->vBuffer, to_process);
                }

                // MAIN PLUGIN STUFF
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];

                    for (size_t j=0; j<c->nPlanSize; ++j)
                    {
                        comp_band_t *b      = c->vPlan[j];

                        // Prepare sidechain signal with band equalizers
                        b->sEQ[0].process(vSc[0], (b->bExtSc) ? vChannels[0].vExtScBuffer : vChannels[0].vScBuffer, to_process);
                        if (channels > 1)
                            b->sEQ[1].process(vSc[1], (b->bExtSc) ? vChannels[1].vExtScBuffer : vChannels[1].vScBuffer, to_process);

                        // Preprocess VCA signal
                        b->sSC.process(vBuffer, const_cast<const float **>(vSc), to_process); // Band now contains processed by sidechain signal
                        b->sScDelay.process(vBuffer, vBuffer, b->fScPreamp, to_process); // Apply sidechain preamp and lookahead delay

                        if (b->bEnabled)
                        {
                            b->sComp.process(b->vVCA, vEnv, vBuffer, to_process); // Output
                            dsp::mul_k2(b->vVCA, b->fMakeup, to_process); // Apply makeup gain

                            // Output curve level
                            float lvl = dsp::abs_max(vEnv, to_process);
                            b->pEnvLvl->set_value(lvl);
                            b->pMeterGain->set_value(b->sComp.reduction(lvl));
                            lvl = b->sComp.curve(lvl) * b->fMakeup;
                            b->pCurveLvl->set_value(lvl);

                            // Remember last envelope level and buffer level
                            b->fGainLevel   = b->vVCA[to_process-1];

                            // Check muting option
                            if (b->bMute)
                                dsp::fill(b->vVCA, GAIN_AMP_M_36_DB, to_process);
                        }
                        else
                        {
                            dsp::fill(b->vVCA, (b->bMute) ? GAIN_AMP_M_36_DB : GAIN_AMP_0_DB, to_process);
                            b->fGainLevel   = GAIN_AMP_0_DB;
                        }
                    }

                    // Output curve parameters for disabled compressors
                    for (size_t i=0; i<meta::mb_compressor_metadata::BANDS_MAX; ++i)
                    {
                        comp_band_t *b      = &c->vBands[i];
                        if (b->bEnabled)
                            continue;

                        b->pEnvLvl->set_value(0.0f);
                        b->pCurveLvl->set_value(0.0f);
                        b->pMeterGain->set_value(GAIN_AMP_0_DB);
                    }
                }

                // Here, we apply VCA to input signal dependent on the input
                if (enXOver == XOVER_MODERN) // 'Modern' mode
                {
                    // Apply VCA control
                    for (size_t i=0; i<channels; ++i)
                    {
                        channel_t *c        = &vChannels[i];
                        c->sDelay.process(c->vBuffer, c->vBuffer, to_process); // Apply delay to compensate lookahead feature
                        dsp::copy(c->vInBuffer, c->vBuffer, to_process);

                        for (size_t j=0; j<c->nPlanSize; ++j)
                        {
                            comp_band_t *b      = c->vPlan[j];
                            sFilters.process(b->nFilterID, c->vBuffer, c->vBuffer, b->vVCA, to_process);
                        }
                    }
                }
                else if (enXOver == XOVER_CLASSIC) // 'Classic' mode
                {
                    // Apply VCA control
                    for (size_t i=0; i<channels; ++i)
                    {
                        channel_t *c        = &vChannels[i];

                        // Originally, there is no signal
                        c->sDelay.process(c->vInBuffer, c->vBuffer, to_process); // Apply delay to compensate lookahead feature, store into vBuffer
                        dsp::copy(vBuffer, c->vInBuffer, to_process);
                        dsp::fill_zero(c->vBuffer, to_process);                 // Clear the channel buffer

                        for (size_t j=0; j<c->nPlanSize; ++j)
                        {
                            comp_band_t *b      = c->vPlan[j];

                            b->sAllFilter.process(c->vBuffer, c->vBuffer, to_process); // Process the signal with all-pass
                            b->sPassFilter.process(vEnv, vBuffer, to_process); // Filter frequencies from input
                            // TODO: use fmadd3
                            dsp::mul2(vEnv, b->vVCA, to_process); // Apply VCA gain
                            dsp::add2(c->vBuffer, vEnv, to_process); // Add signal to the channel buffer
                            b->sRejFilter.process(vBuffer, vBuffer, to_process); // Filter frequencies from input
                        }
                    }
                }
                else // enXOver == XOVER_LINEAR_PHASE
                {
                    // Apply VCA control
                    for (size_t i=0; i<channels; ++i)
                    {
                        channel_t *c        = &vChannels[i];

                        c->sDelay.process(c->vBuffer, c->vBuffer, to_process);  // Apply delay to compensate lookahead feature
                        dsp::copy(c->vInBuffer, c->vBuffer, to_process);
                        c->sFFTXOver.process(c->vBuffer, to_process);
                        dsp::fill_zero(c->vBuffer, to_process);                 // Clear the channel buffer

                        for (size_t j=0; j<c->nPlanSize; ++j)
                        {
                            comp_band_t *b      = c->vPlan[j];
                            dsp::fmadd3(c->vBuffer, b->vVCA, b->vBuffer, to_process);
                        }
                    }
                }

                // MAIN PLUGIN STUFF END

                // Do output channel analysis
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];
                    dsp::copy(c->vOutAnalyze, c->vBuffer, to_process);
                }

                sAnalyzer.process(vAnalyze, to_process);

                // Post-process data (if needed)
                if (nMode == MBCM_MS)
                {
                    dsp::ms_to_lr(vChannels[0].vBuffer, vChannels[1].vBuffer, vChannels[0].vBuffer, vChannels[1].vBuffer, to_process);
                    dsp::ms_to_lr(vChannels[0].vInBuffer, vChannels[1].vInBuffer, vChannels[0].vInBuffer, vChannels[1].vInBuffer, to_process);
                }

                // Final metering
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];

                    // Apply dry/wet balance
                    if (enXOver == XOVER_MODERN)
                        dsp::mix2(c->vBuffer, c->vInBuffer, fWetGain, fDryGain, to_process);
                    else if (enXOver == XOVER_CLASSIC)
                    {
                        c->sDryEq.process(vBuffer, c->vInBuffer, to_process);
                        dsp::mix2(c->vBuffer, vBuffer, fWetGain, fDryGain, to_process);
                    }
                    else // enXOver == XOVER_LINEAR_PHASE
                        dsp::mix2(c->vBuffer, c->vInBuffer, fWetGain, fDryGain, to_process);

                    // Compute output level
                    float level         = dsp::abs_max(c->vBuffer, to_process);
                    c->pOutLvl->set_value(level);

                    // Apply bypass
                    c->sDryDelay.process(vBuffer, c->vIn, to_process);
                    c->sBypass.process(c->vOut, vBuffer, c->vBuffer, to_process);

                    // Update pointers
                    c->vIn             += to_process;
                    c->vOut            += to_process;
                    if (c->vScIn != NULL)
                        c->vScIn           += to_process;
                }
                samples    -= to_process;
            } // while (samples > 0)

            // Output FFT curves for each channel
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c     = &vChannels[i];

                // Calculate transfer function for the compressor
                if (enXOver == XOVER_MODERN)
                {
                    dsp::pcomplex_fill_ri(c->vTr, 1.0f, 0.0f, meta::mb_compressor_metadata::FFT_MESH_POINTS);

                    // Calculate transfer function
                    for (size_t j=0; j<c->nPlanSize; ++j)
                    {
                        comp_band_t *b      = c->vPlan[j];
                        sFilters.freq_chart(b->nFilterID, vTr, vFreqs, b->fGainLevel, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::pcomplex_mul2(c->vTr, vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                    }
                    dsp::pcomplex_mod(c->vTrMem, c->vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                }
                else if (enXOver == XOVER_CLASSIC)
                {
                    dsp::pcomplex_fill_ri(vTr, 1.0f, 0.0f, meta::mb_compressor_metadata::FFT_MESH_POINTS);   // vBuffer
                    dsp::fill_zero(c->vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS*2);                 // c->vBuffer

                    // Calculate transfer function
                    for (size_t j=0; j<c->nPlanSize; ++j)
                    {
                        comp_band_t *b      = c->vPlan[j];

                        // Apply all-pass characteristics
                        b->sAllFilter.freq_chart(vPFc, vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::pcomplex_mul2(c->vTr, vPFc, meta::mb_compressor_metadata::FFT_MESH_POINTS);

                        // Apply lo-pass filter characteristics
                        b->sPassFilter.freq_chart(vPFc, vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::pcomplex_mul2(vPFc, vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::fmadd_k3(c->vTr, vPFc, b->fGainLevel, meta::mb_compressor_metadata::FFT_MESH_POINTS*2);

                        // Apply hi-pass filter characteristics
                        b->sRejFilter.freq_chart(vRFc, vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::pcomplex_mul2(vTr, vRFc, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                    }
                    dsp::pcomplex_mod(c->vTrMem, c->vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                }
                else // enXOver == XOVER_LINEAR_PHASE
                {
                    dsp::fill_zero(c->vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                    // Calculate transfer function
                    for (size_t j=0; j<c->nPlanSize; ++j)
                    {
                        comp_band_t *b      = c->vPlan[j];
                        size_t band         = b - c->vBands;
                        c->sFFTXOver.freq_chart(band, vPFc, vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        dsp::fmadd_k3(c->vTr, vPFc, b->fGainLevel, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                    }
                    dsp::copy(c->vTrMem, c->vTr, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                }

                // Output FFT curve, compression curve and FFT spectrogram for each band
                for (size_t j=0; j<meta::mb_compressor_metadata::BANDS_MAX; ++j)
                {
                    comp_band_t *b      = &c->vBands[j];

                    // FFT spectrogram
                    plug::mesh_t *mesh        = NULL;

                    // FFT curve
                    if (b->nSync & S_EQ_CURVE)
                    {
                        mesh                = (b->pScFreqChart != NULL) ? b->pScFreqChart->buffer<plug::mesh_t>() : NULL;
                        if ((mesh != NULL) && (mesh->isEmpty()))
                        {
                            // Add extra points
                            mesh->pvData[0][0] = SPEC_FREQ_MIN*0.5f;
                            mesh->pvData[0][meta::mb_compressor_metadata::MESH_POINTS+1] = SPEC_FREQ_MAX * 2.0f;
                            mesh->pvData[1][0] = 0.0f;
                            mesh->pvData[1][meta::mb_compressor_metadata::MESH_POINTS+1] = 0.0f;

                            // Fill mesh
                            dsp::copy(&mesh->pvData[0][1], vFreqs, meta::mb_compressor_metadata::MESH_POINTS);
                            dsp::mul_k3(&mesh->pvData[1][1], b->vTr, b->fScPreamp, meta::mb_compressor_metadata::MESH_POINTS);
                            mesh->data(2, meta::mb_compressor_metadata::FILTER_MESH_POINTS);

                            // Mark mesh as synchronized
                            b->nSync           &= ~S_EQ_CURVE;
                        }
                    }

                    // Compression curve
                    if (b->nSync & S_COMP_CURVE)
                    {
                        mesh                = (b->pCurveGraph != NULL) ? b->pCurveGraph->buffer<plug::mesh_t>() : NULL;
                        if ((mesh != NULL) && (mesh->isEmpty()))
                        {
                            if (b->bEnabled)
                            {
                                // Copy frequency points
                                dsp::copy(mesh->pvData[0], vCurve, meta::mb_compressor_metadata::CURVE_MESH_SIZE);
                                b->sComp.curve(mesh->pvData[1], vCurve, meta::mb_compressor_metadata::CURVE_MESH_SIZE);
                                if (b->fMakeup != GAIN_AMP_0_DB)
                                    dsp::mul_k2(mesh->pvData[1], b->fMakeup, meta::mb_compressor_metadata::CURVE_MESH_SIZE);

                                // Mark mesh containing data
                                mesh->data(2, meta::mb_compressor_metadata::CURVE_MESH_SIZE);
                            }
                            else
                                mesh->data(2, 0);

                            // Mark mesh as synchronized
                            b->nSync           &= ~S_COMP_CURVE;
                        }
                    }
                }

                // Output FFT curve for input
                plug::mesh_t *mesh            = (c->pFftIn != NULL) ? c->pFftIn->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    if (c->bInFft)
                    {
                        // Add extra points
                        mesh->pvData[0][0] = SPEC_FREQ_MIN * 0.5f;
                        mesh->pvData[0][meta::mb_compressor_metadata::FFT_MESH_POINTS+1] = SPEC_FREQ_MAX * 2.0f;
                        mesh->pvData[1][0] = 0.0f;
                        mesh->pvData[1][meta::mb_compressor_metadata::FFT_MESH_POINTS+1] = 0.0f;

                        // Copy frequency points
                        dsp::copy(&mesh->pvData[0][1], vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        sAnalyzer.get_spectrum(c->nAnInChannel, &mesh->pvData[1][1], vIndexes, meta::mb_compressor_metadata::FFT_MESH_POINTS);

                        // Mark mesh containing data
                        mesh->data(2, meta::mb_compressor_metadata::FFT_MESH_POINTS + 2);
                    }
                    else
                        mesh->data(2, 0);
                }

                // Output FFT curve for output
                mesh            = (c->pFftOut != NULL) ? c->pFftOut->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    if (sAnalyzer.channel_active(c->nAnOutChannel))
                    {
                        // Copy frequency points
                        dsp::copy(mesh->pvData[0], vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                        sAnalyzer.get_spectrum(c->nAnOutChannel, mesh->pvData[1], vIndexes, meta::mb_compressor_metadata::FFT_MESH_POINTS);

                        // Mark mesh containing data
                        mesh->data(2, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                    }
                    else
                        mesh->data(2, 0);
                }

                // Output Channel curve
                mesh            = (c->pAmpGraph != NULL) ? c->pAmpGraph->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    // Calculate amplitude (modulo)
                    dsp::copy(mesh->pvData[0], vFreqs, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                    dsp::copy(mesh->pvData[1], c->vTrMem, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                    mesh->data(2, meta::mb_compressor_metadata::FFT_MESH_POINTS);
                }
            } // for channel

            // Request for redraw
            if (pWrapper != NULL)
                pWrapper->query_display_draw();
        }

        bool mb_compressor::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            // Check proportions
            if (height > (M_RGOLD_RATIO * width))
                height  = M_RGOLD_RATIO * width;

            // Init canvas
            if (!cv->init(width, height))
                return false;
            width   = cv->width();
            height  = cv->height();

            // Clear background
            bool bypassing = vChannels[0].sBypass.bypassing();
            cv->set_color_rgb((bypassing) ? CV_DISABLED : CV_BACKGROUND);
            cv->paint();

            // Draw axis
            cv->set_line_width(1.0);

            // "-72 db / (:zoom ** 3)" max="24 db * :zoom"

            float miny  = logf(GAIN_AMP_M_72_DB / dsp::ipowf(fZoom, 3));
            float maxy  = logf(GAIN_AMP_P_24_DB * fZoom);

            float zx    = 1.0f/SPEC_FREQ_MIN;
            float zy    = dsp::ipowf(fZoom, 3)/GAIN_AMP_M_72_DB;
            float dx    = width/(logf(SPEC_FREQ_MAX)-logf(SPEC_FREQ_MIN));
            float dy    = height/(miny-maxy);

            // Draw vertical lines
            cv->set_color_rgb(CV_YELLOW, 0.5f);
            for (float i=100.0f; i<SPEC_FREQ_MAX; i *= 10.0f)
            {
                float ax = dx*(logf(i*zx));
                cv->line(ax, 0, ax, height);
            }

            // Draw horizontal lines
            cv->set_color_rgb(CV_WHITE, 0.5f);
            for (float i=GAIN_AMP_M_72_DB; i<GAIN_AMP_P_24_DB; i *= GAIN_AMP_P_12_DB)
            {
                float ay = height + dy*(logf(i*zy));
                cv->line(0, ay, width, ay);
            }

            // Allocate buffer: f, x, y, tr
            pIDisplay           = core::IDBuffer::reuse(pIDisplay, 4, width+2);
            core::IDBuffer *b   = pIDisplay;
            if (b == NULL)
                return false;

            // Initialize mesh
            b->v[0][0]          = SPEC_FREQ_MIN*0.5f;
            b->v[0][width+1]    = SPEC_FREQ_MAX*2.0f;
            b->v[3][0]          = 1.0f;
            b->v[3][width+1]    = 1.0f;

            static const uint32_t c_colors[] =
            {
                CV_MIDDLE_CHANNEL,
                CV_LEFT_CHANNEL, CV_RIGHT_CHANNEL,
                CV_MIDDLE_CHANNEL, CV_SIDE_CHANNEL
            };

            size_t channels     = ((nMode == MBCM_MONO) || ((nMode == MBCM_STEREO) && (!bStereoSplit))) ? 1 : 2;
            const uint32_t *vc  = (channels == 1) ? &c_colors[0] :
                                  (nMode == MBCM_MS) ? &c_colors[3] :
                                  &c_colors[1];

            bool aa = cv->set_anti_aliasing(true);
            lsp_finally { cv->set_anti_aliasing(aa); };
            cv->set_line_width(2);

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];

                for (size_t j=0; j<width; ++j)
                {
                    size_t k        = (j*meta::mb_compressor_metadata::MESH_POINTS)/width;
                    b->v[0][j+1]    = vFreqs[k];
                    b->v[3][j+1]    = c->vTrMem[k];
                }

                dsp::fill(b->v[1], 0.0f, width+2);
                dsp::fill(b->v[2], height, width+2);
                dsp::axis_apply_log1(b->v[1], b->v[0], zx, dx, width+2);
                dsp::axis_apply_log1(b->v[2], b->v[3], zy, dy, width+2);

                // Draw mesh
                uint32_t color = (bypassing || !(active())) ? CV_SILVER : vc[i];
                Color stroke(color), fill(color, 0.5f);
                cv->draw_poly(b->v[1], b->v[2], width+2, stroke, fill);
            }

            return true;
        }

        void mb_compressor::dump(dspu::IStateDumper *v) const
        {
            plug::Module::dump(v);

            size_t channels     = (nMode == MBCM_MONO) ? 1 : 2;

            v->write_object("sAnalyzer", &sAnalyzer);
            v->write_object("sFilters", &sFilters);
            v->write("nMode", nMode);
            v->write("bSidechain", bSidechain);
            v->write("bEnvUpdate", bEnvUpdate);
            v->write("enXOver", enXOver);
            v->write("bStereoSplit", bStereoSplit);
            v->write("nEnvBoost", nEnvBoost);
            v->begin_array("vChannels", vChannels, channels);
            {
                for (size_t i=0; i<channels; ++i)
                {
                    const channel_t *c = &vChannels[i];

                    v->write_object("sBypass", &c->sBypass);
                    v->begin_array("sEnvBoost", c->sEnvBoost, 2);
                    for (size_t i=0; i<2; ++i)
                        v->write_object(&c->sEnvBoost[i]);
                    v->end_array();
                    v->write_object("sDelay", &c->sDelay);
                    v->write_object("sDryEq", &c->sDryEq);
                    v->write_object("sFFTXOver", &c->sFFTXOver);

                    v->begin_array("vBands", c->vBands, meta::mb_compressor_metadata::BANDS_MAX);
                    for (size_t i=0; i<meta::mb_compressor_metadata::BANDS_MAX; ++i)
                    {
                        const comp_band_t *b = &c->vBands[i];
                        v->begin_object(b, sizeof(comp_band_t));
                        {
                            v->write_object("sSC", &b->sSC);
                            v->write_object_array("sEq", b->sEQ, 2);
                            v->write_object("sComp", &b->sComp);
                            v->write_object("sPassFilter", &b->sPassFilter);
                            v->write_object("sRejFilter", &b->sRejFilter);
                            v->write_object("sAllFilter", &b->sAllFilter);
                            v->write_object("sScDelay", &b->sScDelay);

                            v->write("vTr", b->vTr);
                            v->write("vVCA", b->vVCA);
                            v->write("fScPreamp", b->fScPreamp);

                            v->write("fFreqStart", b->fFreqStart);
                            v->write("fFreqEnd", b->fFreqEnd);

                            v->write("fFreqHCF", b->fFreqHCF);
                            v->write("fFreqLCF", b->fFreqLCF);
                            v->write("fMakeup", b->fMakeup);
                            v->write("fGainLevel", b->fGainLevel);
                            v->write("nLookahead", b->nLookahead);

                            v->write("bEnabled", b->bEnabled);
                            v->write("bCustHCF", b->bCustHCF);
                            v->write("bCustLCF", b->bCustLCF);
                            v->write("bMute", b->bMute);
                            v->write("bSolo", b->bSolo);
                            v->write("bExtSc", b->bExtSc);
                            v->write("nSync", b->nSync);
                            v->write("nFilterID", b->nFilterID);

                            v->write("pExtSc", b->pExtSc);
                            v->write("pScSource", b->pScSource);
                            v->write("pScSpSource", b->pScSpSource);
                            v->write("pScMode", b->pScMode);
                            v->write("pScLook", b->pScLook);
                            v->write("pScReact", b->pScReact);
                            v->write("pScPreamp", b->pScPreamp);
                            v->write("pScLpfOn", b->pScLpfOn);
                            v->write("pScHpfOn", b->pScHpfOn);
                            v->write("pScLcfFreq", b->pScLcfFreq);
                            v->write("pScHcfFreq", b->pScHcfFreq);
                            v->write("pScFreqChart", b->pScFreqChart);

                            v->write("pMode", b->pMode);
                            v->write("pEnable", b->pEnable);
                            v->write("pSolo", b->pSolo);
                            v->write("pMute", b->pMute);
                            v->write("pAttLevel", b->pAttLevel);
                            v->write("pAttTime", b->pAttTime);
                            v->write("pRelLevel", b->pRelLevel);
                            v->write("pRelTime", b->pRelTime);
                            v->write("pRatio", b->pRatio);
                            v->write("pKnee", b->pKnee);
                            v->write("pBThresh", b->pBThresh);
                            v->write("pBoost", b->pBoost);
                            v->write("pMakeup", b->pMakeup);
                            v->write("pFreqEnd", b->pFreqEnd);
                            v->write("pCurveGraph", b->pCurveGraph);
                            v->write("pRelLevelOut", b->pRelLevelOut);
                            v->write("pEnvLvl", b->pEnvLvl);
                            v->write("pCurveLvl", b->pCurveLvl);
                            v->write("pMeterGain", b->pMeterGain);
                        }
                    }
                    v->end_array();

                    v->begin_array("vSplit", c->vBands, meta::mb_compressor_metadata::BANDS_MAX-1);
                    for (size_t i=0; i<(meta::mb_compressor_metadata::BANDS_MAX-1); ++i)
                    {
                        const split_t *s = &c->vSplit[i];
                        v->begin_object(s, sizeof(split_t));
                        {
                            v->write("bEnabled", s->bEnabled);
                            v->write("fFreq", s->fFreq);
                            v->write("pEnabled", s->pEnabled);
                            v->write("pFreq", s->pFreq);
                        }
                        v->end_object();
                    }
                    v->end_array();
                    v->writev("vPlan", c->vPlan, meta::mb_compressor_metadata::BANDS_MAX);
                    v->write("nPlanSize", c->nPlanSize);

                    v->write("vIn", c->vIn);
                    v->write("vOut", c->vOut);
                    v->write("vScIn", c->vScIn);

                    v->write("vInBuffer", c->vInBuffer);
                    v->write("vBuffer", c->vBuffer);
                    v->write("vScBuffer", c->vScBuffer);
                    v->write("vExtScBuffer", c->vExtScBuffer);
                    v->write("vTr", c->vTr);
                    v->write("vTrMem", c->vTrMem);
                    v->write("vInAnalyze", c->vInAnalyze);
                    v->write("vOutAnalyze", c->vOutAnalyze);

                    v->write("nAnInChannel", c->nAnInChannel);
                    v->write("nAnOutChannel", c->nAnOutChannel);
                    v->write("bInFft", c->bInFft);
                    v->write("bOutFft", c->bOutFft);

                    v->write("pIn", c->pIn);
                    v->write("pOut", c->pOut);
                    v->write("pScIn", c->pScIn);
                    v->write("pFftIn", c->pFftIn);
                    v->write("pFftInSw", c->pFftInSw);
                    v->write("pFftOut", c->pFftOut);
                    v->write("pFftOutSw", c->pFftOutSw);
                    v->write("pAmpGraph", c->pAmpGraph);
                    v->write("pInLvl", c->pInLvl);
                    v->write("pOutLvl", c->pOutLvl);
                }
            }
            v->end_array();
            v->write("fInGain", fInGain);
            v->write("fDryGain", fDryGain);
            v->write("fWetGain", fWetGain);
            v->write("fZoom", fZoom);
            v->write("pData", pData);
            v->writev("vSc", vSc, 2);
            v->writev("vAnalyze", vAnalyze, 4);
            v->write("vBuffer", vBuffer);
            v->write("vEnv", vEnv);
            v->write("vTr", vTr);
            v->write("vPFc", vPFc);
            v->write("vRFc", vRFc);
            v->write("vFreqs", vFreqs);
            v->write("vCurve", vCurve);
            v->write("vIndexes", vIndexes);
            v->write("pIDisplay", pIDisplay);

            v->write("pBypass", pBypass);
            v->write("pMode", pMode);
            v->write("pInGain", pInGain);
            v->write("pOutGain", pOutGain);
            v->write("pDryGain", pDryGain);
            v->write("pWetGain", pWetGain);
            v->write("pReactivity", pReactivity);
            v->write("pShiftGain", pShiftGain);
            v->write("pZoom", pZoom);
            v->write("pEnvBoost", pEnvBoost);
            v->write("pStereoSplit", pStereoSplit);
        }
    } // namespace plugins
} // namespace lsp


