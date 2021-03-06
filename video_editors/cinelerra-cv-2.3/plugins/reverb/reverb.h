
/*
 * CINELERRA
 * Copyright (C) 2008 Adam Williams <broadcast at earthling dot net>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

#ifndef REVERB_H
#define REVERB_H

class Reverb;
class ReverbEngine;

#include "reverbwindow.h"
#include "pluginaclient.h"

class ReverbConfig
{
public:
	ReverbConfig();


	int equivalent(ReverbConfig &that);
	void copy_from(ReverbConfig &that);
	void interpolate(ReverbConfig &prev, 
		ReverbConfig &next, 
		int64_t prev_frame, 
		int64_t next_frame, 
		int64_t current_frame);
	void dump();

	double level_init;
	int64_t delay_init;
	double ref_level1;
	double ref_level2;
	int64_t ref_total;
	int64_t ref_length;
	int64_t lowpass1, lowpass2;
};

class Reverb : public PluginAClient
{
public:
	Reverb(PluginServer *server);
	~Reverb();

	void update_gui();
	int load_from_file(char *data);
	int save_to_file(char *data);
	int load_configuration();
	
// data for reverb
	ReverbConfig config;
	
	char config_directory[1024];

	double **main_in, **main_out;
	double **dsp_in;
	int64_t **ref_channels, **ref_offsets, **ref_lowpass;
	double **ref_levels;
	int64_t dsp_in_length;
	int redo_buffers;
// skirts for lowpass filter
	double **lowpass_in1, **lowpass_in2;
	DB db;
// required for all realtime/multichannel plugins

	int process_realtime(int64_t size, double **input_ptr, double **output_ptr);
	int is_realtime();
	int is_synthesis();
	int is_multichannel();
	const char* plugin_title();
	int show_gui();
	int set_string();
	void save_data(KeyFrame *keyframe);
	void read_data(KeyFrame *keyframe);
	void raise_window();
	VFrame* new_picon();

// non realtime support
	int load_defaults();
	int save_defaults();
	BC_Hash *defaults;
	
	ReverbThread *thread;
	ReverbEngine **engine;
	int initialized;
};

class ReverbEngine : public Thread
{
public:
	ReverbEngine(Reverb *plugin);
	~ReverbEngine();

	void process_overlay(double *in, double *out, double &out1, double &out2, double level, int64_t lowpass, int64_t samplerate, int64_t size);
	void process_overlays(int output_buffer, int64_t size);
	void wait_process_overlays();
	void run();

	Mutex input_lock, output_lock;
	int completed;
	int output_buffer;
	int64_t size;
	Reverb *plugin;
};

#endif
