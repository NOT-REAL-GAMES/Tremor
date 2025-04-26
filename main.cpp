

// Copyright 2025 NOT REAL GAMES
//
// Permission is hereby granted, free of charge, 
// to any person obtaining a copy of this software 
// and associated documentation files(the “Software”), 
// to deal in the Software without restriction, 
// including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or 
// sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions:
//
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-
// INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN 
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
// OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
// IN THE SOFTWARE.

// This file is part of Tremor: a game engine built on a 
// rewrite of the Quake Engine as a foundation. The original 
// Quake Engine is licensed under the GPLv2 license. 

// The Tremor project is not affiliated with or endorsed by id Software.
// idTech 2's dependencies on Quake will be removed from the Tremor project. 

#include <vulkan/vulkan_core.h>
#include <windows.h>
#include <mmsystem.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>

#include "atomics.h"

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>

#include <iostream>
#include <cstring>

#define LERP_BANDAID

#define SDL_MAIN_HANDLED
#define NO_SDL_VULKAN_TYPEDEFS

#define QS_STRINGIFY_(x) #x
#define QS_STRINGIFY(x)	 QS_STRINGIFY_ (x)

#define TREMOR_VERSION 0.0
#define TREMOR_VER_PATCH 1
#define TREMOR_VER_SUFFIX "-dev"

#define COMMA ,
#define NO_COMMA

#define MAX_MAPSTRING 2048
#define MAX_DEMOS	  8
#define MAX_DEMONAME  16

#define MAX_NUM_ARGVS 50
#define CMDLINE_LENGTH 256

#define MAX_ARGS 80
#define MAX_PARMS 8 

#define MAX_AREA_DEPTH	   9
#define AREA_NODES		   (2 << MAX_AREA_DEPTH)

#define MIN_EDICTS 256
#define MAX_EDICTS 32000

#define MAX_LIGHTSTYLES	  64
#define MAX_MODELS		  8192 // johnfitz -- was 256
#define MAX_SOUNDS		  2048 // johnfitz -- was 256
#define MAX_PARTICLETYPES 2048


#define TREMOR_VER_STRING	  QS_STRINGIFY (TREMOR_VERSION) "." QS_STRINGIFY (TREMOR_VER_PATCH) TREMOR_VER_SUFFIX


#define ANNOTATE_HAPPENS_BEFORE(x) \
	do                             \
	{                              \
	} while (false)
#define ANNOTATE_HAPPENS_AFTER(x) \
	do                            \
	{                             \
	} while (false)
#define ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(x) \
	do                                        \
	{                                         \
	} while (false)

#define Q_ALIGN(a) __declspec (align (a))

#define ENGINE_NAME_AND_VER \
	"Tremor"               \
	" " TREMOR_VER_STRING

#define THREAD_STACK_RESERVATION (128ll * 1024ll)
#define MAX_STACK_ALLOC_SIZE	 (512ll * 1024ll)

#define TASKS_MAX_WORKERS	32

#define NUM_INDEX_BITS		 8
#define MAX_PENDING_TASKS	 (1u << NUM_INDEX_BITS)
#define MAX_EXECUTABLE_TASKS 256
#define MAX_DEPENDENT_TASKS	 16
#define MAX_PAYLOAD_SIZE	 128
#define WORKER_HUNK_SIZE	 (1 * 1024 * 1024)
#define WAIT_SPIN_COUNT		 100

#define THREAD_LOCAL  __declspec (thread)

#define VID_CBITS  6
#define VID_GRADES (1 << VID_CBITS)

#define SIGNONS 4

#define NUM_CSHIFTS		4

#define MAXPRINTMSG 4096
#define MAX_OSPATH 1024

#define MAX_ALIAS_NAME 32

#define MAX_MSGLEN	 64000
#define MAX_DATAGRAM 64000

#define DATAGRAM_MTU 1400

#define MAXCMDLINE	256

#define NET_MAXMESSAGE 64000 
#define NET_LOOPBACKBUFFERS	   5
#define NET_LOOPBACKHEADERSIZE 4

#define NET_NAMELEN 64

#define NUM_PING_TIMES		  16
#define NUM_BASIC_SPAWN_PARMS 16
#define NUM_TOTAL_SPAWN_PARMS 64

#define MAX_CHANNELS		 1024
#define MAX_DYNAMIC_CHANNELS 128

#define MAX_SCOREBOARD	   16
#define MAX_SCOREBOARDNAME 32

#define DEF_SAVEGLOBAL (1 << 15)

#define QCEXTFUNC(n, t) func_t n;

#define NUM_AMBIENTS 4 // automatic ambient sounds

#define MAX_QPATH 64

#define VA_NUM_BUFFS 4
#if (MAX_OSPATH >= 1024)
#define VA_BUFFERLEN MAX_OSPATH
#else
#define VA_BUFFERLEN 1024
#endif

#define countof(x) (sizeof (x) / sizeof ((x)[0]))

#define DotProduct(x, y)				((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])
#define DotProduct2(x, y)				((x)[0] * (y)[0] + (x)[1] * (y)[1])
#define DoublePrecisionDotProduct(x, y) ((double)(x)[0] * (y)[0] + (double)(x)[1] * (y)[1] + (double)(x)[2] * (y)[2])
#define VectorSubtract2(a, b, c)  \
	do                            \
	{                             \
		(c)[0] = (a)[0] - (b)[0]; \
		(c)[1] = (a)[1] - (b)[1]; \
	} while (false)
#define VectorSubtract(a, b, c)   \
	do                            \
	{                             \
		(c)[0] = (a)[0] - (b)[0]; \
		(c)[1] = (a)[1] - (b)[1]; \
		(c)[2] = (a)[2] - (b)[2]; \
	} while (false)
#define VectorAdd2(a, b, c)       \
	do                            \
	{                             \
		(c)[0] = (a)[0] + (b)[0]; \
		(c)[1] = (a)[1] + (b)[1]; \
	} while (false)
#define VectorAdd(a, b, c)        \
	do                            \
	{                             \
		(c)[0] = (a)[0] + (b)[0]; \
		(c)[1] = (a)[1] + (b)[1]; \
		(c)[2] = (a)[2] + (b)[2]; \
	} while (false)
#define VectorCopy(a, b) \
	do                   \
	{                    \
		(b)[0] = (a)[0]; \
		(b)[1] = (a)[1]; \
		(b)[2] = (a)[2]; \
	} while (0)
#define Vector4Copy(a, b) \
	do                    \
	{                     \
		(b)[0] = (a)[0];  \
		(b)[1] = (a)[1];  \
		(b)[2] = (a)[2];  \
		(b)[3] = (a)[3];  \
	} while (0)

#define sound_nominal_clip_dist 1000.0

#define sfunc net_drivers[sock->driver]
#define dfunc net_drivers[net_driverlevel]

static char	   cvar_null_string[] = "";
static char	 argvdummy[] = " ";

THREAD_LOCAL char com_token[1024];
typedef enum
{
	CPE_NOTRUNC,	// return parse error in case of overflow
	CPE_ALLOWTRUNC, // truncate com_token in case of overflow
} cpe_mode;

typedef struct
{
	int rate;
	int width;
	int channels;
	int loopstart;
	int samples;
	int dataofs; /* chunk starts this many bytes from file start	*/
} wavinfo_t;

static char logfilename[MAX_OSPATH]; // current logfile name
static int	log_fd = -1;			 // log file descriptor

typedef uint64_t task_handle_t;
typedef void (*task_func_t) (void*);
typedef void (*task_indexed_func_t) (int, void*);

typedef enum
{
	src_client,	 // came in over a net connection as a clc_stringcmd. host_client will be valid during this state.
	src_command, // from the command buffer
	src_server	 // from a svc_stufftext
} cmd_source_t;

inline cvarflags_t& operator |= (cvarflags_t& lhs, cvarflags_t rhs) {
	lhs = static_cast<cvarflags_t>(static_cast<int>(lhs) | static_cast<int>(rhs));
	return lhs;
}

static void* Clamp(void* number, void* min, void* max)
{
	int* num = ((int*)number);
	if (((int*)number) < ((int*)min))
		num = ((int*)min);
	else if (((int*)number) > ((int*)max))
		num = ((int*)max);
	return num;
}

static void* Min(void* a, void* b)
{
	return (*(int*)a < *(int*)b) ? a : b;
}

static void* Max(void* a, void* b)
{
	return (*(int*)a > *(int*)b) ? a : b;
}

typedef unsigned int func_t;
typedef int			 string_t;

typedef struct
{
	unsigned short type; // if DEF_SAVEGLOBAL bit is set
	// the variable needs to be saved in savegames
	unsigned short ofs;
	int			   s_name;
} ddef_t;

typedef struct sizebuf_s
{
	bool allowoverflow; // if false, do a Sys_Error
	bool overflowed;	// set to true if the buffer size failed
	byte* data;
	int		 maxsize;
	int		 cursize;
} sizebuf_t;

typedef enum
{
	TASK_TYPE_NONE,
	TASK_TYPE_SCALAR,
	TASK_TYPE_INDEXED,
} task_type_t;

typedef struct
{
	task_type_t		task_type;
	int				num_dependents;
	int				indexed_limit;
	atomic_uint32_t remaining_workers;
	atomic_uint32_t remaining_dependencies;
	uint64_t		epoch;
	void* func;
	SDL_mutex* epoch_mutex;
	SDL_cond* epoch_condition;
	uint8_t			payload[MAX_PAYLOAD_SIZE];
	task_handle_t	dependent_task_handles[MAX_DEPENDENT_TASKS];
} task_t;

typedef struct
{
	atomic_uint32_t head;
	uint32_t		head_padding[15]; // Pad to 64 byte cache line size
	atomic_uint32_t tail;
	uint32_t		tail_padding[15];
	uint32_t		capacity_mask;
	SDL_sem* push_semaphore;
	SDL_sem* pop_semaphore;
	atomic_uint32_t task_indices[1];
} task_queue_t;

typedef struct
{
	atomic_uint32_t index;
	uint32_t		limit;
} task_counter_t;

typedef struct {} globalvars_t;

typedef struct
{
	int version;
	int crc; // check of header file

	int ofs_statements;
	int numstatements; // statement 0 is an error

	int ofs_globaldefs;
	int numglobaldefs;

	int ofs_fielddefs;
	int numfielddefs;

	int ofs_functions;
	int numfunctions; // function 0 is an empty

	int ofs_strings;
	int numstrings; // first string is a null string

	int ofs_globals;
	int numglobals;

	int entityfields;
} dprograms_t;
typedef struct
{
	int first_statement; // negative numbers are builtins
	int parm_start;
	int locals; // total ints of parms + locals

	int profile; // runtime

	int s_name;
	int s_file; // source file defined in

	int	 numparms;
	byte parm_size[MAX_PARMS];
} dfunction_t;

typedef void (*builtin_t) (void);

typedef struct hash_map_s
{
	uint32_t num_entries;
	uint32_t hash_size;
	uint32_t key_value_storage_size;
	uint32_t key_size;
	uint32_t value_size;
	uint32_t(*hasher) (const void* const);
	bool (*comp) (const void* const, const void* const);
	uint32_t* hash_to_index;
	uint32_t* index_chain;
	void* keys;
	void* values;
} hash_map_t;

typedef struct statement_s
{
	unsigned short op;
	short		   a, b, c;
} dstatement_t;

typedef struct hash_map_s hash_map_t;

typedef struct
{
	int			 s;
	dfunction_t* f;
} prstack_t;

typedef struct areanode_s
{
	int				   axis; // -1 = leaf node
	float			   dist;
	struct areanode_s* children[2];
	link_t			   trigger_edicts;
	link_t			   solid_edicts;
} areanode_t;

// the free-list of edicts, as a FIFO made of a circular buffer.
typedef struct freelist_s
{
	size_t	 size;		 // current nb of edicts
	size_t	 head_index; // index of the first valid element (head of FIFO)
	edict_t* circular_buffer[MAX_EDICTS];
} freelist_t;

typedef enum
{
	key_game,
	key_console,
	key_message,
	key_menu
} keydest_t;

struct qcvm_s
{
	dprograms_t* progs;
	dfunction_t* functions;
	hash_map_t* function_map;
	dstatement_t* statements;
	float* globals;	 /* same as pr_global_struct */
	ddef_t* fielddefs; // yay reflection.
	hash_map_t* fielddefs_map;

	int edict_size; /* in bytes */

	builtin_t builtins[1024];
	int		  numbuiltins;

	int argc;

	bool	 trace;
	dfunction_t* xfunction;
	int			 xstatement;

	unsigned short progscrc;  // crc16 of the entire file
	unsigned int   progshash; // folded file md4
	unsigned int   progssize; // file size (bytes)

	struct pr_extglobals_s {} extglobals;
	struct pr_extfuncs_s
	{
		/*all vms*/
#define QCEXTFUNCS_COMMON                                                                                      \
	QCEXTFUNC (GameCommand, "void(string cmdtext)") /*obsoleted by m_consolecommand, included for dp compat.*/ \
/*csqc+ssqc*/
#define QCEXTFUNCS_GAME            \
	QCEXTFUNC (EndFrame, "void()") \
/*ssqc*/
#define QCEXTFUNCS_SV                                     \
	QCEXTFUNC (SV_ParseClientCommand, "void(string cmd)") \
	QCEXTFUNC (SV_RunClientCommand, "void()")             \
/*csqc*/
#define QCEXTFUNCS_CS                                                                                                                     \
	QCEXTFUNC (CSQC_Init, "void(float apilevel, string enginename, float engineversion)")                                                 \
	QCEXTFUNC (CSQC_Shutdown, "void()")                                                                                                   \
	QCEXTFUNC (CSQC_DrawHud, "void(vector virtsize, float showscores)")	   /*simple: for the simple(+limited) hud-only csqc interface.*/  \
	QCEXTFUNC (CSQC_DrawScores, "void(vector virtsize, float showscores)") /*simple: (optional) for the simple hud-only csqc interface.*/ \
	QCEXTFUNC (CSQC_InputEvent, "float(float evtype, float scanx, float chary, float devid)")                                             \
	QCEXTFUNC (CSQC_ConsoleCommand, "float(string cmdstr)")                                                                               \
	QCEXTFUNC (CSQC_Parse_Event, "void()")                                                                                                \
	QCEXTFUNC (CSQC_Parse_Damage, "float(float save, float take, vector dir)")                                                            \
	QCEXTFUNC (CSQC_Parse_CenterPrint, "float(string msg)")                                                                               \
	QCEXTFUNC (CSQC_Parse_Print, "void(string printmsg, float printlvl)")

#define QCEXTFUNC(n, t) func_t n;
		QCEXTFUNCS_COMMON
			QCEXTFUNCS_GAME
			QCEXTFUNCS_SV
			QCEXTFUNCS_CS
#undef QCEXTFUNC
	} extfuncs;
	struct pr_extfields_s {} extfields;

	// was static inside pr_edict
	char* strings;
	int			 stringssize;
	const char** knownstrings;
	bool* knownstringsowned;
	int			 maxknownstrings;
	int			 numknownstrings;
	int			 progsstrings; // allocated by PR_MergeEngineFieldDefs (), not tied to edicts
	int			 freeknownstrings;
	ddef_t* globaldefs;
	hash_map_t* globaldefs_map;

	unsigned char* knownzone;
	size_t		   knownzonesize;

	// originally defined in pr_exec, but moved into the switchable qcvm struct
#define MAX_STACK_DEPTH 1024 /*was 64*/ /* was 32 */
	prstack_t stack[MAX_STACK_DEPTH];
	int		  depth;

#define LOCALSTACK_SIZE 16384 /* was 2048*/
	int localstack[LOCALSTACK_SIZE];
	int localstack_used;

	// originally part of the sv_state_t struct
	// FIXME: put worldmodel in here too.
	double			 time;
	int				 num_edicts;
	int				 reserved_edicts;
	int				 max_edicts;
	edict_t* edicts; // can NOT be array indexed, because edict_t is variable sized, but can be used to reference the world ent
	freelist_t		 free_list;
	struct qmodel_s* worldmodel;
	struct qmodel_s* (*GetModel) (int modelindex); // returns the model for the given index, or null.

	// originally from world.c
	areanode_t areanodes[AREA_NODES];
	int		   numareanodes;
};

typedef struct qcvm_s qcvm_t;

static int					 num_workers = 0;
static SDL_Thread** worker_threads;
static task_t				 tasks[MAX_PENDING_TASKS];
static task_queue_t* free_task_queue;
static task_queue_t* executable_task_queue;
static task_counter_t* indexed_task_counters;
static uint8_t				 steal_worker_indices[TASKS_MAX_WORKERS * 2];
static THREAD_LOCAL bool is_worker = false;
static THREAD_LOCAL int		 tl_worker_index;


typedef unsigned char byte;
typedef int64_t qfileofs_t;

typedef struct cmdalias_s
{
	struct cmdalias_s* next;
	char			   name[MAX_ALIAS_NAME];
	char* value;
} cmdalias_t;

typedef enum
{
	ca_dedicated,	 // a dedicated server with no ability to start a client
	ca_disconnected, // full screen console with no connection
	ca_connected	 // valid netcon, talking to a server
} cactive_t;

typedef enum
{
	ev_bad = -1,
	ev_void = 0,
	ev_string,
	ev_float,
	ev_vector,
	ev_entity,
	ev_field,
	ev_function,
	ev_pointer,

	ev_ext_integer,
	ev_ext_uint32,
	ev_ext_sint64,
	ev_ext_uint64,
	ev_ext_double,
} etype_t;

typedef struct
{
	cactive_t state;

	// personalization data sent to server
	char spawnparms[MAX_MAPSTRING]; // to restart a level

	// demo loop control
	int	 demonum;						 // -1 = don't play demos
	char demos[MAX_DEMOS][MAX_DEMONAME]; // when not playing

	// demo recording info must be here, because record is started before
	// entering a map (and clearing client_state_t)
	bool demorecording;
	bool demoplayback;

	// did the user pause demo playback? (separate from cl.paused because we don't
	// want a svc_setpause inside the demo to actually pause demo playback).
	bool demopaused;
	bool demoseeking;
	float	 seektime;
	float	 demospeed;

	// demo file position where the current level starts (after signon packets)
	qfileofs_t demo_prespawn_end;

	bool timedemo;
	int		 forcetrack; // -1 = use normal cd track
	FILE* demofile;
	int		 td_lastframe;	// to meter out one message a frame
	int		 td_startframe; // host_framecount at start
	float	 td_starttime;	// realtime at second frame of timedemo

	// connection information
	int				  signon; // 0 to SIGNONS
	struct qsocket_s* netcon;
	sizebuf_t		  message; // writing buffer to send to server

	char userinfo[8192];
} client_static_t;

typedef enum
{
	dpi_unaware = 0,
	dpi_system_aware = 1,
	dpi_monitor_aware = 2
} dpi_awareness;
typedef BOOL(WINAPI* SetProcessDPIAwareFunc) ();
typedef HRESULT(WINAPI* SetProcessDPIAwarenessFunc) (dpi_awareness value);

static HANDLE hinput, houtput;
static char	  cwd[1024];
static double counter_freq;

size_t max_thread_stack_alloc_size = 0;

typedef struct
{
	int	 length;
	int	 loopstart;
	int	 speed;
	int	 width;
	int	 stereo;
	byte data[1]; /* variable sized	*/
} sfxcache_t;

typedef struct sfx_s
{
	char		name[64];
	sfxcache_t* cache;
} sfx_t;

typedef struct
{
	sfx_t* sfx;		 /* sfx number					*/
	int	   leftvol;	 /* 0-255 volume					*/
	int	   rightvol; /* 0-255 volume					*/
	int	   end;		 /* end time in global paintsamples		*/
	int	   pos;		 /* sample position in sfx			*/
	int	   looping;	 /* where to loop, -1 = no looping		*/
	int	   entnum;	 /* to allow overriding a specific sound		*/
	int	   entchannel;
	vec3_t origin;	   /* origin of sound effect			*/
	vec_t  dist_mult;  /* distance multiplier (attenuation/clipK)	*/
	int	   master_vol; /* 0-255 master volume				*/
} channel_t;

typedef enum
{
	ss_loading,
	ss_active
} server_state_t;

typedef struct
{
	const char* basedir;
	const char* userdir; // user's directory on UNIX platforms.
	// if user directories are enabled, basedir
	// and userdir will point to different
	// memory locations, otherwise to the same.
	int			argc;
	char** argv;
	int			errstate;
} parms_t;

typedef struct
{
	char  name[MAX_SCOREBOARDNAME];
	float entertime;
	int	  frags;
	int	  colors; // two 4 bit fields
	int	  ping;
	byte  translations[VID_GRADES * 256];

	char userinfo[8192];
} scoreboard_t;

typedef struct
{
	float  servertime;
	float  seconds; // servertime-previous->servertime
	vec3_t viewangles;

	// intended velocities
	float forwardmove;
	float sidemove;
	float upmove;

	// used by client for mouse-based movements that should accumulate over multiple client frames
	float forwardmove_accumulator;
	float sidemove_accumulator;
	float upmove_accumulator;

	unsigned int buttons;
	unsigned int impulse;

	unsigned int sequence;

	int weapon;
} usercmd_t;

typedef struct link_s
{
	struct link_s* prev, * next;
} link_t;

typedef struct entity_state_s
{
	vec3_t		   origin;
	vec3_t		   angles;
	unsigned short modelindex; // johnfitz -- was int
	unsigned short frame;	   // johnfitz -- was int
	unsigned int   effects;
	unsigned char  colormap;	   // johnfitz -- was int
	unsigned char  skin;		   // johnfitz -- was int
	unsigned char  scale;		   // spike -- *16
	unsigned char  pmovetype;	   // spike
	unsigned short traileffectnum; // spike -- for qc-defined particle trails. typically evilly used for things that are not trails.
	unsigned short emiteffectnum;  // spike -- for qc-defined particle trails. typically evilly used for things that are not trails.
	short		   velocity[3];	   // spike -- the player's velocity.
	unsigned char  eflags;
	unsigned char  tagindex;
	unsigned short tagentity;
	unsigned short pad;
	unsigned char  colormod[3]; // spike -- entity tints, *32
	unsigned char  alpha;		// johnfitz -- added
	unsigned int   solidsize;	// for csqc prediction logic.
#define ES_SOLID_NOT   0
#define ES_SOLID_BSP   31
#define ES_SOLID_HULL1 0x80201810
#define ES_SOLID_HULL2 0x80401820
#ifdef LERP_BANDAID
	unsigned short lerp;
#endif
} entity_state_t;

typedef struct {} entvars_t;

THREAD_LOCAL qfileofs_t com_filesize;

typedef Q_ALIGN(4) int64_t qcsint64_t;
typedef Q_ALIGN(4) uint64_t qcuint64_t;
typedef Q_ALIGN(4) double qcdouble_t;

typedef struct edict_s
{
	link_t area; /* linked to a division node or leaf */

	unsigned int num_leafs;
	int			 leafnums[128];

	entity_state_t baseline;
	unsigned char  alpha;		 /* johnfitz -- hack to support alpha since it's not part of entvars_t */
	bool	   sendinterval; /* johnfitz -- send time until nextthink to client for better lerp timing */
	float		   oldframe;
	float		   oldthinktime;
	vec3_t		   predthinkpos; /* expected edict origin once its nextthink arrives (sv_smoothplatformlerps) */
	float		   lastthink;	 /* time when predthinkpos was updated, or 0 if not valid (sv_smoothplatformlerps) */

	float	 freetime; /* sv.time when the object was freed */
	bool free;

	entvars_t v; /* C exported fields from progs */

	/* other fields from progs come immediately after */
} edict_t;

typedef enum {
	MAX_CL_STATS = 256,
};

typedef struct client_s
{
	bool active;   // false = client is free
	bool spawned;  // false = don't send datagrams (set when client acked the first entities)
	bool dropasap; // has been told to go to another level
	enum
	{
		PRESPAWN_DONE,
		PRESPAWN_FLUSH = 1,
		//		PRESPAWN_SERVERINFO,
		PRESPAWN_MODELS,
		PRESPAWN_SOUNDS,
		PRESPAWN_PARTICLES,
		PRESPAWN_BASELINES,
		PRESPAWN_STATICS,
		PRESPAWN_AMBIENTS,
		PRESPAWN_SIGNONMSG,
	} sendsignon; // only valid before spawned
	int			 signonidx;
	unsigned int signon_sounds; //
	unsigned int signon_models; //

	double last_message; // reliable messages must be sent
	// periodically

	struct qsocket_s* netconnection; // communications handle

	usercmd_t cmd;	   // movement
	vec3_t	  wishdir; // intended motion calced from cmd

	sizebuf_t message; // can be added to at any time,
	// copied and clear once per frame
	byte	  msgbuf[MAX_MSGLEN];
	edict_t* edict;	// EDICT_NUM(clientnum+1)
	char	  name[32]; // for printing to other people
	int		  colors;

	float ping_times[NUM_PING_TIMES];
	int	  num_pings; // ping_times[num_pings%NUM_PING_TIMES]

	// spawn parms are carried from level to level
	float spawn_parms[NUM_TOTAL_SPAWN_PARMS];

	// client known data for deltas
	int old_frags;

	typedef struct
	{
		char name[MAX_QPATH];
		int	 filepos, filelen;
	} packfile_t;

	typedef struct pack_s
	{
		char		filename[MAX_OSPATH];
		int			handle;
		int			numfiles;
		packfile_t* files;
	} pack_t;

	typedef struct searchpath_s
	{
		unsigned int		 path_id; // identifier assigned to the game directory
		// Note that <install_dir>/game1 and
		// <userdir>/game1 have the same id.
		char				 filename[MAX_OSPATH];
		pack_t* pack;			 // only one of filename / pack will be used
		char				 dir[MAX_QPATH]; // directory name: "id1", "rogue", etc.
		struct searchpath_s* next;
	} searchpath_t;

	sizebuf_t datagram;
	byte	  datagram_buf[MAX_DATAGRAM];

	unsigned int limit_entities;   // vanilla is 600
	unsigned int limit_unreliable; // max allowed size for unreliables
	unsigned int limit_reliable;   // max (total) size of a reliable message.
	unsigned int limit_models;	   //
	unsigned int limit_sounds;	   //
	bool	 pextknown;
	unsigned int protocol_pext1;
	unsigned int protocol_pext2;
	unsigned int resendstatsnum[MAX_CL_STATS / 32]; // the stats which need to be resent.
	unsigned int resendstatsstr[MAX_CL_STATS / 32]; // the stats which need to be resent.
	int			 oldstats_i[MAX_CL_STATS];			// previous values of stats. if these differ from the current values, reflag resendstats.
	float		 oldstats_f[MAX_CL_STATS];			// previous values of stats. if these differ from the current values, reflag resendstats.
	char* oldstats_s[MAX_CL_STATS];
	struct entity_num_state_s
	{
		unsigned int   num; // ascending order, there can be gaps.
		entity_state_t state;
	}			 *previousentities;
	size_t		  numpreviousentities;
	size_t		  maxpreviousentities;
	unsigned int  snapshotresume;
	unsigned int* pendingentities_bits; // UF_ flags for each entity
	size_t		  numpendingentities;	// realloc if too small
#define SENDFLAG_PRESENT 0x80000000u	// tracks that we previously sent one of these ents (resulting in a remove if the ent gets remove()d).
#define SENDFLAG_REMOVE	 0x40000000u	// for packetloss to signal that we need to resend a remove.
#define SENDFLAG_USABLE	 0x00ffffffu	// SendFlags bits that the qc is actually able to use (don't get confused if the mod uses SendFlags=-1).
	struct deltaframe_s
	{ // quick overview of how this stuff actually works:
		// when the server notices a gap in the ack sequence, we walk through the dropped frames and reflag everything that was dropped.
		// if the server isn't tracking enough frames, then we just treat those as dropped;
		// small note: when an entity is new, it re-flags itself as new for the next packet too, this reduces the immediate impact of packetloss on new
		// entities. reflagged state includes stats updates, entity updates, and entity removes.
		int			 sequence; // to see if its stale
		float		 timestamp;
		unsigned int resendstatsnum[MAX_CL_STATS / 32];
		unsigned int resendstatsstr[MAX_CL_STATS / 32];
		struct
		{
			unsigned int num;
			unsigned int ebits;
			unsigned int csqcbits;
		}  *ents;
		int numents; // doesn't contain an entry for every entity, just ones that were sent this frame. no 0 bits
		int maxents;
	}		*frames;
	size_t	 numframes; // preallocated power-of-two
	int		 lastacksequence;
	int		 lastmovemessage;
	double	 lastmovetime;
	bool knowntoqc; // putclientinserver was called
} client_t;


typedef enum
{
	CVAR_NONE = 0,
	CVAR_ARCHIVE = (1U << 0),	 // if set, causes it to be saved to config
	CVAR_NOTIFY = (1U << 1),	 // changes will be broadcasted to all players (q1)
	CVAR_SERVERINFO = (1U << 2),   // added to serverinfo will be sent to clients (q1/net_dgrm.c and qwsv)
	CVAR_USERINFO = (1U << 3),	 // added to userinfo, will be sent to server (qwcl)
	CVAR_CHANGED = (1U << 4),
	CVAR_ROM = (1U << 6),
	CVAR_LOCKED = (1U << 8),	 // locked temporarily
	CVAR_REGISTERED = (1U << 10),  // the var is added to the list of variables
	CVAR_CALLBACK = (1U << 16),	 // var has a callback
	CVAR_USERDEFINED = (1U << 17),  // cvar was created by the user/mod, and needs to be saved a bit differently.
	CVAR_AUTOCVAR = (1U << 18),	 // cvar changes need to feed back to qc global changes.
	CVAR_SETA = (1U << 19)   // cvar will be saved with seta.
} cvarflags_t;

typedef void (*cvarcallback_t) (struct cvar_s*);

typedef struct
{
	int	  destcolor[3];
	float percent; // 0-256
} cshift_t;

typedef struct lightcache_s
{
	int	   surfidx; // < 0: black surface; == 0: no cache; > 0: 1+index of surface
	vec3_t pos;
	short  ds;
	short  dt;
} lightcache_t;

typedef union eval_s
{
	string_t   string;
	float	   _float;
	float	   vector[3];
	func_t	   function;
	int32_t	   _int;
	uint32_t   _uint32;
	qcsint64_t _sint64;
	qcuint64_t _uint64;
	qcdouble_t _double;
	int		   edict;
} eval_t;

typedef struct entity_s
{
	bool forcelink; // model changed

	int update_type;

	entity_state_t baseline; // to fill in defaults in updates
	entity_state_t netstate; // the latest network state

	double			 msgtime;		 // time of last update
	vec3_t			 msg_origins[2]; // last two updates (0 is newest)
	vec3_t			 origin;
	vec3_t			 msg_angles[2]; // last two updates (0 is newest)
	vec3_t			 angles;
	struct qmodel_s* model; // NULL = no model
	struct efrag_s* efrag; // linked list of efrags
	int				 frame;
	float			 syncbase; // for client-side animations
	byte* colormap;
	int				 effects;  // light, particles, etc
	int				 skinnum;  // for Alias models
	int				 visframe; // last frame this entity was
	//  found in an active leaf

	int dlightframe; // dynamic lighting
	int dlightbits;

	// FIXME: could turn these into a union
	struct mnode_s* topnode; // for bmodels, first world node
	//  that splits bmodel, or NULL if
	//  not split

	byte   eflags;		 // spike -- mostly a mirror of netstate, but handles tag inheritance (eww!)
	byte   alpha;		 // johnfitz -- alpha
	byte   lerpflags;	 // johnfitz -- lerping
	float  lerpstart;	 // johnfitz -- animation lerping
	float  lerptime;	 // johnfitz -- animation lerping
	float  lerpfinish;	 // johnfitz -- lerping -- server sent us a more accurate interval, use it instead of 0.1
	short  previouspose; // johnfitz -- animation lerping
	short  currentpose;	 // johnfitz -- animation lerping
	//	short					futurepose;		//johnfitz -- animation lerping
	float  movelerpstart;  // johnfitz -- transform lerping
	vec3_t previousorigin; // johnfitz -- transform lerping
	vec3_t currentorigin;  // johnfitz -- transform lerping
	vec3_t previousangles; // johnfitz -- transform lerping
	vec3_t currentangles;  // johnfitz -- transform lerping

	float scale; // rbQuake -- scale factor for model

#ifdef PSET_SCRIPT
	struct trailstate_s* trailstate; // spike -- managed by the particle system, so we don't loose our position and spawn the wrong number of particles, and we
	// can track beams etc
	struct trailstate_s* emitstate;	 // spike -- for effects which are not so static.
#endif
	float  traildelay; // time left until next particle trail update
	vec3_t trailorg;   // previous particle trail point

	lightcache_t lightcache; // alias light trace cache

	int	   contentscache;
	vec3_t contentscache_origin;
} entity_t;

typedef SOCKET sys_socket_t;

typedef struct qsocket_s
{
	struct qsocket_s* next;
	double			  connecttime;
	double			  lastMessageTime;
	double			  lastSendTime;

	bool isvirtual; // qsocket is emulated by the network layer (closing will not close any system sockets).
	bool disconnected;
	bool canSend;
	bool sendNext;

	int			 driver;
	int			 landriver;
	sys_socket_t socket;
	void* driverdata;

	unsigned int ackSequence;
	unsigned int sendSequence;
	unsigned int unreliableSendSequence;
	int			 sendMessageLength;
	byte		 sendMessage[NET_MAXMESSAGE];

	unsigned int receiveSequence;
	unsigned int unreliableReceiveSequence;
	int			 receiveMessageLength;
	byte		 receiveMessage[NET_MAXMESSAGE * NET_LOOPBACKBUFFERS + NET_LOOPBACKHEADERSIZE];

	struct qsockaddr {
		short qsa_family;
		unsigned char qsa_data[62];
	} addr;
	char			 trueaddress[NET_NAMELEN];	 // lazy address string
	char			 maskedaddress[NET_NAMELEN]; // addresses for this player that may be displayed publically

	bool proquake_angle_hack;  // 1 if we're trying, 2 if the server acked.
	int		 max_datagram;		   // 32000 for local, 1442 for 666, 1024 for 15. this is for reliable fragments.
	int		 pending_max_datagram; // don't change the mtu if we're resending, as that would confuse the peer.
} qsocket_t;


typedef struct
{
	int		  movemessages;		 // since connecting to this server
	// throw out the first couple, so the player
	// doesn't accidentally do something the
	// first frame
	int		  ackedmovemessages; // echo of movemessages from the server.
	usercmd_t movecmds[64];		 // ringbuffer of previous movement commands (journal for prediction)
#define MOVECMDS_MASK (countof (cl.movecmds) - 1)
	usercmd_t pendingcmd; // accumulated state from mice+joysticks.

	// information for local display
	int	  stats[MAX_CL_STATS]; // health, etc
	float statsf[MAX_CL_STATS];
	char* statss[MAX_CL_STATS];
	int	  items;			// inventory bit flags
	float item_gettime[32]; // cl.time of aquiring item, for blinking
	float faceanimtime;		// use anim frame if cl.time < this

	float v_dmg_time, v_dmg_roll, v_dmg_pitch;

	cshift_t cshift_empty;				// can be modified by V_cshift_f ()
	cshift_t cshifts[NUM_CSHIFTS];		// color shifts for damage, powerups
	cshift_t prev_cshifts[NUM_CSHIFTS]; // and content types

	// the client maintains its own idea of view angles, which are
	// sent to the server each frame.  The server sets punchangle when
	// the view is temporarliy offset, and an angle reset commands at the start
	// of each level and after teleporting.
	vec3_t mviewangles[2]; // during demo playback viewangles is lerped
	// between these
	vec3_t viewangles;

	vec3_t mvelocity[2]; // update by server, used for lean+bob
	// (0 is newest)
	vec3_t velocity;	 // lerped between mvelocity[0] and [1]

	vec3_t punchangle; // temporary offset

	// pitch drifting vars
	float	 idealpitch;
	float	 pitchvel;
	bool nodrift;
	float	 driftmove;
	double	 laststop;

	float viewheight;
	float crouch; // local amount for smoothing stepups

	bool paused; // send over by server
	bool onground;
	bool inwater;
	double	 fixangle_time; // timestamp of last svc_setangle message

	int intermission;	// don't change view angle, full screen, etc
	int completed_time; // latched at intermission start

	double mtime[2]; // the timestamp of last two messages
	double time;	 // clients view of time, should be between
	// servertime and oldservertime to generate
	// a lerp point for other data
	double oldtime;	 // previous cl.time, time-oldtime is used
	// to decay light values and smooth step ups

	float last_received_message; // (realtime) for net trouble icon

	//
	// information that is static for the entire time connected to a server
	//
	struct qmodel_s* model_precache[MAX_MODELS];
	struct sfx_s* sound_precache[MAX_SOUNDS];

	char mapname[128];
	char levelname[128]; // for display on solo scoreboard //johnfitz -- was 40.
	int	 viewentity;	 // cl_entitites[cl.viewentity] = player
	int	 maxclients;
	int	 gametype;

	// refresh related state
	struct qmodel_s* worldmodel; // cl_entitites[0].model
	struct octree_t* octree;
	struct efrag_s* free_efrags;
	int				 num_efrags;
	struct efrag_s** efrag_allocs;
	int				 num_efragallocs;
	entity_t		 viewent; // the gun model

	entity_t* entities; // spike -- moved into here
	int		  max_edicts;
	int		  num_entities;

	entity_t** static_entities; // spike -- was static
	int		   max_static_entities;
	int		   num_statics;

	int cdtrack, looptrack; // cd audio

	// frag scoreboard
	scoreboard_t* scores; // [cl.maxclients]

	unsigned protocol; // johnfitz
	unsigned protocolflags;
	unsigned protocol_pext1; // spike -- flag of fte protocol extensions
	unsigned protocol_pext2; // spike -- flag of fte protocol extensions

#ifdef PSET_SCRIPT
	qboolean protocol_particles;
	struct
	{
		const char* name;
		int			index;
	} particle_precache[MAX_PARTICLETYPES];
	struct
	{
		const char* name;
		int			index;
	} local_particle_precache[MAX_PARTICLETYPES];
#endif
	int			 ackframes[8]; // big enough to cover burst
	unsigned int ackframes_count;
	bool	 requestresend;
	bool	 sendprespawn;

	qcvm_t qcvm; // for csqc.

	float zoom;
	float zoomdir;

	char serverinfo[8192]; // \key\value infostring data.
} client_state_t;


typedef struct cvar_s
{
	const char* name;
	const char* string;
	cvarflags_t	   flags;
	float		   value;
	const char* default_string; // johnfitz -- remember defaults for reset function
	cvarcallback_t callback;
	struct cvar_s* next;
} cvar_t;

bool in_update_screen;

static parms_t t_parms;

extern keydest_t key_dest;

jmp_buf host_abortserver;
jmp_buf screen_error;

typedef struct
{
	int			   channels;
	int			   samples;			 /* mono samples in buffer			*/
	int			   submission_chunk; /* don't mix less than this #			*/
	int			   samplepos;		 /* in mono samples				*/
	int			   samplebits;
	int			   signed8; /* device opened for S8 format? (e.g. Amiga AHI) */
	int			   speed;
	unsigned char* buffer;
} dma_t;

volatile dma_t* shm = NULL;

static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nTREMOR ERROR: ";

void ErrorDialog(const char* errorMsg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tremor Error", errorMsg, NULL);
}

static cvar_t nosound = { "nosound", "0", CVAR_NONE };


class Engine {
public:

	bool isDedicated;

	class q {
	public:
		Engine* engine;
		q(Engine e) {
			engine = &e;
		}
		static char* get_va_buffer(void)
		{
			static THREAD_LOCAL char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
			static THREAD_LOCAL int	 buffer_idx = 0;
			buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
			return va_buffers[buffer_idx];
		}
		char* va(const char* format, ...)
		{
			va_list argptr;
			char* va_buf;

			va_buf = get_va_buffer();
			va_start(argptr, format);
			vsnprintf(va_buf, VA_BUFFERLEN, format, argptr);
			va_end(argptr);

			return va_buf;
		}

		size_t strlcpy(char* dst, const char* src, size_t siz)
		{
			char* d = dst;
			const char* s = src;
			size_t		n = siz;

			/* Copy as many bytes as will fit */
			if (n != 0)
			{
				while (--n != 0)
				{
					if ((*d++ = *s++) == '\0')
						break;
				}
			}

			/* Not enough room in dst, add NUL and traverse rest of src */
			if (n == 0)
			{
				if (siz != 0)
					*d = '\0'; /* NUL-terminate dst */
				while (*s++)
					;
			}

			return (s - src - 1); /* count does not include NUL */
		}

		
		static inline int toupper(int c)
		{
			return ((islower(c)) ? (c & ~('a' - 'A')) : c);
		}
		static inline int tolower(int c)
		{
			return ((isupper(c)) ? (c | ('a' - 'A')) : c);
		}
		static inline int to_ascii(int c)
		{
			return (c & 0x7f);
		}
		static inline int isprint(int c)
		{
			return (c >= 0x20 && c <= 0x7e);
		}
		static inline int isgraph(int c)
		{
			return (c > 0x20 && c <= 0x7e);
		}

		static inline int isspace(int c)
		{
			switch (c)
			{
			case ' ':
			case '\t':
			case '\n':
			case '\r':
			case '\f':
			case '\v':
				return 1;
			}
			return 0;
		}

		static inline int isblank(int c)
		{
			return (c == ' ' || c == '\t');
		}

		static inline int is_ascii(int c)
		{
			return ((c & ~0x7f) == 0);
		}

		static inline int islower(int c)
		{
			return (c >= 'a' && c <= 'z');
		}

		static inline int isupper(int c)
		{
			return (c >= 'A' && c <= 'Z');
		}

		static inline int isalpha(int c)
		{
			return (islower(c) || isupper(c));
		}

		static inline int isdigit(int c)
		{
			return (c >= '0' && c <= '9');
		}

		static inline int isxdigit(int c)
		{
			return (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
		}

		static inline int isalnum(int c)
		{
			return (isalpha(c) || isdigit(c));
		}

		int vsnprintf(char* str, size_t size, const char* format, va_list args)
		{
			int ret;

			ret = _vsnprintf_s(str, size, _TRUNCATE, format, args);

			if (ret < 0)
				ret = (int)size;
			if (size == 0) /* no buffer */
				return ret;
			if ((size_t)ret >= size)
				str[size - 1] = '\0';

			return ret;
		}
		int strcasecmp(const char* s1, const char* s2)
		{
			const char* p1 = s1;
			const char* p2 = s2;
			char		c1, c2;

			if (p1 == p2)
				return 0;

			do
			{
				c1 = tolower(*p1++);
				c2 = tolower(*p2++);
				if (c1 == '\0')
					break;
			} while (c1 == c2);

			return (int)(c1 - c2);
		}

		int strncasecmp(const char* s1, const char* s2, size_t n)
		{
			const char* p1 = s1;
			const char* p2 = s2;
			char		c1, c2;

			if (p1 == p2 || n == 0)
				return 0;

			do
			{
				c1 = tolower(*p1++);
				c2 = tolower(*p2++);
				if (c1 == '\0' || c1 != c2)
					break;
			} while (--n > 0);

			return (int)(c1 - c2);
		}

		char* strcasestr(const char* haystack, const char* needle)
		{
			const size_t len = strlen(needle);

			if (!len)
				return (char*)haystack;

			while (*haystack)
			{
				if (!strncasecmp(haystack, needle, len))
					return (char*)haystack;

				++haystack;
			}

			return NULL;
		}

		char* q_strlwr(char* str)
		{
			char* c;
			c = str;
			while (*c)
			{
				*c = tolower(*c);
				c++;
			}
			return str;
		}

		char* q_strupr(char* str)
		{
			char* c;
			c = str;
			while (*c)
			{
				*c = toupper(*c);
				c++;
			}
			return str;
		}

		char* strdup(const char* str)
		{
			size_t len = strlen(str) + 1;
			char* newstr = (char*)mem->Alloc(len);
			memcpy(newstr, str, len);
			return newstr;
		}

		int snprintf(char* str, size_t size, const char* format, ...)
		{
			int		ret;
			va_list argptr;

			va_start(argptr, format);
			ret = vsnprintf(str, size, format, argptr);
			va_end(argptr);

			return ret;
		}

		int wildcmp(const char* wild, const char* string)
		{ // case-insensitive string compare with wildcards. returns true for a match.
			while (*string)
			{
				if (*wild == '*')
				{
					if (*string == '/' || *string == '\\')
					{
						//* terminates if we get a match on the char following it, or if its a \ or / char
						wild++;
						continue;
					}
					if (wildcmp(wild + 1, string))
						return true;
					string++;
				}
				else if ((tolower(*wild) == tolower(*string)) || (*wild == '?'))
				{
					// this char matches
					wild++;
					string++;
				}
				else
				{
					// failure
					return false;
				}
			}

			while (*wild == '*')
			{
				wild++;
			}
			return !*wild;
		}

		void Info_RemoveKey(char* info, const char* key)
		{ // only shrinks, so no need for max size.
			size_t keylen = strlen(key);

			while (*info)
			{
				char* l = info;
				if (*info++ != '\\')
					break; // error / end-of-string

				if (!strncmp(info, key, keylen) && info[keylen] == '\\')
				{
					// skip the key name
					info += keylen + 1;
					// this is the old value for the key. skip over it
					while (*info && *info != '\\')
						info++;

					// okay, we found it. strip it out now.
					memmove(l, info, strlen(info) + 1);
					return;
				}
				else
				{
					// skip the key
					while (*info && *info != '\\')
						info++;

					// validate that its a value now
					if (*info++ != '\\')
						break; // error
					// skip the value
					while (*info && *info != '\\')
						info++;
				}
			}
		}

		void Info_SetKey(char* info, size_t infosize, const char* key, const char* val)
		{
			size_t keylen = strlen(key);
			size_t vallen = strlen(val);

			Info_RemoveKey(info, key);

			if (vallen)
			{
				char* o = info + strlen(info);
				char* e = info + infosize - 1;

				if (!*key || strchr(key, '\\') || strchr(val, '\\'))
					con->Warning("Info_SetKey(%s): invalid key/value\n", key);
				else if (o + 2 + keylen + vallen >= e)
					con->Warning("Info_SetKey(%s): length exceeds max\n", key);
				else
				{
					*o++ = '\\';
					memcpy(o, key, keylen);
					o += keylen;
					*o++ = '\\';
					memcpy(o, val, vallen);
					o += vallen;

					*o = 0;
				}
			}
		}

		size_t strlcat(char* dst, const char* src, size_t siz)
		{
			char* d = dst;
			const char* s = src;
			size_t		n = siz;
			size_t		dlen;

			/* Find the end of dst and adjust bytes left but don't go past end */
			while (n-- != 0 && *d != '\0')
				d++;
			dlen = d - dst;
			n = siz - dlen;

			if (n == 0)
				return (dlen + strlen(s));
			while (*s != '\0')
			{
				if (n != 1)
				{
					*d++ = *s;
					n--;
				}
				s++;
			}
			*d = '\0';

			return (dlen + (s - src)); /* count does not include NUL */
		}
	};
	class VID {
	public:
		Engine* engine;
		bool fullscreen;
		bool initialized = false;
		SDL_Window* draw_context;
		static HICON icon;

		//VID_Init()
		VID(Engine e) {
			engine = &e;
			fullscreen = false;
			auto display = new Display();

			_putenv("SDL_VIDEO_CENTERED = center");

			if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) { std::cout << ("SDL_Init failed: %s", SDL_GetError()) << std::endl; }

			SetMode(1280, 720, 60, false);
		}
		void PL_SetWindowIcon(void)
		{
			HINSTANCE	  handle;
			SDL_SysWMinfo wminfo;
			HWND		  hwnd;

			handle = GetModuleHandle(NULL);
			icon = LoadIcon(handle, L"icon");

			if (!icon)
				return; /* no icon in the exe */

			SDL_VERSION(&wminfo.version);

			if (SDL_GetWindowWMInfo((SDL_Window*)GetWindow(), &wminfo) != SDL_TRUE)
				return; /* wrong SDL version */

			hwnd = wminfo.info.win.window;
#ifdef _WIN64
			SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR)icon);
#else
			SetClassLong(hwnd, GCL_HICON, (LONG)icon);
#endif
		}
		void Gamma_Init(){
			std::cout << "Gamma_Init to be implemented: haven't implemented cvars yet" << std::endl;
		}
		int GetCurrentWidth() {
			int w, h;
			SDL_GetWindowSize(draw_context, &w, &h);
			return w;
		}
		int GetCurrentHeight() {
			int w, h;
			SDL_GetWindowSize(draw_context, &w, &h);
			return h;
		}
		int GetCurrentRefreshRate() {
			SDL_DisplayMode mode;
			SDL_GetCurrentDisplayMode(0, &mode);
			return mode.refresh_rate;
		}
		int GetCurrentBPP() {
			return SDL_BITSPERPIXEL(SDL_GetWindowPixelFormat(draw_context));
		}
		bool GetFullscreen() {
			return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN) != 0;
		}
		bool GetDesktopFullscreen() {
			return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		SDL_Window* GetWindow() {
			return draw_context;
		}
		bool HasMouseOrInputFocus() {
			return (SDL_GetWindowFlags(draw_context) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0;
		}
		bool IsMinimized() {
			return !(SDL_GetWindowFlags(draw_context) & SDL_WINDOW_SHOWN);
		}
		SDL_DisplayMode* SDL2_GetDisplayMode(int width, int height, int refreshrate) {
			SDL_DisplayMode mode;
			int sdlmodes = SDL_GetNumDisplayModes(0);
			int i = 0;

			for (i = 0; i < sdlmodes; i++)
			{
				if (SDL_GetDisplayMode(0, i, &mode) != 0)
					continue;

				if (mode.w == width && mode.h == height && SDL_BITSPERPIXEL(mode.format) >= 24 && mode.refresh_rate == refreshrate)
				{
					return &mode;
				}
			}
			return NULL;
		}

		void Shutdown() {
			if (initialized){
				SDL_QuitSubSystem(SDL_INIT_VIDEO);
				draw_context = NULL;
				DestroyIcon(icon);
			}
		}

		bool ValidMode(int width, int height, int refreshrate, bool fullscreen) {
			// ignore width / height / bpp if vid_desktopfullscreen is enabled
			if (fullscreen && GetDesktopFullscreen())
				return true;

			if (width < 320)
				return false;

			if (height < 200)
				return false;

			if (fullscreen && SDL2_GetDisplayMode(width, height, refreshrate) == NULL)
				return false;

			return true;
		}

		bool SetMode(int width, int height, int refreshrate, bool fullscreen) {
			int	   temp;
			Uint32 flags;
			char   caption[50];
			int	   previous_display;

			

			//TODO: quake shit
			if (!draw_context) {
				draw_context = SDL_CreateWindow("Tremor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_VULKAN);
				if (!draw_context) {
					std::cout << ("SDL_CreateWindow failed: %s", SDL_GetError()) << std::endl;
					return false;
				}
			}
			//TODO: more quake shit
		}

		class Display {
		public:
			int width, height;
			int refreshRate;
			Display() {
				width = 1920;
				height = 1080;
				refreshRate = 60;
			}
		};
	};
	class COM {
	public:
		Engine* engine;
		int argc;
		char** argv;
		char cmdline[CMDLINE_LENGTH];
		char* largv[MAX_NUM_ARGVS + 1];
		int safemode;


		COM(Engine e) {
			engine = &e;
			uint32_t uint_value = 0x12345678;
			uint8_t	 bytes[4];
			memcpy(bytes, &uint_value, sizeof(uint32_t));

			/*    U N I X */

			/*
			BE_ORDER:  12 34 56 78
				   U  N  I  X

			LE_ORDER:  78 56 34 12
				   X  I  N  U

			PDP_ORDER: 34 12 78 56
				   N  U  X  I
			*/
			if (bytes[0] != 0x78 || bytes[1] != 0x56 || bytes[2] != 0x34 || bytes[3] != 0x12)
				std::cout << ("Unsupported endianism. Only little endian is supported") << std::endl;
		}

		int OpenFile(const char* filename, int* handle, unsigned int* path_id)
		{
			return FindFile(filename, handle, NULL, path_id);
		}
		static int FindFile(const char* filename, int* handle, FILE** file, unsigned int* path_id)
		{
			searchpath_t* search;
			char		  netpath[MAX_OSPATH];
			pack_t* pak;
			int			  i;
			bool	  is_config = !qk->strcasecmp(filename, "config.cfg"), found = false;

			if (file && handle)
				sys->Error("COM_FindFile: both handle and file set");

			file_from_pak = 0;

			//
			// search through the path, one element at a time
			//
			for (search = com_searchpaths; search; search = search->next)
			{
				if (search->pack) /* look through all the pak file elements */
				{
					pak = search->pack;
					for (i = 0; i < pak->numfiles; i++)
					{
						if (strcmp(pak->files[i].name, filename) != 0)
							continue;
						// found it!
						com_filesize = pak->files[i].filelen;
						file_from_pak = 1;
						if (path_id)
							*path_id = search->path_id;
						if (handle)
						{
							*handle = pak->handle;
							Sys_FileSeek(pak->handle, pak->files[i].filepos);
							return com_filesize;
						}
						else if (file)
						{ /* open a new file on the pakfile */
							*file = fopen(pak->filename, "rb");
							if (*file)
								fseek(*file, pak->files[i].filepos, SEEK_SET);
							return com_filesize;
						}
						else /* for COM_FileExists() */
						{
							return com_filesize;
						}
					}
				}
				else /* check a file in the directory tree */
				{
					if (!registered.value)
					{ /* if not a registered version, don't ever go beyond base */
						if (strchr(filename, '/') || strchr(filename, '\\'))
							continue;
					}

					if (is_config)
					{
						q_snprintf(netpath, sizeof(netpath), "%s/" CONFIG_NAME, search->filename);
						if (Sys_FileType(netpath) & FS_ENT_FILE)
							found = true;
					}

					if (!found)
					{
						q_snprintf(netpath, sizeof(netpath), "%s/%s", search->filename, filename);
						if (!(Sys_FileType(netpath) & FS_ENT_FILE))
							continue;
					}

					if (path_id)
						*path_id = search->path_id;
					if (handle)
					{
						com_filesize = Sys_FileOpenRead(netpath, &i);
						*handle = i;
						return com_filesize;
					}
					else if (file)
					{
						*file = fopen(netpath, "rb");
						com_filesize = (*file == NULL) ? -1 : COM_filelength(*file);
						return com_filesize;
					}
					else
					{
						return 0; /* dummy valid value for COM_FileExists() */
					}
				}
			}

			if (strcmp(COM_FileGetExtension(filename), "pcx") != 0 && strcmp(COM_FileGetExtension(filename), "tga") != 0 &&
				strcmp(COM_FileGetExtension(filename), "lit") != 0 && strcmp(COM_FileGetExtension(filename), "vis") != 0 &&
				strcmp(COM_FileGetExtension(filename), "ent") != 0)
				Con_DPrintf("FindFile: can't find %s\n", filename);
			else
				Con_DPrintf2("FindFile: can't find %s\n", filename);

			if (handle)
				*handle = -1;
			if (file)
				*file = NULL;
			com_filesize = -1;
			return com_filesize;
		}


		byte* LoadFile(const char* path, unsigned int* path_id)
		{
			int	  h;
			byte* buf;
			char  base[32];
			int	  len;

			buf = NULL; // quiet compiler warning

			// look for it in the filesystem or pack files
			len = OpenFile(path, &h, path_id);
			if (h == -1)
				return NULL;

			// extract the filename base name for hunk tag
			COM_FileBase(path, base, sizeof(base));

			buf = (byte*)Mem_AllocNonZero(len + 1);

			if (!buf)
				Sys_Error("COM_LoadFile: not enough space for %s", path);

			((byte*)buf)[len] = 0;

			Sys_FileRead(h, buf, len);
			COM_CloseFile(h);

			return buf;
		}


		void InitArgv(int c, char** v)
		{
			int i, j, n;

			// reconstitute the command line for the cmdline externally visible cvar
			n = 0;

			for (j = 0; (j < MAX_NUM_ARGVS) && (j < c); j++)
			{
				i = 0;

				while ((n < (CMDLINE_LENGTH - 1)) && v[j][i])
				{
					cmdline[n++] = v[j][i++];
				}

				if (n < (CMDLINE_LENGTH - 1))
					cmdline[n++] = ' ';
				else
					break;
			}

			if (n > 0 && cmdline[n - 1] == ' ')
				cmdline[n - 1] = 0; // johnfitz -- kill the trailing space

			con->Printf("Command line: %s\n", cmdline);

			for (argc = 0; (argc < MAX_NUM_ARGVS) && (argc < c); argc++)
			{
				largv[argc] = v[argc];
				if (!strcmp("-safe", v[argc]))
					safemode = 1;
			}

			largv[argc] = argvdummy;
			argv = largv;

			/*if (COM_CheckParm("-rogue"))
			{
				rogue = true;
				standard_quake = false;
			}

			if (COM_CheckParm("-hipnotic") || COM_CheckParm("-quoth")) // johnfitz -- "-quoth" support
			{
				hipnotic = true;
				standard_quake = false;
			}*/
		}

		const char* ParseEx(const char* data, cpe_mode mode)
		{
			int c;
			int len;

			len = 0;
			com_token[0] = 0;

			if (!data)
				return NULL;

			// skip whitespace
		skipwhite:
			while ((c = *data) <= ' ')
			{
				if (c == 0)
					return NULL; // end of file
				data++;
			}

			// skip // comments
			if (c == '/' && data[1] == '/')
			{
				while (*data && *data != '\n')
					data++;
				goto skipwhite;
			}

			// skip /*..*/ comments
			if (c == '/' && data[1] == '*')
			{
				data += 2;
				while (*data && !(*data == '*' && data[1] == '/'))
					data++;
				if (*data)
					data += 2;
				goto skipwhite;
			}

			// handle quoted strings specially
			if (c == '\"')
			{
				data++;
				while (1)
				{
					if ((c = *data) != 0)
						++data;
					if (c == '\"' || !c)
					{
						com_token[len] = 0;
						return data;
					}
					if (len < countof(com_token) - 1)
						com_token[len++] = c;
					else if (mode == CPE_NOTRUNC)
						return NULL;
				}
			}

			// parse single characters
			if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' || c == ':')
			{
				if (len < countof(com_token) - 1)
					com_token[len++] = c;
				else if (mode == CPE_NOTRUNC)
					return NULL;
				com_token[len] = 0;
				return data + 1;
			}

			// parse a regular word
			do
			{
				if (len < countof(com_token) - 1)
					com_token[len++] = c;
				else if (mode == CPE_NOTRUNC)
					return NULL;
				data++;
				c = *data;
				/* commented out the check for ':' so that ip:port works */
				if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' /* || c == ':' */)
					break;
			} while (c > 32);

			com_token[len] = 0;
			return data;
		}
		const char* Parse(const char* data)
		{
			return ParseEx(data, CPE_NOTRUNC);
		}

	};
	class SCR{
	public:
		Engine* engine;
		bool disabled_for_loading;
		SCR(Engine e) {
			engine = &e;
			disabled_for_loading = false;
		}
		void EndLoadingPlaque(void)
		{
			disabled_for_loading = false;
			con->ClearNotify();
		}
	};
	class Key {
		bool	chat_team = false;
		char chat_buffer[MAXCMDLINE];
		int	chat_bufferlen = 0;

	public:
		Engine* engine;
		Key(Engine e) {
			engine = &e;
		}
		void EndChat(void)
		{
			key_dest = key_game;
			chat_bufferlen = 0;
			chat_buffer[0] = 0;
		}
	};
	class CL{
	public:
		Engine* engine;
		CL(Engine e) {
			engine = &e;
		}
		client_static_t s;
		client_state_t state;

		void Disconnect(void)
		{
			if (key_dest == key_message)
				key->EndChat(); // don't get stuck in chat mode

			// stop sounds (especially looping!)
			engine->s->StopAllSounds(true, false);
			BGM_Stop();
			CDAudio_Stop();

			// if running a local server, shut it down
			if (cls.demoplayback)
				CL_StopPlayback();
			else if (cls.state == ca_connected)
			{
				if (cls.demorecording)
					CL_Stop_f();

				con->DPrintf("Sending clc_disconnect\n");
				sz->Clear(&cls.message);
				msg->WriteByte(&cls.message, 2);
				NET_SendUnreliableMessage(cls.netcon, &cls.message);
				sz->Clear(&cls.message);
				NET_Close(cls.netcon);
				cls.netcon = NULL;

				cls.state = ca_disconnected;
				if (sv->active)
					host->ShutdownServer(false);
			}

			cls.demoplayback = cls.timedemo = false;
			cls.demopaused = false;
			cls.signon = 0;
			cls.netcon = NULL;
			cl->state.intermission = 0;
			cl->state.worldmodel = NULL;
			cl->state.sendprespawn = false;
			SCR_CenterPrintClear();
		}
		
	};
	class Con {
	public:
		Engine* engine;
		int linewidth;

		float cursorspeed = 4;

		#define CON_TEXTSIZE (1024 * 1024) // ericw -- was 65536. johnfitz -- new default size
		#define CON_MINSIZE	 16384		   // johnfitz -- old default, now the minimum size

		int buffersize; // johnfitz -- user can now override default

		bool forcedup; // because no entities to refresh

		int	  totallines; // total lines in console scrollback
		int	  backscroll; // lines up from bottom to display
		int	  current;	  // where next message will be printed
		int	  x;		  // offset in current line for next print
		char* text = NULL;

		cvar_t notifytime = { "con_notifytime", "3", CVAR_NONE };			// seconds
		cvar_t logcenterprint = { "con_logcenterprint", "1", CVAR_NONE }; // johnfitz

		char lastcenterstring[1024];				 // johnfitz
		void (*redirect_flush) (const char* buffer); // call this to flush the redirection buffer (for rcon)
		char redirect_buffer[8192];

		#define NUM_CON_TIMES 4
		float times[NUM_CON_TIMES]; // realtime time the line was generated
		// for transparent notify lines

		int vislines;

		bool debuglog = false;

		bool initialized;

		SDL_mutex* mutex;

		int history_line;

		float times[NUM_CON_TIMES]; // realtime time the line was generated
		
		Con(Engine e) {
			engine = &e;
		}

		const char* Quakebar(int len)
		{
			static char bar[42];
			int			i;

			len = (int)Min((void*)len, (void*)((int)sizeof(bar) - 2));
			len = (int)Min((void*)len, (void*)linewidth);

			bar[0] = '\35';
			for (i = 1; i < len - 1; i++)
				bar[i] = '\36';
			bar[len - 1] = '\37';

			if (len < linewidth)
			{
				bar[len] = '\n';
				bar[len + 1] = 0;
			}
			else
				bar[len] = 0;

			return bar;
		}

		void ClearNotify(void)
		{
			int i;

			for (i = 0; i < NUM_CON_TIMES; i++)
				times[i] = 0;
		}

		void DebugLog(const char* msg)
		{
			if (log_fd == -1)
				return;

			size_t msg_len = strlen(msg);
			if (_write(log_fd, msg, msg_len) != msg_len)
				return; // Nonsense to supress warning
		}

		static void Linefeed(void)
		{
			// johnfitz -- improved scrolling
			if (con->backscroll)
				con->backscroll++;
			if (con->backscroll > con->totallines - (vid->GetCurrentHeight() >> 3) - 1)
				con->backscroll = con->totallines - (vid->GetCurrentHeight() >> 3) - 1;
			// johnfitz

			con->x = 0;
			con->current++;
			memset(&con->text[(con->current % con->totallines) * con->linewidth], ' ', con->linewidth);
		}


		void Warning(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			SafePrintf("\x02Warning: ");
			Printf("%s", msg);
		}

		void DWarning(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];

			if (host->developer.value >= 2)
			{ // don't confuse non-developers with techie stuff...
				// (this is limit exceeded warnings)

				va_start(argptr, fmt);
				qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
				va_end(argptr);

				SafePrintf("\x02Warning: ");
				Printf("%s", msg);
			}
		}


		static void Print(const char* txt)
		{
			int		   y;
			int		   c, l;
			static int cr;
			int		   mask;
			bool   boundary;

			SDL_LockMutex(con->mutex);

			// con_backscroll = 0; //johnfitz -- better console scrolling

			if (txt[0] == 1)
			{
				mask = 128;						// go to colored text
				//S_LocalSound("misc/talk.wav"); // play talk wav
				txt++;
			}
			else if (txt[0] == 2)
			{
				mask = 128; // go to colored text
				txt++;
			}
			else
				mask = 0;

			boundary = true;

			while ((c = *txt))
			{
				if (c <= ' ')
				{
					boundary = true;
				}
				else if (boundary)
				{
					// count word length
					for (l = 0; l < con->linewidth; l++)
						if (txt[l] <= ' ')
							break;

					// word wrap
					if (l != con->linewidth && (con->x + l > con->linewidth))
						con->x = 0;

					boundary = false;
				}

				txt++;

				if (cr)
				{
					con->current--;
					cr = false;
				}

				if (!con->x)
				{
					Linefeed();
					// mark time for transparent overlay
					if (con->current >= 0)
						con->times[con->current % NUM_CON_TIMES] = host->realtime;
				}

				switch (c)
				{
				case '\n':
					con->x = 0;
					break;

				case '\r':
					con->x = 0;
					cr = 1;
					break;

				default: // display character and advance
					y = con->current % con->totallines;
					con->text[y * con->linewidth + con->x] = c | mask;
					con->x++;
					if (con->x >= con->linewidth)
						con->x = 0;
					break;
				}
			}
			SDL_UnlockMutex(con->mutex);
		}

		#define MAXPRINTMSG 4096
		void Printf(const char* fmt, ...)
		{
			va_list			argptr;
			char			msg[MAXPRINTMSG];
			static bool inupdate;

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			if (redirect_flush)
				qk->strlcat(redirect_buffer, msg, sizeof(redirect_buffer));
			// also echo to debugging console
			sys->Printf("%s", msg);

			// log all messages to file
			if (debuglog)
				DebugLog(msg);

			if (!initialized)
				return;

			if (cl->s.state == ca_dedicated)
				return; // no graphics mode

			// write it to the scrollable buffer
			Print(msg);

			// update the screen if the console is displayed
			if (cl->s.signon != SIGNONS && !scr->disabled_for_loading && !t->IsWorker())
			{
				// protect against infinite loop if something in SCR_UpdateScreen calls
				// Con_Printd
				if (!inupdate)
				{
					inupdate = true;
					//SCR_UpdateScreen(false); //remember to uncomment when implemented
					inupdate = false;
				}
			}
		}
	

		void DPrintf(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];

			if (!host->developer.value)
				return; // don't confuse non-developers with techie stuff...

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			SafePrintf("%s", msg); // johnfitz -- was Con_Printf
		}

		void SafePrintf(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];
			int		temp;

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			SDL_LockMutex(con->mutex);
			temp = scr->disabled_for_loading;
			scr->disabled_for_loading = true;
			Printf("%s", msg);
			scr->disabled_for_loading = temp;
			SDL_UnlockMutex(con->mutex);
		}


	};
	class Cmd {
	public:
		Engine* engine;
		bool wait = false;
		sizebuf_t text;
		int argc;
		char argv[MAX_ARGS][1024];
		const char* args = NULL;
		cmdalias_t* alias;

		typedef enum
		{
			src_client,	 // came in over a net connection as a clc_stringcmd. host_client will be valid during this state.
			src_command, // from the command buffer
			src_server	 // from a svc_stufftext
		} cmd_source_t;
		cmd_source_t cmd_source = src_command;
		typedef void (*xcommand_t) (void);
		typedef struct cmd_function_s
		{
			struct cmd_function_s* next;
			const char* name;
			xcommand_t			   function;
			cmd_source_t		   srctype;
			bool			   dynamic;
		} cmd_function_t;

		cmd_function_t* functions;

		cvar_t warncmd = { "cl_warncmd", "1", CVAR_NONE };

		void Wait_f() {
			wait = true;
		}
		Cmd(Engine e) {
			engine = &e;

		}

		cmd_function_t* AddCommand(const char* cmd_name, xcommand_t function, cmd_source_t srctype)
		{
			cmd_function_t* command;
			cmd_function_t* cursor, * prev; // johnfitz -- sorted list insert

			// fail if the command is a variable name
			if (cvar->VariableString(cmd_name)[0])
			{
				con->Printf("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
				return NULL;
			}

			// fail if the command already exists
			for (command = functions; command; command = command->next)
			{
				if (!strcmp(cmd_name, command->name) && command->srctype == srctype)
				{
					if (command->function != function && function)
						con->Printf("Cmd_AddCommand: %s already defined\n", cmd_name);
					return NULL;
				}
			}

			if (host->initialized)
			{
				command = (cmd_function_t*)mem->Alloc(sizeof(*cmd) + strlen(cmd_name) + 1);
				command->name = (const char*)strcpy_s((char*)(cmd + 1), sizeof((char*)(cmd + 1)), cmd_name);
				command->dynamic = true;
			}
			else
			{
				command = (cmd_function_t*)mem->Alloc(sizeof(*cmd));
				command->name = cmd_name;
				command->dynamic = false;
			}
			command->function = function;
			command->srctype = srctype;

			// johnfitz -- insert each entry in alphabetical order
			if (functions == NULL || strcmp(command->name, functions->name) < 0) // insert at front
			{
				command->next = functions;
				functions = command;
			}
			else // insert later
			{
				prev = functions;
				cursor = functions->next;
				while ((cursor != NULL) && (strcmp(command->name, cursor->name) > 0))
				{
					prev = cursor;
					cursor = cursor->next;
				}
				command->next = prev->next;
				prev->next = command;
			}
			// johnfitz

			if (command->dynamic)
				return command;
			return NULL;
		}
		int Argc() {
			return argc;
		}
		const char* Argv(int arg) {
			if (arg < 0 || arg >= argc)
				return "";
			return argv[arg];
		}

		const char* Args(void)
		{
			if (!args)
				return "";
			return args;
		}

		void TokenizeString(const char* text)
		{
			int i;

			// clear the args from the last string
			for (i = 0; i < argc; i++)
				argv[i][0] = 0;

			argc = 0;
			args = NULL;

			while (1)
			{
				// skip whitespace up to a /n
				while (*text && *text <= ' ' && *text != '\n')
				{
					text++;
				}

				if (*text == '\n')
				{ // a newline seperates commands in the buffer
					text++;
					break;
				}

				if (!*text)
					return;

				if (argc == 1)
					args = text;

				text = com->Parse(text);
				if (!text)
					return;

				if (argc < MAX_ARGS)
				{
					strcpy(argv[argc], com_token);
					argc++;
				}
			}
		}

		bool ExecuteString(const char* text, cmd_source_t src)
		{
			cmd_function_t* command;
			cmdalias_t* a;

			cmd_source = src;
			TokenizeString(text);

			// execute the command line
			if (!Argc())
				return true; // no tokens

			// check functions
			for (command = functions; command; command = command->next)
			{
				if (!qk->strcasecmp(argv[0], command->name))
				{
					if (src == src_client && command->srctype != src_client)
						con->DPrintf("%s tried to %s\n", host->client->name, text); // src_client only allows client commands
					else if (src == src_command && command->srctype == src_server)
						continue; // src_command can execute anything but server commands (which it ignores, allowing for alternative behaviour)
					else if (src == src_server && command->srctype != src_server)
						continue; // src_server may only execute server commands (such commands must be safe to parse within the context of a network message, so no
					// disconnect/connect/playdemo/etc)
					command->function();
					return true;
				}
			}

			if (src == src_client)
			{ // spike -- please don't execute similarly named aliases, nor custom cvars...
				con->DPrintf("%s tried to %s\n", host->client->name, text);
				return false;
			}
			if (src != src_command)
				return false;

			// check alias
			for (a = alias; a; a = a->next)
			{
				if (!qk->strcasecmp(argv[0], a->name))
				{
					cbuf->InsertText(a->value);
					return true;
				}
			}

			// check cvars
			if (!cvar->Command())
				if (warncmd.value || host->developer.value)
					con->Printf("Unknown command \"%s\"\n", Argv(0));

			return true;
		}
	};
	class Cbuf {
	public:
		Engine* engine;
		bool wait = false;
		cmd_source_t source;

		Cbuf(Engine e) {
			engine = &e;
			sz->Alloc(&cmd->text, 1 << 18);

		}

		void Wait_f() {
			wait = true;
		}

		void AddText(const char* text)
		{
			int l;

			l = strlen(text);

			if (&cmd->text.cursize + l >= &cmd->text.maxsize)
			{
				con->Printf("Cbuf_AddText: overflow\n");
				return;
			}

			sz->Write(&cmd->text, text, l);
		}

		void AddTextLen(const char* text, int length)
		{
			if (&cmd->text.cursize + length >= &cmd->text.maxsize)
			{
				con->Printf("Cbuf_AddText: overflow\n");
				return;
			}
			sz->Write(&cmd->text, text, length);
		}

		void InsertText(const char* text)
		{
			char* temp;
			int	  templen;

			// copy off any commands still remaining in the exec buffer
			templen = cmd->text.cursize;
			if (templen)
			{
				temp = (char*)mem->Alloc(templen);
				memcpy(temp, cmd->text.data, templen);
				sz->Clear(&cmd->text);
			}
			else
				temp = NULL; // shut up compiler

			// add the entire text of the file
			AddText(text);
			sz->Write(&cmd->text, "\n", 1);
			// add the copied off data
			if (templen)
			{
				sz->Write(&cmd->text, temp, templen);
				mem->Free(temp);
			}
		}
		void Waited(void)
		{
			wait = false;
		}

		void Execute(void)
		{
			int	  i;
			char* text;
			char  line[1024];
			int	  quotes, comment;

			while (cmd->text.cursize && !cmd->wait)
			{
				// find a \n or ; line break
				text = (char*)cmd->text.data;

				quotes = 0;
				comment = 0;
				for (i = 0; i < cmd->text.cursize; i++)
				{
					if (text[i] == '"')
						quotes++;
					if (text[i] == '/' && text[i + 1] == '/')
						comment = true;
					if (!(quotes & 1) && !comment && text[i] == ';')
						break; // don't break if inside a quoted string
					if (text[i] == '\n')
						break;
				}

				if (i > (int)sizeof(line) - 1)
				{
					memcpy(line, text, sizeof(line) - 1);
					line[sizeof(line) - 1] = 0;
				}
				else
				{
					memcpy(line, text, i);
					line[i] = 0;
				}

				// delete the text from the command buffer and move remaining commands down
				// this is necessary because commands (exec, alias) can insert data at the
				// beginning of the text buffer

				if (i == cmd->text.cursize)
					cmd->text.cursize = 0;
				else
				{
					i++;
					cmd->text.cursize -= i;
					memmove(text, text + i, cmd->text.cursize);
				}

				// execute the command line
				cmd->ExecuteString(line, Cmd::src_command);
                
			}
		}

	};
	class Cvar {
	public:
		Engine* engine;
		static cvar_t* vars;

		Cvar(Engine e) {
			engine = &e;
		}
		void Reset(const char* name);
		void List_f() {

		}
		bool Exists(const char* cmd_name) {
			
		}
		void RegisterVariable(Cvar* variable) {
			char value[512];
			bool set_rom;
			Cvar* cursor, *prev;
		}
		cvar_t* FindVar(const char* var_name)
		{
			cvar_t* var;

			for (var = vars; var; var = var->next)
			{
				if (!strcmp(var_name, var->name))
					return var;
			}

			return NULL;
		}
		const char* VariableString(const char* var_name)
		{
			cvar_t* var;

			var = FindVar(var_name);
			if (!var)
				return cvar_null_string;
			return var->string;
		}
		bool Command(void)
		{
			cvar_t* v;

			// check variables
			v = FindVar(cmd->Argv(0));
			if (!v)
				return false;

			// perform a variable print or set
			if (cmd->Argc() == 1)
			{
				con->Printf("\"%s\" is \"%s\"\n", v->name, v->string);
				return true;
			}

			Set(v->name, cmd->Argv(1));
			return true;
		}

		void Set(const char* var_name, const char* value)
		{
			cvar_t* var;

			var = FindVar(var_name);
			if (!var)
			{ // there is an error in C code if this happens
				con->Printf("Cvar_Set: variable %s not found\n", var_name);
				return;
			}

			SetQuick(var, value);
		}

		void SetQuick(cvar_t* var, const char* value)
		{
			if (var->flags & (CVAR_ROM | CVAR_LOCKED))
				return;
			if (!(var->flags & CVAR_REGISTERED))
				return;

			if (!var->string)
				var->string = qk->strdup(value);
			else
			{
				int len;

				if (!strcmp(var->string, value))
					return; // no change

				var->flags |= CVAR_CHANGED;
				len = strlen(value);
				if (len != strlen(var->string))
				{
					mem->Free((void*)var->string);
					var->string = (char*)mem->Alloc(len + 1);
				}
				memcpy((char*)var->string, value, len + 1);
			}

			var->value = atof(var->string);

			// johnfitz -- save initial value for "reset" command
			if (!var->default_string)
				var->default_string = qk->strdup(var->string);
			// johnfitz -- during initialization, update default too
			else if (!host->initialized)
			{
				//	Sys_Printf("changing default of %s: %s -> %s\n",
				//		   var->name, var->default_string, var->string);
				mem->Free((void*)var->default_string);
				var->default_string = qk->strdup(var->string);
			}
			// johnfitz

			if (var->callback)
				var->callback(var);
			if (var->flags & CVAR_AUTOCVAR)
				PR_AutoCvarChanged(var);
		}
	};
	class Net {
	public:
		Engine* engine;
		double time;
		Net(Engine e) {
			engine = &e;
		}
		double SetNetTime(void)
		{
			time = sys->DoubleTime();
			return time;
		}
		bool CanSendMessage(qsocket_t* sock)
		{
			if (!sock)
				return false;

			if (sock->disconnected)
				return false;

			SetNetTime();

			return sfunc.CanSendMessage(sock);
		}

	};
	class Mem {
	public:
		Engine* engine;
		Mem(Engine e) {
			engine = &e;
		}
		void Init() {
			max_thread_stack_alloc_size = MAX_STACK_ALLOC_SIZE;
		}
		void* Alloc(const size_t size) {
			return SDL_calloc(1, size);
		}

		void* Realloc(void* ptr, const size_t size) {
			if (ptr) {
				return SDL_realloc(ptr, size);
			}
			else {
				return Alloc(size);
			}
		}

		void Free(const void* ptr) {
			if (ptr) {
				free((void*)ptr);
			}
		}
	};
	class Host {
	public:
		Engine* engine;
		parms_t* parms;
		bool initialized;
		double frametime;
		double realtime;
		double oldrealtime;

		cvar_t developer = { "developer", "0", CVAR_NONE };

		int framecount;

		int minimum_memory;

		client_t* client;

		void Shutdown() {

		}

		void ShutdownServer(bool crash)
		{
			int		  i;
			int		  count;
			sizebuf_t buf;
			byte	  message[4];
			double	  start;

			if (!sv->active)
				return;

			sv->active = false;

			// stop all client sounds immediately
			if (cl->s.state == ca_connected)
				cl->Disconnect();

			// flush any pending messages - like the score!!!
			start = sys->DoubleTime();
			do
			{
				count = 0;
				for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
				{
					if (client->active && client->message.cursize && client->netconnection)
					{
						if (net->CanSendMessage(client->netconnection))
						{
							NET_SendMessage(client->netconnection, &client->message);
							sz->Clear(&client->message);
						}
						else
						{
							NET_GetMessage(client->netconnection);
							count++;
						}
					}
				}
				if ((sys->DoubleTime() - start) > 3.0)
					break;
			} while (count);

			// make sure all the clients know we're disconnecting
			buf.data = message;
			buf.maxsize = 4;
			buf.cursize = 0;
			MSG_WriteByte(&buf, svc_disconnect);
			count = NET_SendToAll(&buf, 5.0);
			if (count)
				con->Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

			pr->SwitchQCVM(&sv->qcvm);
			for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
				if (client->active)
					SV_DropClient(crash);

			pr->qcvm->worldmodel = NULL;
			pr->SwitchQCVM(NULL);

			//
			// clear structures
			//
			//	memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
			memset(svs.clients, 0, svs.maxclientslimit * sizeof(client_t));
		}


		void Error(const char* error, ...)
		{
			va_list			argptr;
			char			string[1024];
			static bool inerror = false;

			if (inerror)
				sys->Error("Host_Error: recursively entered");
			inerror = true;

			pr->SwitchQCVM(NULL);

			scr->EndLoadingPlaque(); // reenable screen updates

			va_start(argptr, error);
			qk->vsnprintf(string, sizeof(string), error, argptr);
			va_end(argptr);
			con->Printf("Host_Error: %s\n", string);

			if (cl->state.qcvm.extfuncs.CSQC_DrawHud && in_update_screen)
			{
				inerror = false;
				longjmp(screen_error, 1);
			}

			if (sv->active)
				ShutdownServer(false);

			if (cl->s.state == ca_dedicated)
				sys->Error("Host_Error: %s\n", string); // dedicated servers exit

			cl->Disconnect();
			cl->s.demonum = -1;
			cl->state.intermission = 0; // johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

			inerror = false;

			longjmp(host_abortserver, 1);
		}


		Host(Engine e, int argc, char* argv[]) {

			engine = &e;

			double time, oldtime, newtime;

			parms = new parms_t;

			parms->basedir = ".";
			parms->argc = argc;
			parms->argv = argv;

			parms->errstate = 0;


			con = new Con(*engine);
			com = new COM(*engine);

			com->InitArgv(argc, argv);

			engine = &e;

			engine->mem = new Mem(*engine);
			engine->t = new Tasks(*engine);
			engine->cmd = new Cmd(*engine);
			engine->cbuf = new Cbuf(*engine);



		}
	};
	class Tasks {
	public:
		Engine* engine;
		Tasks(Engine e) {
			engine = &e;

			free_task_queue = CreateTaskQueue(MAX_PENDING_TASKS);
			executable_task_queue = CreateTaskQueue(MAX_EXECUTABLE_TASKS);

			for (uint32_t task_index = 0; task_index < (MAX_PENDING_TASKS - 1); ++task_index)
			{
				TaskQueuePush(free_task_queue, task_index);
			}

			for (uint32_t task_index = 0; task_index < MAX_PENDING_TASKS; ++task_index)
			{
				tasks[task_index].epoch_mutex = SDL_CreateMutex();
				tasks[task_index].epoch_condition = SDL_CreateCond();
			}

			num_workers = (int)(Clamp((void*)1, (void*)SDL_GetCPUCount(), (void*)TASKS_MAX_WORKERS));

			// Fill lookup table to avoid modulo in Task_ExecuteIndexed
			for (int i = 0; i < num_workers; ++i)
			{
				steal_worker_indices[i] = i;
				steal_worker_indices[i + num_workers] = i;
			}

			indexed_task_counters = (task_counter_t*)mem->Alloc(sizeof(task_counter_t) * num_workers * MAX_PENDING_TASKS);
			worker_threads = (SDL_Thread**)mem->Alloc(sizeof(SDL_Thread*) * num_workers);
			for (int i = 0; i < num_workers; ++i)
			{
				worker_threads[i] = SDL_CreateThread(Worker, "Task_Worker", (void*)(intptr_t)i);
			}
		}
		static inline int IndexedTaskCounterIndex(int task_index, int worker_index)
		{
			return (MAX_PENDING_TASKS * worker_index) + task_index;
		}
		static inline uint32_t IndexFromTaskHandle(task_handle_t handle)
		{
			return handle & (MAX_PENDING_TASKS - 1);
		}
		static inline uint64_t EpochFromTaskHandle(task_handle_t handle)
		{
			return handle >> NUM_INDEX_BITS;
		}
		static inline task_handle_t CreateTaskHandle(uint32_t index, int epoch)
		{
			return (task_handle_t)index | ((task_handle_t)epoch << NUM_INDEX_BITS);
		}
		static uint32_t ShuffleIndex(uint32_t i)
		{
			// Swap bits 0-3 and 4-7 to avoid false sharing
			return (i & ~0xFF) | ((i & 0xF) << 4) | ((i >> 4) & 0xF);
		}
		static inline void CPUPause() {
			// Pause the CPU for a short time
			// This is a placeholder for an actual implementation
			_mm_pause();
		}
		static inline void SpinWaitSemaphore(SDL_sem* semaphore)
		{
			int remaining_spins = WAIT_SPIN_COUNT;
			int result = 0;
			while ((result = SDL_SemTryWait(semaphore)) != 0)
			{
				CPUPause();
				if (--remaining_spins == 0)
					break;
			}
			if (result != 0)
				SDL_SemWait(semaphore);
		}
		static task_queue_t* CreateTaskQueue(int capacity)
		{
			assert(capacity > 0);
			assert((capacity & (capacity - 1)) == 0); // Needs to be power of 2
			task_queue_t* queue = (task_queue_t*)mem->Alloc(sizeof(task_queue_t) + (sizeof(atomic_uint32_t) * (capacity - 1)));
			queue->capacity_mask = capacity - 1;
			queue->push_semaphore = SDL_CreateSemaphore(capacity - 1);
			queue->pop_semaphore = SDL_CreateSemaphore(0);
			return queue;
		}
		static inline void TaskQueuePush(task_queue_t* queue, uint32_t task_index)
		{
			SpinWaitSemaphore(queue->push_semaphore);
			uint32_t head = Atomic_LoadUInt32(&queue->head);
			bool cas_successful = false;
			do
			{
				const uint32_t next = (head + 1u) & queue->capacity_mask;
				cas_successful = Atomic_CompareExchangeUInt32(&queue->head, &head, next);
			} while (!cas_successful);

			const uint32_t shuffled_index = ShuffleIndex(head);
			while (Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) != 0u)
				CPUPause();

			ANNOTATE_HAPPENS_BEFORE(&queue->task_indices[shuffled_index]);
			Atomic_StoreUInt32(&queue->task_indices[shuffled_index], task_index + 1);
			SDL_SemPost(queue->pop_semaphore);
		}
		static inline uint32_t TaskQueuePop(task_queue_t* queue)
		{
			SpinWaitSemaphore(queue->pop_semaphore);
			uint32_t tail = Atomic_LoadUInt32(&queue->tail);
			bool cas_successful = false;
			do
			{
				const uint32_t next = (tail + 1u) & queue->capacity_mask;
				cas_successful = Atomic_CompareExchangeUInt32(&queue->tail, &tail, next);
			} while (!cas_successful);

			const uint32_t shuffled_index = ShuffleIndex(tail);
			while (Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) == 0u)
				CPUPause();

			const uint32_t val = Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) - 1;
			Atomic_StoreUInt32(&queue->task_indices[shuffled_index], 0u);
			SDL_SemPost(queue->push_semaphore);
			ANNOTATE_HAPPENS_AFTER(&queue->task_indices[shuffled_index]);

			return val;
		}
		static inline void ExecuteIndexed(int worker_index, task_t* task, uint32_t task_index)
		{
			for (int i = 0; i < num_workers; ++i)
			{
				const int		steal_worker_index = steal_worker_indices[worker_index + i];
				int				counter_index = IndexedTaskCounterIndex(task_index, steal_worker_index);
				task_counter_t* counter = &indexed_task_counters[counter_index];
				uint32_t		index = 0;
				while ((index = Atomic_IncrementUInt32(&counter->index)) < counter->limit)
				{
					((task_indexed_func_t)task->func) (index, task->payload);
				}
			}
		}
		static int Worker(void* data)
		{
			is_worker = true;

			const int worker_index = (intptr_t)data;
			tl_worker_index = worker_index;
			while (true)
			{
				uint32_t task_index = TaskQueuePop(executable_task_queue);
				task_t* task = &tasks[task_index];
				ANNOTATE_HAPPENS_AFTER(task);

				if (task->task_type == TASK_TYPE_SCALAR)
				{
					((task_func_t)task->func) (task->payload);
				}
				else if (task->task_type == TASK_TYPE_INDEXED)
				{
					ExecuteIndexed(worker_index, task, task_index);
				}

#if defined(USE_HELGRIND)
				ANNOTATE_HAPPENS_BEFORE(task);
				qboolean indexed_task = task->task_type == TASK_TYPE_INDEXED;
				if (indexed_task)
				{
					// Helgrind needs to know about all threads
					// that participated in an indexed execution
					SDL_LockMutex(task->epoch_mutex);
					for (int i = 0; i < task->num_dependents; ++i)
					{
						const int task_index = IndexFromTaskHandle(task->dependent_task_handles[i]);
						task_t* dep_task = &tasks[task_index];
						ANNOTATE_HAPPENS_BEFORE(dep_task);
					}
				}
#endif

				if (Atomic_DecrementUInt32(&task->remaining_workers) == 1)
				{
					SDL_LockMutex(task->epoch_mutex);
					for (int i = 0; i < task->num_dependents; ++i)
						t->Submit(task->dependent_task_handles[i]);
					task->epoch += 1;
					SDL_CondBroadcast(task->epoch_condition);
					SDL_UnlockMutex(task->epoch_mutex);
					TaskQueuePush(free_task_queue, task_index);
				}

#if defined(USE_HELGRIND)
				if (indexed_task)
					SDL_UnlockMutex(task->epoch_mutex);
#endif
			}
			return 0;
		}
		int NumWorkers(void)
		{
			return num_workers;
		}
		bool IsWorker(void)
		{
			return is_worker;
		}
		int GetWorkerIndex(void)
		{
			return tl_worker_index;
		}
		task_handle_t Allocate(void)
		{
			uint32_t task_index = TaskQueuePop(free_task_queue);
			task_t* task = &tasks[task_index];
			Atomic_StoreUInt32(&task->remaining_dependencies, 1);
			task->task_type = TASK_TYPE_NONE;
			task->num_dependents = 0;
			task->indexed_limit = 0;
			task->func = NULL;
			return CreateTaskHandle(task_index, task->epoch);
		}
		void AssignFunc(task_handle_t handle, task_func_t func, void* payload, size_t payload_size)
		{
			assert(payload_size <= MAX_PAYLOAD_SIZE);
			task_t* task = &tasks[IndexFromTaskHandle(handle)];
			task->task_type = TASK_TYPE_SCALAR;
			task->func = (void*)func;
			if (payload)
				memcpy(&task->payload, payload, payload_size);
		}
		void AssignIndexedFunc(task_handle_t handle, task_indexed_func_t func, uint32_t limit, void* payload, size_t payload_size)
		{
			assert(payload_size <= MAX_PAYLOAD_SIZE);
			uint32_t task_index = IndexFromTaskHandle(handle);
			task_t* task = &tasks[task_index];
			task->task_type = TASK_TYPE_INDEXED;
			task->func = (void*)func;
			task->indexed_limit = limit;
			uint32_t index = 0;
			uint32_t count_per_worker = (limit + num_workers - 1) / num_workers;
			for (int worker_index = 0; worker_index < num_workers; ++worker_index)
			{
				const int		task_counter_index = IndexedTaskCounterIndex(task_index, worker_index);
				task_counter_t* counter = &indexed_task_counters[task_counter_index];
				Atomic_StoreUInt32(&counter->index, index);
				counter->limit = (uint32_t)(Min((void*)(index + count_per_worker), (void*)limit));
				index += count_per_worker;
			}
			if (payload)
				memcpy(&task->payload, payload, payload_size);
		}
		void Submit(task_handle_t handle)
		{
			uint32_t task_index = IndexFromTaskHandle(handle);
			task_t* task = &tasks[task_index];
			assert(task->epoch == EpochFromTaskHandle(handle));
			ANNOTATE_HAPPENS_BEFORE(task);
			if (Atomic_DecrementUInt32(&task->remaining_dependencies) == 1)
			{
				const int num_task_workers = (task->task_type == TASK_TYPE_INDEXED) ? (int)(Min((void*)task->indexed_limit, (void*)num_workers)) : 1;
				Atomic_StoreUInt32(&task->remaining_workers, num_task_workers);
				for (int i = 0; i < num_task_workers; ++i)
				{
					TaskQueuePush(executable_task_queue, task_index);
				}
			}
		}



	};
	class PR {
	public:
		Engine* engine;
		PR(Engine e) {
			engine = &e;
		}
		qcvm_t* qcvm;
		globalvars_t* pr_global_struct;
		void		  SwitchQCVM(qcvm_t* nvm)
		{
			if (qcvm && nvm)
				sys->Error("PR_SwitchQCVM: A qcvm was already active");
			qcvm = nvm;
			if (qcvm)
				pr_global_struct = (globalvars_t*)qcvm->globals;
			else
				pr_global_struct = NULL;
		}

		const char* GetString(int num)
		{
			if (num >= 0 && num < qcvm->stringssize)
				return qcvm->strings + num;
			else if (num < 0 && num >= -qcvm->numknownstrings)
			{
				if (!qcvm->knownstrings[-1 - num])
				{
					host->Error("PR_GetString: attempt to get a non-existant string %d\n", num);
					return "";
				}
				return qcvm->knownstrings[-1 - num];
			}
			else
			{
				return qcvm->strings;
				host->Error("PR_GetString: invalid string offset %d\n", num);
				return "";
			}
		}


		void AutoCvarChanged(cvar_t* var)
		{
			char* n;
			ddef_t* glob;
			qcvm_t* oldqcvm = qcvm;
			SwitchQCVM(NULL);
			if (sv.active)
			{
				SwitchQCVM(&sv.qcvm);
				n = qk->va("autocvar_%s", var->name);
				glob = ED_FindGlobal(n);
				if (glob)
				{
					if (!ed->ParseEpair((void*)qcvm->globals, glob, var->string, true))
						con->Warning("EXT: Unable to configure %s\n", n);
				}
				SwitchQCVM(NULL);
			}
			if (cl->state.qcvm.globals)
			{
				SwitchQCVM(&cl->state.qcvm);
				n = qk->va("autocvar_%s", var->name);
				glob = ED_FindGlobal(n);
				if (glob)
				{
					if (!ed->ParseEpair((void*)qcvm->globals, glob, var->string, true))
						con->Warning("EXT: Unable to configure %s\n", n);
				}
				SwitchQCVM(NULL);
			}
			SwitchQCVM(oldqcvm);
		}
	};
	class SZ {
	public:
		Engine* engine;
		SZ(Engine e) {
			engine = &e;
		}
		void Alloc(sizebuf_t* buf, int startsize) {
			if (startsize < 256)
				startsize = 256;
			buf->data = (byte*)mem->Alloc(startsize);
			buf->maxsize = startsize;
			buf->cursize = 0;
		}
		void Free(sizebuf_t* buf) {
			mem->Free(buf->data);
			buf->data = NULL;
			buf->maxsize = 0;
			buf->cursize = 0;
		}
		void Clear(sizebuf_t* buf) {
			buf->cursize = 0;
			buf->overflowed = false;
		}

		void* GetSpace(sizebuf_t* buf, int length) {
			void* data;

			if (buf->cursize + length > buf->maxsize)
			{
				if (!buf->allowoverflow)
					std::cout << ("SZ_GetSpace: overflow without allowoverflow set") << std::endl; 

				if (length > buf->maxsize)
					std::cout << ("SZ_GetSpace: %i is > full buffer size", length) << std::endl;

				std::cout << ("SZ_GetSpace: overflow\n") << std::endl;
				Clear(buf);
				buf->overflowed = true;
			}

			data = buf->data + buf->cursize;
			buf->cursize += length;

			return data;


		}

		void Write(sizebuf_t* buf, const void* data, int length)
		{
			memcpy(GetSpace(buf, length), data, length);
		}

		void Print(sizebuf_t* buf, const char* data)
		{
			int len = strlen(data) + 1;

			if (buf->data[buf->cursize - 1])
			{ /* no trailing 0 */
				memcpy((byte*)GetSpace(buf, len), data, len);
			}
			else
			{ /* write over trailing 0 */
				memcpy((byte*)GetSpace(buf, len - 1) - 1, data, len);
			}
		}
	};
	class S {
	public:
		Engine* engine;
		bool initialized;
		bool started;
		SDL_mutex* mutex;

		int paintedtime;

		channel_t channels[MAX_CHANNELS];
		int		  total_channels;

		S(Engine e) {
			engine = &e;
		}
		sfxcache_t* LoadSound(sfx_t* s)
		{
			char		namebuffer[256];
			byte* data = NULL;
			wavinfo_t	info;
			int			len;
			float		stepscale;
			sfxcache_t* sc = NULL;

			SDL_LockMutex(mutex);

			// see if still in memory
			if (s->cache)
			{
				sc = s->cache;
				goto unlock_mutex;
			}

			//	Con_Printf ("S_LoadSound: %x\n", (int)stackbuf);

			// load it in
			qk->strlcpy(namebuffer, "sound/", sizeof(namebuffer));
			qk->strlcat(namebuffer, s->name, sizeof(namebuffer));

			//	Con_Printf ("loading %s\n",namebuffer);

			data = com->LoadFile(namebuffer, NULL);

			if (!data)
			{
				con->Printf("Couldn't load %s\n", namebuffer);
				goto unlock_mutex;
			}

			info = GetWavinfo(s->name, data, com_filesize);
			if (info.channels != 1)
			{
				con->Printf("%s is a stereo sample\n", s->name);
				goto unlock_mutex;
			}

			if (info.width != 1 && info.width != 2)
			{
				con->Printf("%s is not 8 or 16 bit\n", s->name);
				goto unlock_mutex;
			}

			stepscale = (float)info.rate / shm->speed;
			len = info.samples / stepscale;

			len = len * info.width * info.channels;

			if (info.samples == 0 || len == 0)
			{
				con->Printf("%s has zero samples\n", s->name);
				goto unlock_mutex;
			}

			sc = (sfxcache_t*)mem->Alloc(len + sizeof(sfxcache_t));
			if (!sc)
				goto unlock_mutex;
			sc->length = info.samples;
			sc->loopstart = info.loopstart;
			sc->speed = info.rate;
			sc->width = info.width;
			sc->stereo = info.channels;

			s->cache = sc;
			ResampleSfx(s, sc->speed, sc->width, data + info.dataofs);

		unlock_mutex:
			mem->Free(data);
			SDL_UnlockMutex(mutex);
			return sc;
		}


		void StopAllSounds(bool clear, bool keep_statics)
		{
			int i;

			if (!initialized)
				return;

			SDL_LockMutex(mutex);
			if (!started)
				goto unlock_mutex;

			if (!keep_statics)
				total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; // no statics

			for (i = 0; i < MAX_CHANNELS; i++)
			{
				if (!keep_statics || channels[i].entnum || !channels[i].sfx || !S_LoadSound(snd_channels[i].sfx) ||
					S_LoadSound(snd_channels[i].sfx)->loopstart == -1)
					memset(&snd_channels[i], 0, sizeof(channel_t));
				else
				{
					snd_channels[i].pos = 0;
					snd_channels[i].end = paintedtime + S_LoadSound(snd_channels[i].sfx)->length;
				}
			}

			if (clear)
				ClearBuffer();

		unlock_mutex:
			SDL_UnlockMutex(mutex);
		}

		channel_t* PickChannel(int entnum, int entchannel)
		{
			int ch_idx;
			int first_to_die;
			int life_left;

			// Check for replacement sound, or find the best one to replace
			first_to_die = -1;
			life_left = 0x7fffffff;
			for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++)
			{
				if (entchannel != 0 // channel 0 never overrides
					&& channels[ch_idx].entnum == entnum && (channels[ch_idx].entchannel == entchannel || entchannel == -1))
				{ // always override sound from same entity
					first_to_die = ch_idx;
					break;
				}

				// don't let monster sounds override player sounds
				if (channels[ch_idx].entnum == cl->state.viewentity && entnum != cl->state.viewentity && channels[ch_idx].sfx)
					continue;

				if (channels[ch_idx].end - paintedtime < life_left)
				{
					life_left = channels[ch_idx].end - paintedtime;
					first_to_die = ch_idx;
				}
			}

			if (first_to_die == -1)
				return NULL;

			if (channels[first_to_die].sfx)
				channels[first_to_die].sfx = NULL;

			return &channels[first_to_die];
		}



		void StartSound(int entnum, int entchannel, sfx_t* sfx, vec3_t origin, float fvol, float attenuation)
		{
			channel_t* target_chan, * check;
			sfxcache_t* sc;
			int			ch_idx;
			int			skip;

			SDL_LockMutex(mutex);
			if (!started || !sfx || nosound.value)
				goto unlock_mutex;

			// pick a channel to play on
			target_chan = PickChannel(entnum, entchannel);
			if (!target_chan)
				goto unlock_mutex;

			// spatialize
			memset(target_chan, 0, sizeof(*target_chan));
			VectorCopy(origin, target_chan->origin);
			target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
			target_chan->master_vol = (int)(fvol * 255);
			target_chan->entnum = entnum;
			target_chan->entchannel = entchannel;
			SND_Spatialize(target_chan);

			if (!target_chan->leftvol && !target_chan->rightvol)
				goto unlock_mutex;

			// new channel
			sc = S_LoadSound(sfx);
			if (!sc)
			{
				target_chan->sfx = NULL;
				goto unlock_mutex; // couldn't load the sound's data
			}

			target_chan->sfx = sfx;
			target_chan->pos = 0.0;
			target_chan->end = paintedtime + sc->length;

			// if an identical sound has also been started this frame, offset the pos
			// a bit to keep it from just making the first one louder
			check = &channels[NUM_AMBIENTS];
			for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++, check++)
			{
				if (check == target_chan)
					continue;
				if (check->sfx == sfx && !check->pos)
				{
					/*
					skip = COM_Rand () % (int)(0.1 * shm->speed);
					if (skip >= target_chan->end)
						skip = target_chan->end - 1;
					*/
					/* LordHavoc: fixed skip calculations */
					skip = 0.1 * shm->speed; /* 0.1 * sc->speed */
					if (skip > sc->length)
						skip = sc->length;
					if (skip > 0)
						skip = COM_Rand() % skip;
					target_chan->pos += skip;
					target_chan->end -= skip;
					break;
				}
			}

		unlock_mutex:
			SDL_UnlockMutex(mutex);
		}


		void ClearBuffer(void)
		{
			int clear;

			SDL_LockMutex(mutex);

			if (!started || !shm)
				goto unlock_mutex;

			SNDDMA_LockBuffer();
			if (!shm->buffer)
				goto unlock_mutex;

			s_rawend = 0;

			if (shm->samplebits == 8 && !shm->signed8)
				clear = 0x80;
			else
				clear = 0;

			memset(shm->buffer, clear, shm->samples * shm->samplebits / 8);

			SNDDMA_Submit();

		unlock_mutex:
			SDL_UnlockMutex(mutex);
		}

		void SNDDMA_LockBuffer(void)
		{
			SDL_LockAudio();
		}
	};
	class MSG {
	public:
		Engine* engine;
		MSG(Engine e) {
			engine = &e;
		}
		void WriteByte(sizebuf_t* sb, int c)
		{
			byte* buf;

#ifdef PARANOID
			if (c < 0 || c > 255)
				Sys_Error("MSG_WriteByte: range error");
#endif

			buf = (byte*)sz->GetSpace(sb, 1);
			buf[0] = c;
		}
	};
	class Sys {
	public:
		Engine* engine;

		Sys(Engine e) {
			engine = &e;

		}
		void Init() {
			SetTimerResolution();
			SetDPIAware();

			hinput = GetStdHandle(STD_INPUT_HANDLE);
			houtput = GetStdHandle(STD_OUTPUT_HANDLE);

		}
		static void SetTimerResolution() {
			timeBeginPeriod(1);
		}
		static void SetDPIAware() {
			
			HMODULE					   hUser32, hShcore;
			SetProcessDPIAwarenessFunc setDPIAwareness;
			SetProcessDPIAwareFunc	   setDPIAware;


			hShcore = LoadLibraryA("Shcore.dll");
			hUser32 = LoadLibraryA("user32.dll");
			setDPIAwareness = (SetProcessDPIAwarenessFunc)(hShcore ? GetProcAddress(hShcore, "SetProcessDpiAwareness") : NULL);
			setDPIAware = (SetProcessDPIAwareFunc)(hUser32 ? GetProcAddress(hUser32, "SetProcessDPIAware") : NULL);

			if (setDPIAwareness) /* Windows 8.1+ */
				setDPIAwareness(dpi_monitor_aware);
			else if (setDPIAware) /* Windows Vista-8.0 */
				setDPIAware();

			if (hShcore)
				FreeLibrary(hShcore);
			if (hUser32)
				FreeLibrary(hUser32);
		}
		static void InitSDL() {
			SDL_version v;
			SDL_GetVersion(&v);

			std::cout << "SDL version: " << (int)v.major << "." << (int)v.minor << "." << (int)v.patch << std::endl;

			if (SDL_Init(0) < 0) {
				std::cout << ("SDL_Init failed: %s", SDL_GetError()) << std::endl;
			}

			atexit(AtExit);
		}
		void Error(const char* error, ...) {
			va_list argptr;
			char	text[1024];
			DWORD	dummy;

			host->parms->errstate++;

			va_start(argptr, error);
			qk->vsnprintf(text, sizeof(text), error, argptr);
			va_end(argptr);

			pr->SwitchQCVM(NULL);

			if (this->engine->isDedicated)
				WriteFile(houtput, errortxt1, strlen(errortxt1), &dummy, NULL);
			/* SDL will put these into its own stderr log,
			   so print to stderr even in graphical mode. */
			fputs(errortxt1, stderr);
			fputs(errortxt2, stderr);
			fputs(text, stderr);
			fputs("\n\n", stderr);
			if (!engine->isDedicated)
				ErrorDialog(text);
			else
			{
				WriteFile(houtput, errortxt2, strlen(errortxt2), &dummy, NULL);
				WriteFile(houtput, text, strlen(text), &dummy, NULL);
				WriteFile(houtput, "\r\n", 2, &dummy, NULL);
				SDL_Delay(3000); /* show the console 3 more seconds */
			}

#ifdef _DEBUG
			__debugbreak();
#endif

			exit(1);
		}
		void Printf(const char* fmt, ...) {
			va_list argptr;
			char	text[1024];
			DWORD	dummy;

			va_start(argptr, fmt);
			qk->vsnprintf(text, sizeof(text), fmt, argptr);
			va_end(argptr);

			if (this->engine->isDedicated)
			{
				WriteFile(houtput, text, strlen(text), &dummy, NULL);
			}
			else
			{
				/* SDL will put these into its own stdout log,
				   so print to stdout even in graphical mode. */
				fputs(text, stdout);
				OutputDebugStringA(text);
			}
		}

		void Sys_Quit()
		{
			host->Shutdown();

			if (this->engine->isDedicated)
				FreeConsole();

			exit(0);
		}

		static void AtExit() {
			SDL_Quit();
		}

		double DoubleTime(void)
		{
			return (double)SDL_GetPerformanceCounter() / counter_freq;
		}
	};
	class ED {
	public:
		Engine* engine;
		ED(Engine e) {
			engine = &e;
		}
		void ED_RezoneString(string_t* ref, const char* str)
		{
			char* buf;
			size_t len = strlen(str) + 1;
			size_t id;

			if (*ref)
			{ // if the reference is already a zoned string then free it first.
				id = -1 - *ref;
				if (id < pr->qcvm->knownzonesize && (pr->qcvm->knownzone[id >> 3] & (1u << (id & 7))))
				{ // okay, it was zoned.
					pr->qcvm->knownzone[id >> 3] &= ~(1u << (id & 7));
					buf = (char*)pr->GetString(*ref);
					PR_ClearEngineString(*ref);
					mem->Free(buf);
				}
				//		else
				//			Con_Warning("ED_RezoneString: string wasn't strzoned\n");	//warnings would trigger from the default cvar value that autocvars are
				// initialised with
			}

			buf = (char*)mem->Alloc(len);
			memcpy(buf, str, len);
			id = -1 - (*ref = PR_SetEngineString(buf));
			// make sure its flagged as zoned so we can clean up properly after.
			if (id >= pr->qcvm->knownzonesize)
			{
				int old_size = (pr->qcvm->knownzonesize + 7) >> 3;
				pr->qcvm->knownzonesize = (id + 32) & ~7;
				int new_size = (pr->qcvm->knownzonesize + 7) >> 3;
				pr->qcvm->knownzone = mem->Realloc(pr->qcvm->knownzone, new_size);
				memset(pr->qcvm->knownzone + old_size, 0, new_size - old_size);
			}
			pr->qcvm->knownzone[id >> 3] |= 1u << (id & 7);
		}

		bool ParseEpair(void* base, ddef_t* key, const char* s, bool zoned)
		{
			int			 i;
			char		 string[128];
			ddef_t* def;
			char* v, * w;
			char* end;
			void* d;
			dfunction_t* func;

			d = (void*)((int*)base + key->ofs);

			switch (key->type & ~DEF_SAVEGLOBAL)
			{
			case ev_string:
				if (zoned) // zoned version allows us to change the strings more freely
					ED_RezoneString((string_t*)d, s);
				else
					*(string_t*)d = ED_NewString(s);
				break;

			case ev_float:
				*(float*)d = atof(s);
				break;
			case ev_ext_double:
				*(qcdouble_t*)d = atof(s);
				break;
			case ev_ext_integer:
				*(int32_t*)d = atoi(s);
				break;
			case ev_ext_uint32:
				*(uint32_t*)d = atoi(s);
				break;
			case ev_ext_sint64:
				*(qcsint64_t*)d = strtoll(s, NULL, 0); // if longlong is 128bit then no real harm done for 64bit quantities...
				break;
			case ev_ext_uint64:
				*(qcuint64_t*)d = strtoull(s, NULL, 0);
				break;

			case ev_vector:
				qk->strlcpy(string, s, sizeof(string));
				end = (char*)string + strlen(string);
				v = string;
				w = string;

				for (i = 0; i < 3 && (w <= end); i++) // ericw -- added (w <= end) check
				{
					// set v to the next space (or 0 byte), and change that char to a 0 byte
					while (*v && *v != ' ')
						v++;
					*v = 0;
					((float*)d)[i] = atof(w);
					w = v = v + 1;
				}
				// ericw -- fill remaining elements to 0 in case we hit the end of string
				// before reading 3 floats.
				if (i < 3)
				{
					con->DWarning("Avoided reading garbage for \"%s\" \"%s\"\n", PR_GetString(key->s_name), s);
					for (; i < 3; i++)
						((float*)d)[i] = 0.0f;
				}
				break;

			case ev_entity:
				if (!strncmp(s, "entity ", 7)) // Spike: putentityfieldstring/etc should be able to cope with etos's weirdness.
					s += 7;
				*(int*)d = EDICT_TO_PROG(EDICT_NUM(atoi(s)));
				break;

			case ev_field:
				def = ED_FindField(s);
				if (!def)
				{
					// johnfitz -- HACK -- suppress error becuase fog/sky fields might not be mentioned in defs.qc
					if (strncmp(s, "sky", 3) && strcmp(s, "fog"))
						Con_DPrintf("Can't find field %s\n", s);
					return false;
				}
				*(int*)d = G_INT(def->ofs);
				break;

			case ev_function:
				func = ED_FindFunction(s);
				if (!func)
				{
					con->Printf("Can't find function %s\n", s);
					return false;
				}
				*(func_t*)d = func - pr->qcvm->functions;
				break;

			default:
				break;
			}
			return true;
		}

	};
	class SV {
	public:
		Engine* engine;

		
		bool active; // false if only a net client

		bool paused;
		bool loadgame;	 // handle connections specially
		bool nomonsters; // server started with 'nomonsters' cvar active

		char lastsave[128];

		int	   lastcheck; // used by PF_checkclient
		double lastchecktime;

		qcvm_t qcvm; // Spike: entire qcvm state

		char			 name[64];					 // map name
		char			 modelname[64];				 // maps/<name>.bsp, for model_precache[0]
		const char* model_precache[MAX_MODELS]; // NULL terminated
		struct qmodel_s* models[MAX_MODELS];
		const char* sound_precache[MAX_SOUNDS]; // NULL terminated
		const char* lightstyles[MAX_LIGHTSTYLES];
		server_state_t	 state; // some actions are only valid during load

		sizebuf_t datagram;
		byte	  datagram_buf[MAX_DATAGRAM];

		sizebuf_t reliable_datagram; // copied to all clients at end of frame
		byte	  reliable_datagram_buf[MAX_DATAGRAM];

		sizebuf_t signon;
		byte	  signon_buf[MAX_MSGLEN - 2]; // johnfitz -- was 8192, now uses MAX_MSGLEN

		unsigned protocol; // johnfitz
		unsigned protocolflags;

		sizebuf_t multicast; // selectively copied to clients by the multicast builtin
		byte	  multicast_buf[MAX_DATAGRAM];

		const char* particle_precache[MAX_PARTICLETYPES]; // NULL terminated

		entity_state_t* static_entities;
		int				num_statics;
		int				max_statics;

		struct ambientsound_s
		{
			vec3_t		 origin;
			unsigned int soundindex;
			float		 volume;
			float		 attenuation;
		}  *ambientsounds;
		int num_ambients;
		int max_ambients;

		struct svcustomstat_s
		{
			int		idx;
			int		type;
			int		fld;
			eval_t* ptr;
		} customstats[MAX_CL_STATS * 2]; // strings or numeric...
		size_t numcustomstats;

		int effectsmask; // only enable colored quad/penta dlights in 2021 release
		


		bool active;
		SV(Engine e) {
			engine = &e;
		}
	};
	
	static VID* vid;
	static COM* com;
	static SCR* scr;
	static CL* cl;
	static Con* con;
	static Cmd* cmd;
	static Cvar* cvar;
	static Mem* mem;
	static Host* host;
	static SZ* sz;
	static MSG* msg;
	static Sys* sys;
	static q* qk;
	static Tasks* t;
	static Cbuf* cbuf;
	static PR* pr;
	static ED* ed;
	static SV* sv;
	static Key* key;
	static Net* net;
	static S* s;

	Engine(int argc, char* argv[]) {
		
		sys = new Sys(*this);
		sz = new SZ(*this);

		
		sys->InitSDL();
		sys->Init();

		sys->Printf("Detected %d CPUs.\n", SDL_GetCPUCount());
		sys->Printf("Initializing %s.\n", ENGINE_NAME_AND_VER);

		host = new Host(*this,argc,argv);


		scr = new SCR(*this);
		cl = new CL(*this);
		con = new Con(*this);
		cmd = new Cmd(*this);
		cvar = new Cvar(*this);
		vid = new VID(*this);
		msg = new MSG(*this);
		qk = new q(*this);
		pr = new PR(*this);
		ed = new ED(*this);
		sv = new SV(*this);
		key = new Key(*this);
		net = new Net(*this);
		s = new S(*this);
	}
};

// needs to be here, otherwise compiler will bitch
Engine::VID* Engine::vid = nullptr;
Engine::COM* Engine::com = nullptr;
Engine::SCR* Engine::scr = nullptr;
Engine::CL* Engine::cl = nullptr;
Engine::Con* Engine::con = nullptr;
Engine::Cmd* Engine::cmd = nullptr;
Engine::Cvar* Engine::cvar = nullptr;
Engine::Mem* Engine::mem = nullptr;
Engine::Host* Engine::host = nullptr;
Engine::SZ* Engine::sz = nullptr;
Engine::MSG* Engine::msg = nullptr;
Engine::Sys* Engine::sys = nullptr;
Engine::q* Engine::qk = nullptr;
Engine::Tasks* Engine::t = nullptr;
Engine::Cbuf* Engine::cbuf = nullptr;
Engine::PR* Engine::pr = nullptr;
Engine::ED* Engine::ed = nullptr;
Engine::SV* Engine::sv = nullptr;

int main(int argc, char *argv[]) {

	
	auto bla = new Engine(argc,argv);




	return 0;
}