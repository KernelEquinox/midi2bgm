#include <vector>
#include <map>

#define MAXCHANNELS 1024

typedef    unsigned char              u8;
typedef    unsigned short int        u16;
typedef    unsigned long             u32;
typedef    unsigned long long int    u64;

typedef    signed char                s8;
typedef    signed short int          s16;
typedef    signed long               s32;
typedef    signed long long int      s64;

typedef    float                     f32;

struct time_value
{
	unsigned long time;
	unsigned long value;

	time_value(unsigned long time, unsigned long value)
	{
		this->time = time;
		this->value = value;
	}
};

struct time_value_sort
{
    inline bool operator() (const time_value& struct1, const time_value& struct2)
    {
        return (struct1.time < struct2.time);
    }
};

#define INSTRUMENTTYPE 3
#define EFFECTTYPE 2
#define VOLUMETYPE 0
#define PANTYPE 1
#define PITCHBENDTYPE 4
#define VIBRATOTYPE 5

struct time_value_type : public time_value
{
	int type;
	time_value_type(unsigned long time, unsigned long value, int type) : time_value(time, value)
	{
		this->type = type;
	}
};

struct time_value_type_sort
{
    inline bool operator() (const time_value_type& struct1, const time_value_type& struct2)
    {
		if (struct1.time == struct2.time)
			return (struct1.type < struct2.type);
		else
			return (struct1.time < struct2.time);
    }
};

struct song_time_value
{
	unsigned long start_time;
	unsigned long end_time;

	unsigned char value;
};

struct song_note_info
{
	unsigned long start_time;
	unsigned long end_time;
	int note_number;
	unsigned char velocity;

	unsigned long instrument;

	int pan;
	int volume;
	unsigned char pitch_bend;
	unsigned char vibrato;

	unsigned long tempo;

	unsigned char effect;

	int orig_track;
	int orig_note_id;

	bool ignore_note;

	int segment_number;

	song_note_info()
	{
		volume = 0x7F;
		pitch_bend = 0x40;
		pan = 0x40;

		orig_track = 0;
		orig_note_id = 0;

		effect = 0;

		ignore_note = false;

		segment_number = 0x00;

		vibrato = 0x00;
	}
};

struct song_segment_track_info
{
	std::vector<song_note_info> song_note_list;
};

struct song_sub_segment_info
{
	int start_time;
	int end_time;
	unsigned short track_flags;
};

struct song_segment_info
{
	std::vector<time_value> tempo_positions;
	song_segment_track_info song_segment_tracks[0x10];
	std::map<int, song_sub_segment_info> song_sub_segment;
};

struct song_drums
{
	unsigned char flags;
	unsigned char instrument;
	unsigned char unknown2;
	unsigned char unknown3;
	unsigned char volume;
	unsigned char pan;
	unsigned char effect;
	unsigned char unknown7;
	unsigned char unknown8;
	unsigned char unknown9;
	unsigned char unknownA;
	unsigned char unknownB;
};

struct song_instrument
{
	unsigned char flags;
	unsigned char instrument;
	unsigned char volume;
	unsigned char pan;
	unsigned char effect;
	unsigned char unknown5;
	unsigned char unknown6;
	unsigned char unknown7;
};

struct song_midi_note_info : song_note_info
{
	unsigned char orig_controller;
	song_midi_note_info()
	{
		orig_controller = 0;	
		end_time = 0xFFFFFFFF;
	}
};

struct song_sort_by_start
{
    inline bool operator() (const song_note_info& struct1, const song_note_info& struct2)
    {
        return (struct1.start_time < struct2.start_time);
    }
};

struct song_sort_by_end
{
    inline bool operator() (const song_note_info& struct1, const song_note_info& struct2)
    {
        return (struct1.end_time < struct2.end_time);
    }
};

struct track_event // referenced in order, but first = 2710, doors refer to 27XX + preset to it implicitly, 0x44 per
{
	bool obsolete_event;
	unsigned long delta;
	unsigned long duration;
	unsigned long time;
	u8 type;
	u8* contents;  // remember delete all track events mem later
	int content_size;

	track_event()
	{
		type = 0x00;

		contents = NULL;
		content_size = 0;

		delta = 0;
		time = 0;
		obsolete_event = false;
		duration = 0;
	}
};
