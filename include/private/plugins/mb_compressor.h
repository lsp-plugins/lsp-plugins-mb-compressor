/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
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
#include <lsp-plug.in/dsp-units/ctl/Counter.h>
#include <lsp-plug.in/dsp-units/dynamics/Compressor.h>
#include <lsp-plug.in/dsp-units/filters/DynamicFilters.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>
#include <lsp-plug.in/dsp-units/filters/Filter.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <lsp-plug.in/dsp-units/util/FFTCrossover.h>
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

                enum xover_mode_t
                {
                    XOVER_CLASSIC,                              // Classic mode
                    XOVER_MODERN,                               // Modern mode
                    XOVER_LINEAR_PHASE                          // Linear phase mode
                };

                enum sc_type_t
                {
                    SCT_INTERNAL,
                    SCT_EXTERNAL,
                    SCT_LINK,
                };

            protected:
                enum sync_t
                {
                    S_COMP_CURVE    = 1 << 0,
                    S_EQ_CURVE      = 1 << 1,
                    S_BAND_CURVE    = 1 << 2,

                    S_ALL           = S_COMP_CURVE | S_EQ_CURVE | S_BAND_CURVE
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

                    float                  *vBuffer;            // Crossover band data
                    float                  *vSc;                // Transfer function for sidechain
                    float                  *vTr;                // Transfer function for band
                    float                  *vVCA;               // Voltage-controlled amplification value for each band
                    float                   fScPreamp;          // Sidechain preamp

                    float                   fFreqStart;
                    float                   fFreqEnd;

                    float                   fFreqHCF;           // Cutoff frequency for low-pass filter
                    float                   fFreqLCF;           // Cutoff frequency for high-pass filter
                    float                   fMakeup;            // Makeup gain
                    float                   fGainLevel;         // Gain adjustment level
                    uint32_t                nLookahead;         // Lookahead amount

                    bool                    bEnabled;           // Enabled flag
                    bool                    bCustHCF;           // Custom frequency for high-cut filter
                    bool                    bCustLCF;           // Custom frequency for low-cut filter
                    bool                    bMute;              // Mute channel
                    bool                    bSolo;              // Solo channel
                    uint32_t                nScType;            // Sidechain type
                    uint32_t                nSync;              // Synchronize output data flags
                    uint32_t                nFilterID;          // Identifier of the filter

                    plug::IPort            *pScType;            // Sidechain type
                    plug::IPort            *pScSource;          // Sidechain source
                    plug::IPort            *pScSpSource;        // Sidechain split source
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
                    plug::IPort            *pHold;              // Hold time
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
                    dspu::Filter            sEnvBoost[3];       // Envelope boost filter
                    dspu::Delay             sDelay;             // Delay for lookahead compensation purpose
                    dspu::Delay             sDryDelay;          // Delay for dry signal
                    dspu::Delay             sXOverDelay;        // Delay for crossover
                    dspu::Equalizer         sDryEq;             // Dry equalizer
                    dspu::FFTCrossover      sFFTXOver;          // FFT crossover for linear phase

                    comp_band_t             vBands[meta::mb_compressor_metadata::BANDS_MAX];     // Compressor bands
                    split_t                 vSplit[meta::mb_compressor_metadata::BANDS_MAX-1];   // Split bands
                    comp_band_t            *vPlan[meta::mb_compressor_metadata::BANDS_MAX];      // Execution plan (band indexes)
                    size_t                  nPlanSize;              // Plan size

                    float                  *vIn;                // Input data buffer
                    float                  *vOut;               // Output data buffer
                    float                  *vScIn;              // Sidechain data buffer (if present)
                    float                  *vShmIn;             // Shared memory link buffer (if present)

                    float                  *vInAnalyze;         // Input signal analysis
                    float                  *vInBuffer;          // Input buffer
                    float                  *vBuffer;            // Common data processing buffer
                    float                  *vScBuffer;          // Sidechain buffer
                    float                  *vExtScBuffer;       // External sidechain buffer
                    float                  *vShmBuffer;         // Shared memory link buffer
                    float                  *vTr;                // Transfer function
                    float                  *vTrMem;             // Transfer buffer (memory)

                    size_t                  nAnInChannel;       // Analyzer channel used for input signal analysis
                    size_t                  nAnOutChannel;      // Analyzer channel used for output signal analysis
                    bool                    bInFft;             // Input signal FFT enabled
                    bool                    bOutFft;            // Output signal FFT enabled

                    plug::IPort            *pIn;                // Input
                    plug::IPort            *pOut;               // Output
                    plug::IPort            *pScIn;              // Sidechain
                    plug::IPort            *pShmIn;             // Shared memory link input
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
                dspu::Counter           sCounter;               // Sync counter
                uint32_t                nMode;                  // Compressor mode
                bool                    bSidechain;             // External side chain
                bool                    bEnvUpdate;             // Envelope filter update
                bool                    bUseExtSc;              // External sidechain is in use
                bool                    bUseShmLink;            // Shared memory link is in use
                xover_mode_t            enXOver;                // Crossover mode
                bool                    bStereoSplit;           // Stereo split mode
                uint32_t                nEnvBoost;              // Envelope boost
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
                plug::IPort            *pDryWet;                // Dry/Wet gain balance port
                plug::IPort            *pReactivity;            // Reactivity
                plug::IPort            *pShiftGain;             // Shift gain port
                plug::IPort            *pZoom;                  // Zoom port
                plug::IPort            *pEnvBoost;              // Envelope adjust
                plug::IPort            *pStereoSplit;           // Split left/right independently

            protected:
                static bool compare_bands_for_sort(const comp_band_t *b1, const comp_band_t *b2);
                static dspu::compressor_mode_t      decode_mode(int mode);
                static dspu::sidechain_source_t     decode_sidechain_source(int source, bool split, size_t channel);
                static size_t                       select_fft_rank(size_t sample_rate);
                static void                         process_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count);

            protected:
                void                do_destroy();
                void                preprocess_channel_input(size_t count);
                uint32_t            decode_sidechain_type(uint32_t sc) const;
                void                process_input_mono(float *out, const float *in, size_t count);
                void                process_input_stereo(float *l_out, float *r_out, const float *l_in, const float *r_in, size_t count);
                const float        *select_buffer(const comp_band_t *band, const channel_t *channel);

            public:
                explicit mb_compressor(const meta::plugin_t *metadata, bool sc, size_t mode);
                mb_compressor(const mb_compressor &) = delete;
                mb_compressor(mb_compressor &&) = delete;
                virtual ~mb_compressor() override;

                mb_compressor & operator = (const mb_compressor &) = delete;
                mb_compressor & operator = (mb_compressor &&) = delete;

                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void        destroy() override;

            public:
                virtual void        update_settings() override;
                virtual void        update_sample_rate(long sr) override;
                virtual void        ui_activated() override;

                virtual void        process(size_t samples) override;
                virtual bool        inline_display(plug::ICanvas *cv, size_t width, size_t height) override;

                virtual void        dump(dspu::IStateDumper *v) const override;
        };

    } /* namespace plugins */
} /* namespace lsp */

#endif /* PRIVATE_PLUGINS_MB_COMPRESSOR_H_ */
