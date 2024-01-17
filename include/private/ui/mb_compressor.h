/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-mb-compressor
 * Created on: 6 дек. 2023 г.
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
#ifndef PRIVATE_UI_MB_COMPRESSOR_H_
#define PRIVATE_UI_MB_COMPRESSOR_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/lltl/darray.h>

namespace lsp
{
    namespace plugui
    {
        class mb_compressor_ui: public ui::Module, public ui::IPortListener
        {
            protected:
                typedef struct split_t
                {
                    mb_compressor_ui   *pUI;
                    ui::IPort          *pFreq;          // Split frequency port
                    ui::IPort          *pOn;            // Split enable port

                    ui::IPort          *PFilterOn;      // Filter enable port
                    ui::IPort          *pLCF;           // Low cut frequency port
                    ui::IPort          *pHCF;           // High cut frequency port

                    size_t              nChannel;       // Channel (left/right/mid/side)
                    float               fFreq;          // Split frequency
                    bool                bOn;            // Split is enabled
                    bool                bAllocated;     // Split is allocated for some band

                    tk::GraphMarker    *wMarker;        // Graph marker for editing
                    tk::GraphText      *wNote;          // Text with note and frequency

                    tk::GraphDot       *wDot;           // Dot for editing

                    size_t              id;             // Split ID
                } split_t;

            protected:
                ui::IPort              *pCurrentBand;   // Current band port

                tk::Graph              *wGraph;

                lltl::darray<split_t>   vSplits;          // List of split widgets and ports
                lltl::parray<split_t>   vActiveSplits;    // List of split widgets and ports
                ssize_t                 nXAxisIndex;
                ssize_t                 nYAxisIndex;
                size_t                  nCurrentBand;     // Current band
                const char            **fmtStrings;       // List of format strings

            protected:

                static status_t slot_split_mouse_in(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_split_mouse_out(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_band_dot_mouse_down(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_band_dot_mouse_move(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_graph_dbl_click(tk::Widget *sender, void *ptr, void *data);
                static ssize_t  compare_splits_by_freq(const split_t *a, const split_t *b);

            protected:

                template <class T>
                T              *find_widget(const char *fmt, const char *base, size_t id);
                ui::IPort      *find_port(const char *fmt, const char *base, size_t id);
                split_t        *find_split_by_widget(tk::Widget *widget);
                split_t        *find_split_by_port(ui::IPort *port);
                split_t        *find_end_split(split_t *start_split);
                split_t        *allocate_split();

            protected:
                void            on_split_mouse_in(split_t *s);
                void            on_split_mouse_out();

                void            on_band_dot_mouse_down(split_t *b);
                void            on_band_dot_mouse_move(split_t *b);
                void            on_band_dot_move();

                void            on_graph_dbl_click(ssize_t x, ssize_t y);

                ssize_t         find_axis(const char *id);
                void            add_splits();
                void            resort_active_splits();
                void            update_split_note_text(split_t *s);
                void            toggle_active_split_fequency(split_t *initiator);

            public:
                explicit mb_compressor_ui(const meta::plugin_t *meta);
                virtual ~mb_compressor_ui() override;

                virtual status_t    init(ui::IWrapper *wrapper, tk::Display *dpy) override;
                virtual status_t    post_init() override;

                virtual void        notify(ui::IPort *port, size_t flags) override;
        };
    } // namespace plugui
} // namespace lsp

#endif /* PRIVATE_UI_MB_COMPRESSOR_H_ */
