#include <algorithm>
#include <math.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include "getopt.h"
#else
#include <getopt.h>
#endif
#include "midi2bgm.h"



int num_tracks;
int drum_track = -1;



unsigned short flip_u16(unsigned short val)
{
    return (((val & 0xFF00) >> 8) | ((val & 0x00FF) << 8));
}

unsigned long flip_u32(unsigned long val)
{
    return (((val & 0xFF000000) >> 24) | ((val & 0x00FF0000) >> 8) | ((val & 0x0000FF00) << 8) | ((val & 0x000000FF) << 24));
}

unsigned short char_array_to_short(unsigned char* ptr)
{
    return flip_u16(*reinterpret_cast<unsigned short*> (ptr));
}

unsigned long char_array_to_long(unsigned char* ptr)
{
    return flip_u32(*reinterpret_cast<unsigned long*> (ptr));
}


void write_u32(unsigned char* buf, unsigned long addr, unsigned long data)
{
    buf[addr] = ((data >> 24) & 0xFF);
    buf[addr+1] = ((data >> 16) & 0xFF);
    buf[addr+2] = ((data >> 8) & 0xFF);
    buf[addr+3] = ((data) & 0xFF);
}

void write_u16(unsigned char* buf, unsigned long addr, unsigned short data)
{
    buf[addr] = ((data >> 8) & 0xFF);
    buf[addr+1] = ((data) & 0xFF);  
}

bool does_overlap(float x1, float x2, float y1, float y2)
{
    return x2 > y1 && y2 > x1;
}



unsigned long get_vl_bytes(u8* vl_bytes, int& offset, unsigned long& original, u8*& alt_pattern, u8& alt_offset, u8& alt_length, bool include_fe_repeats)
{
    unsigned long vl_val = 0; //Vlength Value.
    u8 tmp_byte; //Byte value read.

    while (true)
    {
        if (alt_pattern != NULL)
        {
            tmp_byte = alt_pattern[alt_offset];
            alt_offset++;

            if (alt_offset == alt_length)
            {
                delete [] alt_pattern;
                alt_pattern = NULL;
                alt_offset = 0;
                alt_length = 0;
            }
        }
        else
        {
            tmp_byte = vl_bytes[offset];
            offset++;

            if ((tmp_byte == 0xFE) && (vl_bytes[offset] != 0xFE) && include_fe_repeats)
            {
                u8 repeat_first_byte = vl_bytes[offset];
                offset++;

                unsigned short repeat_distance = ((repeat_first_byte << 8) | vl_bytes[offset]);
                offset++;
                u8 repeat_count = vl_bytes[offset];
                offset++;

                alt_pattern = new u8[repeat_count];
                for (int copy = ((offset - 4) - repeat_distance); copy < (((offset - 4) - repeat_distance) + repeat_count); copy++)
                {
                    alt_pattern[copy - ((offset - 4) - repeat_distance)] = vl_bytes[copy];
                }
                alt_offset = 0;
                alt_length = repeat_count;

                tmp_byte = alt_pattern[alt_offset];
                alt_offset++;
            }
            else if ((tmp_byte == 0xFE) && (vl_bytes[offset] == 0xFE) && include_fe_repeats)
            {
                // skip duplicate FEs
                offset++;
            }

            if ((alt_offset == alt_length) && (alt_pattern != NULL))
            {
                delete [] alt_pattern;
                alt_pattern = NULL;
                alt_offset = 0;
                alt_length = 0;
            }
        }
        if ((tmp_byte >> 7) == 0x1)
        {
            vl_val += tmp_byte;
            vl_val = vl_val << 8; //Shift to next byte in VLVal.
        }
        else
        {
            vl_val += tmp_byte;
            break;
        } 
    }
    
    original = vl_val;

    unsigned long vlength = 0;

    for (int c = 0, a = 0; ;c += 8, a+= 7)
    {
        vlength += (((vl_val >> c) & 0x7F) << a);
        if (c == 24)
            break;
    }
    return vlength;
}



u8 read_midi_byte(u8* vl_bytes, int& offset, u8*& alt_pattern, u8& alt_offset, u8& alt_length, bool include_fe_repeats)
{
    u8 return_byte;
    if (alt_pattern != NULL)
    {
        return_byte = alt_pattern[alt_offset];
        alt_offset++;
    }
    else
    {
        return_byte = vl_bytes[offset];
        offset++;

        if ((return_byte == 0xFE) && (vl_bytes[offset] != 0xFE) && include_fe_repeats)
        {
            u8 repeat_first_byte = vl_bytes[offset];
            offset++;

            unsigned long repeat_distance = ((repeat_first_byte << 8) | vl_bytes[offset]);
            offset++;
            u8 repeat_count = vl_bytes[offset];
            offset++;

            alt_pattern = new u8[repeat_count];
            for (int copy = ((offset - 4) - repeat_distance); copy < (((offset - 4) - repeat_distance) + repeat_count); copy++)
            {
                alt_pattern[copy - ((offset - 4) - repeat_distance)] = vl_bytes[copy];
            }
            alt_offset = 0;
            alt_length = repeat_count;

            return_byte = alt_pattern[alt_offset];
            alt_offset++;
        }
        else if ((return_byte == 0xFE) && (vl_bytes[offset] == 0xFE) && include_fe_repeats)
        {
            // skip duplicate FEs
            offset++;
        }
    }

    if ((alt_offset == alt_length) && (alt_pattern != NULL))
    {
        delete [] alt_pattern;
        alt_pattern = NULL;
        alt_offset = 0;
        alt_length = 0;
    }
    
    return return_byte;
}



void write_delay(unsigned long delay, unsigned char* out_buf, int& out_pos)
{
    while (delay > 0)
    {
        if (delay < 0x78)
        {
            out_buf[out_pos++] = delay;
            delay = 0;
        }
        else
        {
            delay = delay - 0x78;
            unsigned long mask_low_extra = delay >> 8;

            if (mask_low_extra > 7)
                mask_low_extra = 7;

            out_buf[out_pos++] = 0x78 | mask_low_extra;

            delay = delay - (mask_low_extra << 8);

            unsigned long extra_byte = 0;
            if (delay > 0)
            {
                if (delay > 0x78)
                    extra_byte = 0x78;
                else
                    extra_byte = delay;
            }

            out_buf[out_pos++] = extra_byte;

            delay = delay - extra_byte;
        }
    }
}






bool midi_to_song_list(const char* input, std::vector<time_value>& tempo_positions, std::vector<song_midi_note_info> channels[MAXCHANNELS], int& num_channels, std::vector<int>& instruments, int& lowest_time, int& highest_time, bool loop, unsigned long loop_point, unsigned short & division)
{
    num_channels = 0;
    lowest_time = 0x7FFFFFFF;
    highest_time = 0;

    int note_id = 0;

    num_tracks = 0;

    


    const char* tmp_file = input;

    FILE* fp = fopen(tmp_file, "rb");
    if (fp == NULL)
    {
        printf("[!] Error reading file: '%s'\n", tmp_file);
        return false;
    }   

    fseek(fp, 0L, SEEK_END);
    u32 filesize = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    u8* midi_data = new u8[filesize];
    fread(midi_data, 1, filesize, fp);
    fclose(fp);

    unsigned long header = char_array_to_long(&midi_data[0]);

    // "MThd"
    if (header != 0x4D546864)
    {
        delete [] midi_data;
        printf("[!] Invalid midi hdr\n");
        return false;
    }

    unsigned long header_length = char_array_to_long(&midi_data[4]);

    unsigned short type = char_array_to_short(&midi_data[8]);
    unsigned short num_tracks = char_array_to_short(&midi_data[0xA]);
    division = char_array_to_short(&midi_data[0xC]);

    if (num_tracks > 0x10)
    {
        delete [] midi_data;
        printf("[!] Invalid, can only support 16 tracks, first 1 only tempo\n");
        return false;
    }

    float note_time_divisor = division / 0x30;

    loop_point = (float)loop_point / note_time_divisor;

    if (type == 0)
    {
        
    }
    else if (type == 1)
    {

    }
    else
    {
        delete [] midi_data;

        printf("[!] Invalid midi type\n");
        return false;
    }



    int position = 0xE;

    u8* repeat_pattern = NULL;
    u8 alt_pattern = 0;
    u8 alt_length = 0;

    bool unknowns_hit = false;
    for (int track_num = 0; track_num < num_tracks; track_num++)
    {
        std::vector<song_midi_note_info> pending_note_list;

        int cur_segment[0x10];
        unsigned char cur_pan[0x10];
        unsigned char cur_volume[0x10];
        unsigned char cur_reverb[0x10];
        signed char cur_pitch_bend[0x10];

        unsigned char cur_msb_bank[0x10];
        unsigned char cur_lsb_bank[0x10];
        unsigned char cur_instrument[0x10];

        // Controllers defaults
        for (int x = 0; x < 0x10; x++)
        {
            cur_pan[x] = 0x40;
            cur_volume[x] = 0x7F;
            cur_reverb[x] = 0x00;
            cur_instrument[x] = 0x00;
            cur_pitch_bend[x] = 0x40;

            cur_msb_bank[x] = 0x00;
            cur_lsb_bank[x] = 0x00;
            cur_segment[x] = 0x00;
        }

        unsigned long time = 0;
        float time_float = 0;

        unsigned long track_header = ((((((midi_data[position] << 8) | midi_data[position+1]) << 8) | midi_data[position+2]) << 8) | midi_data[position+3]);
        if (track_header != 0x4D54726B)
        {
            delete [] midi_data;

            printf("[!] Invalid track midi hdr\n");
            return false;
        }
        
        unsigned long track_length = ((((((midi_data[position+4] << 8) | midi_data[position+5]) << 8) | midi_data[position+6]) << 8) | midi_data[position+7]);

        position += 8;

        u8 prev_event_value = 0xFF;

        bool end_flag = false;

        while (!end_flag && (position < filesize))
        {
            unsigned long original;
            unsigned long time_tag = get_vl_bytes(midi_data, position, original, repeat_pattern, alt_pattern, alt_length, false);
            time_float += (float)time_tag / note_time_divisor;
            
            time = time_float;

            u8 event_val = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);

            bool status_bit = false;

            if (event_val <= 0x7F)
            {
                // continuation
                status_bit = true;
            }
            else
            {
                status_bit = false;
            }

            if (event_val == 0xFF) // meta event
            {
                u8 sub_type = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);

                if (sub_type == 0x2F) //End of Track Event.
                {
                    end_flag = true;

                    unsigned long length = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);  // end 00 in real mid
                }
                else if (sub_type == 0x51) //Set Tempo Event.
                {
                    unsigned long length = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false); 

                    unsigned char byte_data[3];
                    byte_data[0] = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    byte_data[1] = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    byte_data[2] = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);

                    unsigned long quarter_note_delta = ((((byte_data[0] << 8) | byte_data[1]) << 8) | byte_data[2]);
                    unsigned long tmp_tempo = 60000000.0 / quarter_note_delta;

                    if (tmp_tempo > 255)
                        tmp_tempo = 255;
                    else if (tmp_tempo < 1)
                        tmp_tempo = 1;
                    else
                        tmp_tempo = (unsigned char)tmp_tempo;

                    bool match_tempo = false;
                    for (int y = 0; y < tempo_positions.size(); y++)
                    {
                        if (tempo_positions[y].time == time)
                        {
                            match_tempo = true;
                        }
                    }

                    if (!match_tempo)
                    {
                        tempo_positions.push_back(time_value(time, tmp_tempo));
                    }
                }
                // Various Unused Meta Events
                else if ((sub_type < 0x7F) && !(sub_type == 0x51 || sub_type == 0x2F))
                {
                    unsigned long length = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false); 

                    for (int i = 0; i < length; i++)
                        read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                }
                // Unused Sequencer Specific Event
                else if (sub_type == 0x7F)
                {
                    int length = get_vl_bytes(midi_data, position, original, repeat_pattern, alt_pattern, alt_length, false);
                    // subtract length
                    for (int i = 0; i < length; i++)
                    {
                        read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    }
                }

                prev_event_value = event_val;
            }
            // Note off
            else if ((event_val >= 0x80 && event_val < 0x90) || (status_bit && (prev_event_value >= 0x80 && prev_event_value < 0x90)))
            {
                u8 cur_event_val;

                u8 note_number;
                if (status_bit)
                {
                    note_number = event_val;
                    cur_event_val = prev_event_value;
                }
                else
                {
                    note_number = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    cur_event_val = event_val;
                }
                u8 velocity = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);

                int controller = (cur_event_val & 0xF);

                for (int p = 0; p < pending_note_list.size(); p++)
                {
                    if (pending_note_list[p].orig_controller != (controller))
                        continue;

                    // Go backwards in list
                    if (pending_note_list[p].note_number == note_number)
                    {
                        pending_note_list[p].end_time = time;

                        // Promote to regular
                        channels[track_num].push_back(pending_note_list[p]);

                        pending_note_list.erase(pending_note_list.begin() + p);
                        break;
                    }
                }

                if (!status_bit)
                    prev_event_value = event_val;
            }
            else if ((event_val >= 0x90 && event_val < 0xA0) || (status_bit && (prev_event_value >= 0x90 && prev_event_value < 0xA0)))
            {
                u8 cur_event_val;

                u8 note_number;
                if (status_bit)
                {
                    note_number = event_val;
                    cur_event_val = prev_event_value;
                }
                else
                {
                    note_number = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    cur_event_val = event_val;
                }
                u8 velocity = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);

                int controller = (cur_event_val & 0xF);

                if (velocity == 0)
                {
                    for (int p = 0; p < pending_note_list.size(); p++)
                    {
                        if (pending_note_list[p].orig_controller != (controller))
                            continue;

                        // Go backwards in list
                        if (pending_note_list[p].note_number == note_number)
                        {
                            pending_note_list[p].end_time = time;

                            // Promote to regular
                            channels[track_num].push_back(pending_note_list[p]);

                            pending_note_list.erase(pending_note_list.begin() + p);
                            break;
                        }
                    }
                }
                else
                {
                    // If wasn't shut off, turn it off from before, then start new note
                    for (int p = 0; p < pending_note_list.size(); p++)
                    {
                        if (pending_note_list[p].orig_controller != (controller))
                            continue;

                        // Go backwards in list
                        if (pending_note_list[p].note_number == note_number)
                        {
                            pending_note_list[p].end_time = time;

                            // Promote to regular
                            channels[track_num].push_back(pending_note_list[p]);

                            pending_note_list.erase(pending_note_list.begin() + p);
                            break;
                        }
                    }

                    song_midi_note_info new_song_info;
                    new_song_info.orig_controller = controller;
                    new_song_info.orig_track = track_num;
                    new_song_info.orig_note_id = note_id++;
                    new_song_info.note_number = note_number;
                    new_song_info.velocity = velocity;
                    new_song_info.pan = cur_pan[controller];
                    new_song_info.volume = cur_volume[controller];
                    new_song_info.effect = cur_reverb[controller];
                    new_song_info.instrument = cur_instrument[controller] + (cur_lsb_bank[controller] * 0x80) + (cur_msb_bank[controller] * 0x8000);
                    new_song_info.segment_number = cur_segment[controller];
                    new_song_info.pitch_bend = cur_pitch_bend[controller];
                    new_song_info.start_time = time;
                    pending_note_list.push_back(new_song_info);
                }

                if (!status_bit)
                    prev_event_value = event_val;
            }
            else if (((event_val >= 0xB0) && (event_val < 0xC0))  || (status_bit && (prev_event_value >= 0xB0 && prev_event_value < 0xC0))) // controller change
            {
                u8 controller_type;
                unsigned char cur_event_val;

                if (status_bit)
                {
                    controller_type = event_val;
                    cur_event_val = prev_event_value;
                }
                else
                {
                    controller_type = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    cur_event_val = event_val;
                }

                int controller = (cur_event_val & 0xF);

                u8 controller_value = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);

                if (controller_type == 0) // MSB Instrument Bank
                {
                    cur_msb_bank[controller] = controller_value;    
                }
                else if (controller_type == 7) // Volume
                {
                    if (controller_value != cur_volume[controller])
                    {
                        for (int p = 0; p < pending_note_list.size(); p++)
                        {
                            if (pending_note_list[p].orig_controller != (controller))
                                continue;

                            // Reopen
                            if (pending_note_list[p].start_time != time)
                            {
                                pending_note_list[p].end_time = time;
                    
                                // Promote to regular
                                channels[track_num].push_back(pending_note_list[p]);

                                // Reset
                                pending_note_list[p].start_time = time;
                                pending_note_list[p].end_time = 0xFFFFFFFF;
                            }
                            
                            pending_note_list[p].volume = controller_value;
                        }
                    }

                    cur_volume[controller] = controller_value;  
                }
                else if (controller_type == 10) // Pan
                {
                    if (controller_value != cur_pan[controller])
                    {
                        for (int p = 0; p < pending_note_list.size(); p++)
                        {
                            if (pending_note_list[p].orig_controller != (controller))
                                continue;

                            // Reopen
                            if (pending_note_list[p].start_time != time)
                            {
                                pending_note_list[p].end_time = time;
                    
                                // Promote to regular
                                channels[track_num].push_back(pending_note_list[p]);

                                // Reset
                                pending_note_list[p].start_time = time;
                                pending_note_list[p].end_time = 0xFFFFFFFF;
                            }
                            
                            pending_note_list[p].pan = controller_value;
                        }
                    }

                    cur_pan[controller] = controller_value;
                }
                else if (controller_type == 32) // LSB Instrument Bank
                {
                    cur_lsb_bank[controller] = controller_value;    
                }
                else if (controller_type == 91) // Reverb
                {
                    if (controller_value != cur_reverb[controller])
                    {
                        for (int p = 0; p < pending_note_list.size(); p++)
                        {
                            if (pending_note_list[p].orig_controller != (controller))
                                continue;

                            // Reopen
                            if (pending_note_list[p].start_time != time)
                            {
                                pending_note_list[p].end_time = time;
                    
                                // Promote to regular
                                channels[track_num].push_back(pending_note_list[p]);

                                // Reset
                                pending_note_list[p].start_time = time;
                                pending_note_list[p].end_time = 0xFFFFFFFF;
                            }
                            
                            pending_note_list[p].effect = controller_value;
                        }
                    }

                    cur_reverb[controller] = controller_value;
                }
                else if (controller_type == 104) // Segment
                {
                    if (controller_value >= cur_segment[controller])
                    {
                        cur_segment[controller] = controller_value;
                    }
                }

                if (!status_bit)
                    prev_event_value = event_val;
            }
            else if (((event_val >= 0xC0) && (event_val < 0xD0)) || (status_bit && (prev_event_value >= 0xC0 && prev_event_value < 0xD0))) // change instrument
            {
                u8 instrument;
                unsigned char cur_event_val;

                if (status_bit)
                {
                    instrument = event_val;
                    cur_event_val = prev_event_value;
                }
                else
                {
                    instrument = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    cur_event_val = event_val;
                }

                if ((event_val & 0xF) == 9) // Drums in GM
                    instrument = instrument;
                else
                    instrument = instrument;

                int controller = (cur_event_val & 0xF);

                unsigned short tmp_instrument = instrument + (cur_lsb_bank[controller] * 0x80) + (cur_msb_bank[controller] * 0x8000);
                
                for (int p = 0; p < pending_note_list.size(); p++)
                {
                    if (pending_note_list[p].orig_controller != (controller))
                        continue;

                    if (pending_note_list[p].instrument != tmp_instrument)
                    {
                        // Reopen
                        if (pending_note_list[p].start_time != time)
                        {
                            pending_note_list[p].end_time = time;
                
                            // Promote to regular
                            channels[track_num].push_back(pending_note_list[p]);

                            // Reset
                            pending_note_list[p].start_time = time;
                            pending_note_list[p].end_time = 0xFFFFFFFF;
                        }

                        pending_note_list[p].instrument = tmp_instrument;
                    }
                }

                cur_instrument[controller] = instrument;
                
                if (!status_bit)
                    prev_event_value = event_val;
            }
            else if (((event_val >= 0xD0) && (event_val < 0xE0))  || (status_bit && (prev_event_value >= 0xD0 && prev_event_value < 0xE0))) // channel aftertouch
            {
                unsigned char cur_event_val;
                u8 amount;
                if (status_bit)
                {
                    amount = event_val;
                    cur_event_val = prev_event_value;
                }
                else
                {
                    amount = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    cur_event_val = event_val;
                }

                if (!status_bit)
                    prev_event_value = event_val;
            }
            // Pitch Bend
            else if (((event_val >= 0xE0) && (event_val < 0xF0))  || (status_bit && (prev_event_value >= 0xE0 && prev_event_value < 0xF0))) // pitch bend
            {
                u8 value_lsb;

                unsigned char cur_event_val;
                if (status_bit)
                {
                    value_lsb = event_val;
                    cur_event_val = prev_event_value;
                }
                else
                {
                    value_lsb = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                    cur_event_val = event_val;
                }

                u8 value_msb = read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);

                int controller = (cur_event_val & 0xF);

                if (cur_pitch_bend[controller] != value_msb)
                {
                    for (int p = 0; p < pending_note_list.size(); p++)
                    {
                        if (pending_note_list[p].orig_controller != (controller))
                            continue;

                        // Reopen
                        if (pending_note_list[p].start_time != time)
                        {
                            pending_note_list[p].end_time = time;
                
                            // Promote to regular
                            channels[track_num].push_back(pending_note_list[p]);

                            // Reset
                            pending_note_list[p].start_time = time;
                            pending_note_list[p].end_time = 0xFFFFFFFF;
                        }
                        
                        pending_note_list[p].pitch_bend = value_msb;
                    }
                }

                cur_pitch_bend[controller] = value_msb;

                if (!status_bit)
                    prev_event_value = event_val;
            }
            else if (event_val == 0xF0 || event_val == 0xF7)
            {
                unsigned char cur_event_val = event_val;
                int length = get_vl_bytes(midi_data, position, original, repeat_pattern, alt_pattern, alt_length, false);
                // subtract length
                for (int i = 0; i < length; i++)
                {
                    read_midi_byte(midi_data, position, repeat_pattern, alt_pattern, alt_length, false);
                }
            }
            else
            {
                if (!unknowns_hit)
                {
                    printf("[!] Invalid midi character found\n");
                    unknowns_hit = true;
                }
            }
        }

        for (int p = 0; p < pending_note_list.size(); p++)
        {
            pending_note_list[p].end_time = time;
            channels[track_num].push_back(pending_note_list[p]);
        }

        // Clear empty notes
        for (int x = (channels[track_num].size() - 1); x >= 0; x--)
        {
            if (channels[track_num][x].start_time == channels[track_num][x].end_time)
                channels[track_num].erase(channels[track_num].begin() + x);
        }

        for (int x = 0; x < channels[track_num].size(); x++)
        {
            song_midi_note_info tmp_note_info = channels[track_num][x];

            if (tmp_note_info.end_time > highest_time)
                highest_time = tmp_note_info.end_time;

            if (tmp_note_info.start_time < lowest_time)
                lowest_time = tmp_note_info.start_time;
        }
    }
    
    delete [] midi_data;

    num_channels = num_tracks;

    if (num_channels == 0)
    {
        printf("[!] No Channels\n");
        return false;
    }

    if (loop && (loop_point > highest_time))
    {

        printf("[!] Error, loop point is beyond end of midi\n");
        return false;
    }

    std::sort(tempo_positions.begin(), tempo_positions.end(), time_value_sort());

    for (int x = 0; x < num_channels; x++)
    {
        bool renumbered_loop = false;
        int renumber_segment = -1;

        // Separate
        if (loop && (loop_point != 0))
        {
            for (int y = (channels[x].size() - 1); y >= 0; y--)
            {
                song_midi_note_info note_midi_import = channels[x][y];
                if ((loop_point > note_midi_import.start_time) && (loop_point < note_midi_import.end_time))
                {
                    // Need to split
                    channels[x][y].end_time = loop_point;

                    note_midi_import.start_time = loop_point;
                    channels[x].push_back(note_midi_import);

                    renumbered_loop = true;
                    renumber_segment = note_midi_import.segment_number;
                }
            }
        }

        std::stable_sort(channels[x].begin(), channels[x].end(), song_sort_by_start());

        if (renumbered_loop)
        {
            for (int y = 0; y < channels[x].size(); y++)
            {
                if (channels[x][y].segment_number == renumber_segment)
                {
                    if (channels[x][y].start_time >= loop_point)
                    {
                        channels[x][y].segment_number = -1;
                    }
                }
            }
        }
    }

    return true;
}



bool convert_to_bgm(const char* output, std::vector<song_drums> drums, std::vector<song_instrument> instruments, song_segment_info song_segments[4], unsigned long name, bool loop)
{

    int output_pos = 0;

    unsigned char* output_buf = new unsigned char[0x100000];

    for (int x = 0; x < 0x100000; x++)
        output_buf[x] = 0x00;

    // "BGM "
    write_u32(output_buf, output_pos, 0x42474D20);
    output_pos += 4;    // 0x0004
    
    // BGM size
    int final_size_pos = output_pos;
    output_pos += 4;    // 0x0008

    // BGM index
    write_u32(output_buf, output_pos, name);
    output_pos += 3;    // 0x000C

    // Make sure space is present
    output_buf[output_pos++] = ' ';

    // 0x00000000
    write_u32(output_buf, output_pos, 0x00000000);
    output_pos += 4;    // 0x0010

    // 0x04 0x00 0x00 0x00
    int num_segments = 4;
    output_buf[output_pos] = 0x04;
    output_buf[output_pos + 1] = 0x00;
    output_buf[output_pos + 2] = 0x00;
    output_buf[output_pos + 3] = 0x00;
    output_pos += 4;    // 0x0014

    int segment_offset_pos = output_pos;
    output_pos += (2 * num_segments);   // 0x001C

    int drum_offset_pos = output_pos;
    output_pos += 2;    // 0x001E
    write_u16(output_buf, output_pos, drums.size());
    output_pos += 2;    // 0x0020

    int instrument_offset_pos = output_pos;
    output_pos += 2;    // 0x0022
    write_u16(output_buf, output_pos, instruments.size());
    output_pos += 2;    // 0x0024

    if (drums.size() > 0)
        write_u16(output_buf, drum_offset_pos, (output_pos >> 2));
    else
        write_u16(output_buf, drum_offset_pos, 0);

    for (int x = 0; x < drums.size(); x++)
    {
        output_buf[output_pos++] = drums[x].flags;
        output_buf[output_pos++] = drums[x].instrument;
        output_buf[output_pos++] = drums[x].unknown2;
        output_buf[output_pos++] = drums[x].unknown3;
        output_buf[output_pos++] = drums[x].volume;
        output_buf[output_pos++] = drums[x].pan;
        output_buf[output_pos++] = drums[x].effect;
        output_buf[output_pos++] = drums[x].unknown7;
        output_buf[output_pos++] = drums[x].unknown8;
        output_buf[output_pos++] = drums[x].unknown9;
        output_buf[output_pos++] = drums[x].unknownA;
        output_buf[output_pos++] = drums[x].unknownB;
    }
    
    if (instruments.size() > 0)
        write_u16(output_buf, instrument_offset_pos, (output_pos >> 2));
    else
        write_u16(output_buf, instrument_offset_pos, 0x0000);

    for (int x = 0; x < instruments.size(); x++)
    {
        output_buf[output_pos++] = instruments[x].flags;
        output_buf[output_pos++] = instruments[x].instrument;
        output_buf[output_pos++] = instruments[x].volume;
        output_buf[output_pos++] = instruments[x].pan;
        output_buf[output_pos++] = instruments[x].effect;
        output_buf[output_pos++] = instruments[x].unknown5;
        output_buf[output_pos++] = instruments[x].unknown6;
        output_buf[output_pos++] = instruments[x].unknown7;
    }

    int segment_offset_ptrs[4];
    std::vector<int> sub_segments[4];
    for (int segment = 0; segment < num_segments; segment++)
    {
        bool has_note = false;

        for (int track_number = 0; track_number < 0x10; track_number++)
        {
            if (song_segments[segment].song_segment_tracks[track_number].song_note_list.size() > 0)
            {
                has_note = true;
                break;
            }
        }

        if (has_note)
        {
            write_u16(output_buf, segment_offset_pos + (segment * 2), (output_pos >> 2));
            segment_offset_ptrs[segment] = output_pos;
        }
        else
        {
            write_u16(output_buf, segment_offset_pos + (segment * 2), 0x0000);
            segment_offset_ptrs[segment] = 0x00000000;
            continue;
        }

        for (int track_number = 0; track_number < 0x10; track_number++)
        {
            for (int y = 0; y < song_segments[segment].song_segment_tracks[track_number].song_note_list.size(); y++)
            {
                if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].segment_number != -1)
                {
                    if (std::find(sub_segments[segment].begin(), sub_segments[segment].end(), song_segments[segment].song_segment_tracks[track_number].song_note_list[y].segment_number) == sub_segments[segment].end())
                    {
                        sub_segments[segment].push_back(song_segments[segment].song_segment_tracks[track_number].song_note_list[y].segment_number);
                    }
                }
            }
        }

        std::sort(sub_segments[segment].begin(), sub_segments[segment].end());

        bool did_special_30 = false;

        if (sub_segments[segment].size() > 0)
        {
            for (int x = 0; x < (sub_segments[segment][sub_segments[segment].size() - 1] + 1); x++)
            {
                // Skip one
                if (std::find(sub_segments[segment].begin(), sub_segments[segment].end(), x) == sub_segments[segment].end())
                {
                    write_u32(output_buf, output_pos, 0x30000000);
                    did_special_30 = true;
                }
                output_pos += 4;
            }
        }

        if (did_special_30)
        {
            write_u32(output_buf, output_pos, 0x50000000);
            output_pos += 4;
        }
        // Last 00000000 terminator
        output_pos += 4;
    }

    for (int segment = 0; segment < num_segments; segment++)
    {
        if (sub_segments[segment].size() > 0)
        {
            bool is_after_loop_sub_segment = false;
            for (int x = 0; x < (sub_segments[segment][sub_segments[segment].size() - 1] + 1); x++)
            {
                int max_time = 0;

                if (std::find(sub_segments[segment].begin(), sub_segments[segment].end(), x) != sub_segments[segment].end())
                {
                    for (int track_number = 0; track_number < 0x10; track_number++)
                    {
                        for (int y = 0; y < song_segments[segment].song_segment_tracks[track_number].song_note_list.size(); y++)
                        {
                            if (x == song_segments[segment].song_segment_tracks[track_number].song_note_list[y].segment_number)
                            {
                                if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].end_time > max_time)
                                {
                                    max_time = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].end_time;
                                }
                            }
                        }
                    }

                    if (song_segments[segment].song_sub_segment.find(x) != song_segments[segment].song_sub_segment.end())
                    {
                        if (max_time < song_segments[segment].song_sub_segment[x].end_time)
                        {
                            max_time = song_segments[segment].song_sub_segment[x].end_time;
                        }
                    }

                    bool is_drum_track = false;

                    write_u32(output_buf, segment_offset_ptrs[segment] + (x * 4), 0x10000000 | ((output_pos - segment_offset_ptrs[segment]) >> 2));

                    int track_ptr_offset = output_pos;

                    output_pos += 0x40;

                    for (int track_number = 0; track_number < 0x10; track_number++)
                    {
                        if ((track_number == 0) || (song_segments[segment].song_segment_tracks[track_number].song_note_list.size() > 0))
                        {
                            // Flags
                            //0x0080 Drum Track
                            if (is_drum_track)
                            {
                                write_u16(output_buf, track_ptr_offset + (track_number * 0x4), ((output_pos - track_ptr_offset)));
                                // Flags
                                write_u16(output_buf, track_ptr_offset + (track_number * 0x4) + 2, 0xE080);
                            }
                            else
                            {
                                write_u16(output_buf, track_ptr_offset + (track_number * 0x4), ((output_pos - track_ptr_offset)));
                                // Flags
                                if (track_number == drum_track)
                                    write_u16(output_buf, track_ptr_offset + (track_number * 0x4) + 2, 0xA080);
                                else
                                    write_u16(output_buf, track_ptr_offset + (track_number * 0x4) + 2, 0xA000);
                            }
                        }
                        else
                        {
                            write_u16(output_buf, track_ptr_offset + (track_number * 0x4), 0x0000);
                            write_u16(output_buf, track_ptr_offset + (track_number * 0x4) + 2, 0x0000);
                            continue;
                        }

                        int time = 0;
                        int start_time = 0;

                        if (song_segments[segment].song_sub_segment.find(x) != song_segments[segment].song_sub_segment.end())
                        {
                            time = song_segments[segment].song_sub_segment[x].start_time;
                            start_time = song_segments[segment].song_sub_segment[x].start_time;;
                        }

                        if (track_number == 0)
                        {
                            // Tempo only 
                            for (int t = 0; t < song_segments[segment].tempo_positions.size(); t++)
                            {
                                if (song_segments[segment].tempo_positions[t].time >= start_time)
                                {
                                    if (song_segments[segment].tempo_positions[t].time >= max_time)
                                        break;

                                    if ((t > 0) && (song_segments[segment].tempo_positions[t].value == song_segments[segment].tempo_positions[t - 1].value))
                                        continue;

                                    if (song_segments[segment].tempo_positions[t].time > time)
                                    {
                                        write_delay((song_segments[segment].tempo_positions[t].time - time), output_buf, output_pos);
                                        time = song_segments[segment].tempo_positions[t].time;
                                    }

                                    unsigned short tempo_value = song_segments[segment].tempo_positions[t].value;

                                    output_buf[output_pos++] = 0xE0;
                                    write_u16(output_buf, output_pos, tempo_value);
                                    output_pos += 2;

                                    if (t == 0)
                                    {
                                        // Write Master Volume
                                        output_buf[output_pos++] = 0xE1;
                                        output_buf[output_pos++] = 0x64;

                                        // Write Effect
                                        output_buf[output_pos++] = 0xE6;
                                        write_u16(output_buf, output_pos, 0x0001);
                                        output_pos += 2;
                                    }
                                }
                            }
                        }
                        else
                        {
                            int cur_instrument = -1;
                            int cur_volume = -1;
                            int cur_pan = -1;
                            int cur_reverb = -1;

                            for (int y = 0; y < song_segments[segment].song_segment_tracks[track_number].song_note_list.size(); y++)
                            {
                                if (x == song_segments[segment].song_segment_tracks[track_number].song_note_list[y].segment_number)
                                {
                                    if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].start_time > time)
                                    {
                                        write_delay((song_segments[segment].song_segment_tracks[track_number].song_note_list[y].start_time - time), output_buf, output_pos);
                                        time = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].start_time;
                                    }

                                    if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].effect != cur_reverb)
                                    {
                                        output_buf[output_pos++] = 0xEB;
                                        output_buf[output_pos++] = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].effect;
                                        cur_reverb = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].effect;
                                    }

                                    if (is_drum_track)
                                    {
                                        if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].volume != cur_volume)
                                        {
                                            output_buf[output_pos++] = 0xE9;
                                            output_buf[output_pos++] = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].volume;

                                            cur_volume = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].volume;
                                        }

                                        if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].pan != cur_pan)
                                        {
                                            output_buf[output_pos++] = 0xEA;
                                            output_buf[output_pos++] = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].pan;

                                            cur_pan = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].pan;
                                        }

                                        // TODO do this
                                    }
                                    else
                                    {
                                        if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].instrument != cur_instrument)
                                        {
                                            bool found_instrument = false;
                                            for (int i = 0; i < instruments.size(); i++)
                                            {
                                                if (instruments[i].instrument == song_segments[segment].song_segment_tracks[track_number].song_note_list[y].instrument)
                                                {
                                                    found_instrument = true;
                                                    output_buf[output_pos++] = 0xF5;
                                                    output_buf[output_pos++] = instruments[i].instrument;

                                                    cur_instrument = instruments[i].instrument;
                                                    break;
                                                }
                                            }

                                            if (!found_instrument)
                                            {
                                                output_buf[output_pos++] = 0xE8;
                                                output_buf[output_pos++] = 0x30;
                                                output_buf[output_pos++] = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].instrument;

                                                cur_instrument = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].instrument;
                                            }
                                        }

                                        if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].volume != cur_volume)
                                        {
                                            output_buf[output_pos++] = 0xE9;
                                            output_buf[output_pos++] = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].volume;

                                            cur_volume = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].volume;
                                        }

                                        if (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].pan != cur_pan)
                                        {
                                            output_buf[output_pos++] = 0xEA;
                                            output_buf[output_pos++] = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].pan;

                                            cur_pan = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].pan;
                                        }

                                        unsigned char note_number = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].note_number - 0xC;
                                        if (note_number > 0x53)
                                            note_number = 0x53;

                                        unsigned long note_length = (song_segments[segment].song_segment_tracks[track_number].song_note_list[y].end_time - song_segments[segment].song_segment_tracks[track_number].song_note_list[y].start_time);

                                        int orig_length = note_length;
                                        if (note_length > 0xD3FF)
                                            note_length = 0xD3FF;

                                        output_buf[output_pos++] = note_number + 0x80;
                                        output_buf[output_pos++] = song_segments[segment].song_segment_tracks[track_number].song_note_list[y].velocity;
                                        
                                        if (note_length < 0xC0)
                                        {
                                            output_buf[output_pos++] = note_length;
                                            note_length = 0;
                                        }
                                        else
                                        {
                                            note_length = note_length - 0xC0;

                                            unsigned long mask_low_extra = note_length >> 8;

                                            if (mask_low_extra > 0x3F)
                                                mask_low_extra = 0x3F;

                                            output_buf[output_pos++] = 0xC0 | mask_low_extra;

                                            note_length = note_length - (mask_low_extra << 8);

                                            unsigned long extra_byte = 0;
                                            if (note_length > 0)
                                            {
                                                if (note_length > 0xFF)
                                                    extra_byte = 0xFF;
                                                else
                                                    extra_byte = note_length;
                                            }

                                            output_buf[output_pos++] = extra_byte;

                                            note_length = note_length - extra_byte;
                                        }
                                    }
                                }
                            }
                        }

                        if (time < max_time)
                        {
                            write_delay((max_time - time), output_buf, output_pos);
                            time = max_time;
                        }

                        // End
                        output_buf[output_pos++] = 0x00;

                        if ((output_pos % 4) != 0)
                        {
                            int pad = 4 - (output_pos % 4);
                            for (int p = 0; p < pad; p++)
                                output_buf[output_pos++] = 0x00;
                        }
                    }

                    is_after_loop_sub_segment = false;

                }
                else
                {
                    is_after_loop_sub_segment = true;
                }
            }
        }
    }

    write_u32(output_buf, final_size_pos, output_pos);

    FILE* fp = fopen(output, "wb");
    if (fp == NULL)
    {
        delete [] output_buf;

        printf("[!] Error outputting file\n");
        return false;
    }

    fwrite(output_buf, 1, output_pos, fp);

    delete [] output_buf;
    fclose(fp);

    return true;
}



bool convert_midi_to_bgm(const char* input[4], const char* output, bool loop, unsigned long& loop_point, unsigned long name)
{
    song_segment_info song_segments[4];
    for (int segment_number = 0; segment_number < 4; segment_number++)
    {
        if (!strlen(input[segment_number]))
            continue;

        std::vector<time_value> tempo_positions;
        std::vector<song_midi_note_info> channels[0x10];
        int num_channels = 0;
        std::vector<int> instruments;
        int lowest_time = 0x7FFFFFFF;
        int highest_time = 0;

        unsigned short division = 0x30;
        if (!midi_to_song_list(input[segment_number], tempo_positions, channels, num_channels, instruments, lowest_time, highest_time, loop, loop_point, division))
            return false;

        std::vector<int> sub_segments;

        for (int track_num = 0; track_num < 0x10; track_num++)
        {
            for (int x = 0; x < channels[track_num].size(); x++)
            {
                if (std::find(sub_segments.begin(), sub_segments.end(), channels[track_num][x].segment_number) == sub_segments.end())
                {
                    sub_segments.push_back(channels[track_num][x].segment_number);
                }
            }
        }

        float note_time_divisor = division / 0x30;

        unsigned long loop_point_real = (float)loop_point / note_time_divisor;

        // Renumber segments
        for (int track_num = 0; track_num < 0x10; track_num++)
        {
            for (int x = 0; x < channels[track_num].size(); x++)
            {
                int old_segment = channels[track_num][x].segment_number;

                bool not_found_segment = false;
                for (int s = 0; s < sub_segments.size(); s++)
                {
                    if (old_segment == sub_segments[s])
                    {
                        int new_segment_number = s;
                        if (loop && (channels[track_num][x].start_time >= loop_point_real))
                            new_segment_number++;

                        channels[track_num][x].segment_number = new_segment_number;
                        not_found_segment = true;
                        break;
                    }
                }

                if (!not_found_segment)
                {
                    // Should never happen
                    channels[track_num][x].segment_number = 0;
                }
            }
        }

        sub_segments.clear();

        for (int track_num = 0; track_num < 0x10; track_num++)
        {
            for (int x = 0; x < channels[track_num].size(); x++)
            {
                if (std::find(sub_segments.begin(), sub_segments.end(), channels[track_num][x].segment_number) == sub_segments.end())
                {
                    sub_segments.push_back(channels[track_num][x].segment_number);
                }
            }
        }


        std::vector<int> sub_segment_starts;
        std::vector<int> sub_segment_ends;
        sub_segment_starts.resize(sub_segments.size());
        sub_segment_ends.resize(sub_segments.size());

        for (int sub_segment = 0; sub_segment < sub_segments.size(); sub_segment++)
        {
            int start_segment = 0x7FFFFFFF;
            int end_segment = 0;
            for (int track_num = 0; track_num < 0x10; track_num++)
            {
                for (int x = 0; x < channels[track_num].size(); x++)
                {
                    if (sub_segment == channels[track_num][x].segment_number)
                    {
                        if (channels[track_num][x].start_time < start_segment)
                            start_segment = channels[track_num][x].start_time;

                        if (channels[track_num][x].end_time > end_segment)
                            end_segment = channels[track_num][x].end_time;
                    }
                }
            }

            if (sub_segment == 0)
            {
                sub_segment_starts[sub_segment] = 0;
            }
            else
            {
                sub_segment_starts[sub_segment] = start_segment;
            }
            sub_segment_ends[sub_segment] = end_segment;
        }

        for (int x = 0; x < sub_segments.size(); x++)
        {
            for (int y = 0; y < sub_segments.size(); y++)
            {
                if (x != y)
                {
                    if (does_overlap(sub_segment_starts[x], sub_segment_ends[x], sub_segment_starts[y], sub_segment_ends[y]))
                    {
                        char response[5];
                        printf("[?] Warning Overlap of sub-segments.\n");
                        printf("Do you want to continue? [y/N] ");
                        scanf("%4s", response);
                        if (response[0] != 'Y' && response[0] != 'y')
                            return false;
                        else
                        {
                            x = sub_segments.size();
                            y = sub_segments.size();
                            break;
                        }
                    }
                }
            }
        }
        
        for (int track_num = 0; track_num < 0x10; track_num++)
        {
            for (int x = 0; x < channels[track_num].size(); x++)
            {
                song_segments[segment_number].song_segment_tracks[track_num].song_note_list.push_back(channels[track_num][x]);
            }
        }

        song_segments[segment_number].tempo_positions = tempo_positions;
    }
    std::vector<song_drums> drums_list;
    std::vector<song_instrument> instruments_list;

    return convert_to_bgm(output, drums_list, instruments_list, song_segments, name, loop);
}



void print_usage(char* prog_name)
{
    printf("\n");
    printf("midi2bgm - Converts a MIDI file to a Paper Mario BGM file\n");
    printf("\n");
    printf("Usage: %s [options] -i <infile> -o <outfile>\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("    -i, --infile    <filename>  Input MIDI file\n");
    printf("    -o, --outfile   <filename>  Output BGM file\n");
    printf("\n");
    printf("    -l, --loop                  Generate a looping BGM file\n");
    printf("    -p, --point     <value>     MIDI loop point value (default: 0)\n");
    printf("    -n, --number    <index>     BGM index number\n");
    printf("        --name      <name>      BGM index name\n");
    printf("    -d, --drum      <track>     Track to mark as percussion (default: none)\n");
    printf("\n");
}



int main(int argc, char* argv[])
{
    int opt_idx;
    int c;
    const char* input[4];
    const char* outfile;
    bool loop = true;
    unsigned long loop_point = 0;
    unsigned long name = 0x33313920;

    bool has_i = false;
    bool has_o = false;
    bool has_error = false;

    static struct option options[] =
    {
        {"infile",  required_argument, 0, 'i'},
        {"outfile", required_argument, 0, 'o'},
        {"loop",    no_argument,       0, 'l'},
        {"point",   required_argument, 0, 'p'},
        {"number",  required_argument, 0, 'n'},
        {"drum",    required_argument, 0, 'd'},
        {"help",    no_argument,       0, 'h'}
    };

    while ((c = getopt_long(argc, argv, "d:hi:ln:o:p:", options, &opt_idx)) != -1)
    {
        if (c == 1)
            break;

        switch(c)
        {
            case 'd':
                drum_track = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'i':
                input[0] = optarg;
                input[1] = "";
                input[2] = "";
                input[3] = "";
                has_i = true;
                break;
            case 'l':
                loop = true;
                break;
            case 'n':
                name = char_array_to_long((unsigned char*)optarg);
                break;
            case 'o':
                outfile = optarg;
                has_o = true;
                break;
            case 'p':
                loop_point = atoi(optarg);
                break;
            case '?':
                has_error = true;
            default:
                break;
        }
    }

    if (optind < argc) {
        printf ("Unrecognized options: ");
        while (optind < argc)
            printf ("%s ", argv[optind++]);
        printf ("\n");
    }

    // Skip printing usage if error is encountered
    if (has_error)
        return 1;

    if (!has_i || !has_o)
    {
        print_usage(argv[0]);
        return 1;
    }


    // Attempt conversion
    bool success = convert_midi_to_bgm(input, outfile, loop, loop_point, name);

    if (success)
        printf("File successfully converted to %s!\n", outfile);
    else
        printf("Failed to convert file!\n");

    return 0;
}
