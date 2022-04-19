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

#ifndef PRIVATE_PLUGINS_MB_COMPRESSOR_H_
#define PRIVATE_PLUGINS_MB_COMPRESSOR_H_

#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/plug-fw/core/IDBuffer.h>
#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/dynamics/Compressor.h>
#include <lsp-plug.in/dsp-units/filters/DynamicFilters.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>
#include <lsp-plug.in/dsp-units/filters/Filter.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <lsp-plug.in/dsp-units/util/MeterGraph.h>
#include <lsp-plug.in/dsp-units/util/Sidechain.h>

#include <private/meta/mb_compressor.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Multiband Compressor Plugin Series
         */
        class mb_compressor: public plug::Module
        {
            public:
                enum c_mode_t
                {
                    MBCM_MONO,
                    MBCM_STEREO,
                    MBCM_LR,
                    MBCM_MS
                };

            protected:
                enum sync_t
                {
                    S_COMP_CURVE    = 1 << 0,
                    S_EQ_CURVE      = 1 << 1,

                    S_ALL           = S_COMP_CURVE | S_EQ_CURVE
                };

                typedef struct comp_band_t
                {
                    dspu::Sidechain         sSC;                // Sidechain module
                    dspu::Equalizer         sEQ[2];             // Sidechain equalizers
                    dspu::Compressor        sComp;              // Compressor
                    dspu::Filter            sPassFilter;        // Passing filter for 'classic' mode
                    dspu::Filter            sRejFilter;         // Rejection filter for 'classic' mode
                    dspu::Filter            sAllFilter;         // All-pass filter for phase compensation
                    dspu::Delay             sScDelay;           // Sidechain delay for lookahead purpose

                    float                  *vTr;                // Transfer function
                    float                  *vVCA;               // Voltage-controlled amplification value for each band
                    float                   fScPreamp;          // Sidechain preamp

                    float                   fFreqStart;
                    float                   fFreqEnd;

                    float                   fFreqHCF;           // Cutoff frequency for low-pass filter
                    float                   fFreqLCF;           // Cutoff frequency for high-pass filter
                    float                   fMakeup;            // Makeup gain
                    float                   fGainLevel;         // Gain adjustment level
                    size_t                  nLookahead;         // Lookahead amount

                    bool                    bEnabled;           // Enabled flag
                    bool                    bCustHCF;           // Custom frequency for high-cut filter
                    bool                    bCustLCF;           // Custom frequency for low-cut filter
                    bool                    bMute;              // Mute channel
                    bool                    bSolo;              // Solo channel
                    bool                    bExtSc;             // External sidechain
                    size_t                  nSync;              // Synchronize output data flags
                    size_t                  nFilterID;          // Identifier of the filter

                    plug::IPort            *pExtSc;             // External sidechain
                    plug::IPort            *pScSource;          // Sidechain source
                    plug::IPort            *pScMode;            // Sidechain mode
                    plug::IPort            *pScLook;            // Sidechain lookahead
                    plug::IPort            *pScReact;           // Sidechain reactivity
                    plug::IPort            *pScPreamp;          // Sidechain preamp
                    plug::IPort            *pScLpfOn;           // Sidechain low-pass on
                    plug::IPort            *pScHpfOn;           // Sidechain hi-pass on
                    plug::IPort            *pScLcfFreq;         // Sidechain low-cut frequency
                    plug::IPort            *pScHcfFreq;         // Sidechain hi-cut frequency
                    plug::IPort            *pScFreqChart;       // Sidechain band frequency chart

                    plug::IPort            *pMode;              // Compressor mode
                    plug::IPort            *pEnable;            // Enable compressor
                    plug::IPort            *pSolo;              // Soloing
                    plug::IPort            *pMute;              // Muting
                    plug::IPort            *pAttLevel;          // Attack level
                    plug::IPort            *pAttTime;           // Attack time
                    plug::IPort            *pRelLevel;          // Release level
                    plug::IPort            *pRelTime;           // Release time
                    plug::IPort            *pRatio;             // Ratio
                    plug::IPort            *pKnee;              // Knee
                    plug::IPort            *pBThresh;           // Boost threshold
                    plug::IPort            *pBoost;             // Boost signal amount
                    plug::IPort            *pMakeup;            // Makeup gain
                    plug::IPort            *pFreqEnd;           // Frequency range end
                    plug::IPort            *pCurveGraph;        // Compressor curve graph
                    plug::IPort            *pRelLevelOut;       // Release level out
                    plug::IPort            *pEnvLvl;            // Envelope level meter
                    plug::IPort            *pCurveLvl;          // Reduction curve level meter
                    plug::IPort            *pMeterGain;         // Reduction gain meter
                } comp_band_t;

                typedef struct split_t
                {
                    bool                    bEnabled;           // Split band is enabled
                    float                   fFreq;              // Split band frequency

                    plug::IPort            *pEnabled;           // Enable port
                    plug::IPort            *pFreq;              // Split frequency
                } split_t;

                typedef struct channel_t
                {
                    dspu::Bypass            sBypass;            // Bypass
                    dspu::Filter            sEnvBoost[2];       // Envelope boost filter
                    dspu::Delay             sDelay;             // Delay for lookahead compensation purpose

                    comp_band_t             vBands[meta::mb_compressor_metadata::BANDS_MAX];     // Compressor bands
                    split_t                 vSplit[meta::mb_compressor_metadata::BANDS_MAX-1];   // Split bands
                    comp_band_t            *vPlan[meta::mb_compressor_metadata::BANDS_MAX];      // Execution plan (band indexes)
                    size_t                  nPlanSize;              // Plan size

                    float                  *vIn;                // Input data buffer
                    float                  *vOut;               // Output data buffer
                    float                  *vScIn;              // Sidechain data buffer (if present)

                    float                  *vInBuffer;          // Input buffer
                    float                  *vBuffer;            // Common data processing buffer
                    float                  *vScBuffer;          // Sidechain buffer
                    float                  *vExtScBuffer;       // External sidechain buffer
                    float                  *vTr;                // Transfer function
                    float                  *vTrMem;             // Transfer buffer (memory)
                    float                  *vInAnalyze;         // Input signal analysis
                    float                  *vOutAnalyze;        // Input signal analysis

                    size_t                  nAnInChannel;       // Analyzer channel used for input signal analysis
                    size_t                  nAnOutChannel;      // Analyzer channel used for output signal analysis
                    bool                    bInFft;             // Input signal FFT enabled
                    bool                    bOutFft;            // Output signal FFT enabled

                    plug::IPort            *pIn;                // Input
                    plug::IPort            *pOut;               // Output
                    plug::IPort            *pScIn;              // Sidechain
                    plug::IPort            *pFftIn;             // Pre-processing FFT analysis data
                    plug::IPort            *pFftInSw;           // Pre-processing FFT analysis control port
                    plug::IPort            *pFftOut;            // Post-processing FFT analysis data
                    plug::IPort            *pFftOutSw;          // Post-processing FFT analysis controlport
                    plug::IPort            *pAmpGraph;          // Compressor's amplitude graph
                    plug::IPort            *pInLvl;             // Input level meter
                    plug::IPort            *pOutLvl;            // Output level meter
                } channel_t;

            protected:
                dspu::Analyzer          sAnalyzer;              // Analyzer
                dspu::DynamicFilters    sFilters;               // Dynamic filters for each band in 'modern' mode
                size_t                  nMode;                  // Compressor mode
                bool                    bSidechain;             // External side chain
                bool                    bEnvUpdate;             // Envelope filter update
                bool                    bModern;                // Modern mode
                size_t                  nEnvBoost;              // Envelope boost
                channel_t              *vChannels;              // Compressor channels
                float                   fInGain;                // Input gain
                float                   fDryGain;               // Dry gain
                float                   fWetGain;               // Wet gain
                float                   fZoom;                  // Zoom
                uint8_t                *pData;                  // Aligned data pointer
                float                  *vSc[2];                 // Sidechain signal data
                float                  *vAnalyze[4];            // Analysis buffer
                float                  *vBuffer;                // Temporary buffer
                float                  *vEnv;                   // Compressor envelope buffer
                float                  *vTr;                    // Transfer buffer
                float                  *vPFc;                   // Pass filter characteristics buffer
                float                  *vRFc;                   // Reject filter characteristics buffer
                float                  *vFreqs;                 // Analyzer FFT frequencies
                float                  *vCurve;                 // Curve
                uint32_t               *vIndexes;               // Analyzer FFT indexes
                core::IDBuffer         *pIDisplay;              // Inline display buffer

                plug::IPort            *pBypass;                // Bypass port
                plug::IPort            *pMode;                  // Global mode
                plug::IPort            *pInGain;                // Input gain port
                plug::IPort            *pOutGain;               // Output gain port
                plug::IPort            *pDryGain;               // Dry gain port
                plug::IPort            *pWetGain;               // Wet gain port
                plug::IPort            *pReactivity;            // Reactivity
                plug::IPort            *pShiftGain;             // Shift gain port
                plug::IPort            *pZoom;                  // Zoom port
                plug::IPort            *pEnvBoost;              // Envelope adjust

            protected:
                static bool compare_bands_for_sort(const comp_band_t *b1, const comp_band_t *b2);
                static dspu::compressor_mode_t    decode_mode(int mode);

            public:
                explicit mb_compressor(const meta::plugin_t *metadata, bool sc, size_t mode);
                virtual ~mb_compressor();

                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports);
                virtual void        destroy();

            public:
                virtual void        update_settings();
                virtual void        update_sample_rate(long sr);
                virtual void        ui_activated();

                virtual void        process(size_t samples);
                virtual bool        inline_display(plug::ICanvas *cv, size_t width, size_t height);

                virtual void        dump(dspu::IStateDumper *v) const;
        };
    } // namespace plugins
} // namespace lsp

#endif /* PRIVATE_PLUGINS_MB_COMPRESSOR_H_ */
