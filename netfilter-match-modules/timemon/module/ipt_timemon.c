/*  timemon --	An iptables extension for time monitoring/control
 *  			Can be used to efficiently monitor time usage and/or implement time quotas
 *  			Can be queried using the ipttmctl userspace library
 *  			Originally designed for use with Gargoyle router firmware (gargoyle-router.com)
 *
 *  Heavily lifted from the ipt_bandwidth extension by Eric Bishop. Most of
 *  the credit belongs with him.
 *  Copyright © 2009 by Eric Bishop <eric@gargoyle-router.com>
 *  Copyright © 2017 by Michael Gray <michael.gray@lantisproject.com>
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>

#include <linux/time.h>

#include <linux/semaphore.h> 


#include "timemon_deps/tree_map.h"
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_timemon.h>


#include <linux/ip.h>
#include <linux/netfilter/x_tables.h>


/* #define TIMEMON_DEBUG 1 */


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Gray");
MODULE_DESCRIPTION("Match time used, designed for use with Gargoyle web interface (www.gargoyle-router.com)");

/* 
 * WARNING: accessing the sys_tz variable takes FOREVER, and kills performance 
 * keep a local variable that gets updated from the extern variable 
 */
extern struct timezone sys_tz; 
static int local_minutes_west;
static int local_seconds_west;
static time_t last_local_mw_update;


static spinlock_t timemon_lock = __SPIN_LOCK_UNLOCKED(timemon_lock);
DEFINE_SEMAPHORE(userspace_lock);

static string_map* id_map = NULL;


typedef struct info_and_maps_struct
{
	struct ipt_timemon_info* info;
	long_map* ip_map;
	long_map* ip_history_map;
}info_and_maps;

typedef struct history_struct
{
	time_t first_start;
	time_t first_end;
	time_t last_end; /* also beginning of current time frame */
	uint32_t max_nodes;
	uint32_t num_nodes;
	uint32_t non_zero_nodes;
	uint32_t current_index;
	uint64_t* history_data;
} tm_history;



static unsigned char set_in_progress = 0;
static char set_id[TIMEMON_MAX_ID_LENGTH] = "";

/* 
 * function prototypes
 *
 * (prototypes only provided for 
 * functions not part of iptables API)
 *
*/


static void adjust_ip_for_backwards_time_shift(unsigned long key, void* value);
static void adjust_id_for_backwards_time_shift(char* key, void* value);
static void check_for_backwards_time_shift(time_t now);


static void shift_timezone_of_ip(unsigned long key, void* value);
static void shift_timezone_of_id(char* key, void* value);
static void check_for_timezone_shift(time_t now, int already_locked);



static tm_history* initialize_history(uint32_t max_nodes);
static unsigned char update_history(tm_history* history, time_t interval_start, time_t interval_end, struct ipt_timemon_info* info);



static void do_reset(unsigned long key, void* value);
static void set_timemon_to_zero(unsigned long key, void* value);
static void handle_interval_reset(info_and_maps* iam, time_t now);

static uint64_t pow64(uint64_t base, uint64_t pow);
static uint64_t get_tm_record_max(void); /* called by init to set global variable */

static inline int is_leap(unsigned int y);
static time_t get_next_reset_time(struct ipt_timemon_info *info, time_t now, time_t previous_reset);
static time_t get_nominal_previous_reset_time(struct ipt_timemon_info *info, time_t current_next_reset);

static uint64_t* initialize_map_entries_for_ip(info_and_maps* iam, unsigned long ip, uint64_t initial_timemon);




static time_t backwards_check = 0;
static time_t backwards_adjust_current_time = 0;
static time_t backwards_adjust_info_previous_reset = 0;
static time_t backwards_adjust_ips_zeroed = 0;
static info_and_maps* backwards_adjust_iam = NULL;

/*
static char print_out_buf[25000];
static void print_to_buf(char* outdat);
static void reset_buf(void);
static void do_print_buf(void);

static void print_to_buf(char* outdat)
{
	int buf_len = strlen(print_out_buf);
	sprintf(print_out_buf+buf_len, "\t%s\n", outdat);
}
static void reset_buf(void)
{
	print_out_buf[0] = '\n';
	print_out_buf[1] = '\0';
}
static void do_print_buf(void)
{
	char* start = print_out_buf;
	char* next = strchr(start, '\n');
	while(next != NULL)
	{
		*next = '\0';
		printk("%s\n", start);
		start = next+1;
		next = strchr(start, '\n');
	}
	printk("%s\n", start);
	
	reset_buf();
}
*/

static void adjust_ip_for_backwards_time_shift(unsigned long key, void* value)
{
	tm_history* old_history = (tm_history*)value;
	
	if(old_history->num_nodes == 1)
	{
		if(backwards_adjust_info_previous_reset > backwards_adjust_current_time)
		{
			if(backwards_adjust_ips_zeroed == 0)
			{
				apply_to_every_long_map_value(backwards_adjust_iam->ip_map, set_timemon_to_zero);
				backwards_adjust_iam->info->next_reset = get_next_reset_time(backwards_adjust_iam->info, backwards_adjust_current_time, backwards_adjust_current_time);
				backwards_adjust_iam->info->previous_reset = backwards_adjust_current_time;
				backwards_adjust_iam->info->current_timeusage = 0;
				backwards_adjust_ips_zeroed = 1;
			}
		}
		return;
	}
	else if(old_history->last_end < backwards_adjust_current_time)
	{
		return;
	}
	else
	{
		
		/* 
		 * reconstruct new history without newest nodes, to represent data as it was 
		 * last time the current time was set to the interval to which we just jumped back
		 */
		uint32_t next_old_index;
		time_t old_next_start =  old_history->first_start == 0 ? backwards_adjust_info_previous_reset : old_history->first_start; /* first time point in old history */
		tm_history* new_history = initialize_history(old_history->max_nodes);
		if(new_history == NULL)
		{
			printk("ipt_timemon: warning, kmalloc failure!\n");
			return;
		}

		

		/* oldest index in old history -- we iterate forward through old history using this index */
		next_old_index = old_history->num_nodes == old_history->max_nodes ? (old_history->current_index+1) % old_history->max_nodes : 0;


		/* if first time point is after current time, just completely re-initialize history, otherwise set first time point to old first time point */
		(new_history->history_data)[ new_history->current_index ] = old_next_start < backwards_adjust_current_time ? (old_history->history_data)[next_old_index] : 0;
		backwards_adjust_iam->info->previous_reset                = old_next_start < backwards_adjust_current_time ? old_next_start : backwards_adjust_current_time;


		/* iterate through old history, rebuilding in new history*/
		while( old_next_start < backwards_adjust_current_time )
		{
			time_t old_next_end = get_next_reset_time(backwards_adjust_iam->info, old_next_start, old_next_start); /* 2nd param = last reset, 3rd param = current time */
			if(  old_next_end < backwards_adjust_current_time)
			{
				update_history(new_history, old_next_start, old_next_end, backwards_adjust_iam->info);
				next_old_index++;
				(new_history->history_data)[ new_history->current_index ] =  (old_history->history_data)[next_old_index];
			}
			backwards_adjust_iam->info->previous_reset = old_next_start; /*update previous_reset variable in tm_info as we iterate */
			old_next_start = old_next_end;
		}

		/* update next_reset variable from previous_reset variable which we've already set */
		backwards_adjust_iam->info->next_reset = get_next_reset_time(backwards_adjust_iam->info, backwards_adjust_iam->info->previous_reset, backwards_adjust_iam->info->previous_reset); 
		


		/* set old_history to be new_history */	
		kfree(old_history->history_data);
		old_history->history_data   = new_history->history_data;
		old_history->first_start    = new_history->first_start;
		old_history->first_end      = new_history->first_end;
		old_history->last_end       = new_history->last_end;
		old_history->num_nodes      = new_history->num_nodes;
		old_history->non_zero_nodes = new_history->non_zero_nodes;
		old_history->current_index  = new_history->current_index;
		set_long_map_element(backwards_adjust_iam->ip_map, key, (void*)(old_history->history_data + old_history->current_index) );
		if(key == 0)
		{
			backwards_adjust_iam->info->combined_tm = (uint64_t*)(old_history->history_data + old_history->current_index);
		}
		backwards_adjust_iam->info->last_record_time = backwards_adjust_current_time;
		
		/* 
		 * free new history  (which was just temporary) 
		 * note that we don't need to free history_data from new_history
		 * we freed the history_data from old history, and set that to the history_data from new_history
		 * so, this cleanup has already been handled
		 */
		kfree(new_history);
		
	}
}
static void adjust_id_for_backwards_time_shift(char* key, void* value)
{
	info_and_maps* iam = (info_and_maps*)value;
	if(iam == NULL)
	{
		return;
	}
	if(iam->info == NULL)
	{
		return;
	}

	backwards_adjust_iam = iam;
	if( (iam->info->reset_is_constant_interval == 0 && iam->info->reset_interval == TIMEMON_NEVER) || iam->info->cmp == TIMEMON_CHECK )
	{
		return;
	}
	if(iam->ip_history_map != NULL)
	{
		backwards_adjust_info_previous_reset = iam->info->previous_reset;
		backwards_adjust_ips_zeroed = 0;
		apply_to_every_long_map_value(iam->ip_history_map, adjust_ip_for_backwards_time_shift);
	}
	else
	{
		time_t next_reset_after_adjustment = get_next_reset_time(iam->info, backwards_adjust_current_time, backwards_adjust_current_time);
		if(next_reset_after_adjustment < iam->info->next_reset)
		{
			iam->info->previous_reset = backwards_adjust_current_time;
			iam->info->next_reset = next_reset_after_adjustment;
			iam->info->last_record_time = backwards_adjust_current_time;
		}
	}
	backwards_adjust_iam = NULL;
}
static void check_for_backwards_time_shift(time_t now)
{
	spin_lock_bh(&timemon_lock);
	if(now < backwards_check && backwards_check != 0)
	{
		printk("ipt_timemon: backwards time shift detected, adjusting\n");

		/* adjust */
		down(&userspace_lock);

		/* This function is always called with absolute time, not time adjusted for timezone. Correct that before adjusting. */
		backwards_adjust_current_time = now - local_seconds_west;
		apply_to_every_string_map_value(id_map, adjust_id_for_backwards_time_shift);
		up(&userspace_lock);
	}
	backwards_check = now;
	spin_unlock_bh(&timemon_lock);
}



static int old_minutes_west;
static time_t shift_timezone_current_time;
static time_t shift_timezone_info_previous_reset;
static info_and_maps* shift_timezone_iam = NULL;
static void shift_timezone_of_ip(unsigned long key, void* value)
{
	#ifdef TIMEMON_DEBUG
		unsigned long* ip = &key;
		printk("shifting ip = %u.%u.%u.%u\n", *((char*)ip), *(((char*)ip)+1), *(((char*)ip)+2), *(((char*)ip)+3) );
	#endif


	tm_history* history = (tm_history*)value;
	int32_t timezone_adj = (old_minutes_west-local_minutes_west)*60;
	#ifdef TIMEMON_DEBUG
		printk("  before jump:\n");
		printk("    current time = %ld\n",  shift_timezone_current_time);
		printk("    first_start  = %ld\n", history->first_start);
		printk("    first_end    = %ld\n", history->first_end);
		printk("    last_end     = %ld\n", history->last_end);
		printk("\n");
	#endif
	
	/* given time after shift, calculate next and previous reset times */
	time_t next_reset = get_next_reset_time(shift_timezone_iam->info, shift_timezone_current_time, 0);
	time_t previous_reset = get_nominal_previous_reset_time(shift_timezone_iam->info, next_reset);
	shift_timezone_iam->info->next_reset = next_reset;

	/*if we're resetting on a constant interval, we can just adjust -- no need to worry about relationship to constant boundaries, e.g. end of day */
	if(shift_timezone_iam->info->reset_is_constant_interval)
	{
		shift_timezone_iam->info->previous_reset = previous_reset;
		if(history->num_nodes > 1)
		{
			history->first_start = history->first_start + timezone_adj;
			history->first_end = history->first_end + timezone_adj;
			history->last_end = history->last_end + timezone_adj;
		}
	}
	else
	{

		
		/* next reset will be the newly computed next_reset. */
		int node_index=history->num_nodes - 1;
		if(node_index > 0)
		{
			/* based on new, shifted time, iterate back over all nodes in history */
			shift_timezone_iam->info->previous_reset = previous_reset ;
			history->last_end = previous_reset;

			while(node_index > 1)
			{
				previous_reset = get_nominal_previous_reset_time(shift_timezone_iam->info, previous_reset);
				node_index--;
			}
			history->first_end = previous_reset;
			
			previous_reset = get_nominal_previous_reset_time(shift_timezone_iam->info, previous_reset);
			history->first_start = previous_reset > history->first_start + timezone_adj ? previous_reset : history->first_start + timezone_adj;
		}
		else
		{
			/*
			 * history hasn't really been initialized -- there's only one, current time point.
			 * we only know what's in the current accumulator in info. Just adjust previous reset time and make sure it's valid 
			 */
			shift_timezone_iam->info->previous_reset = previous_reset > shift_timezone_info_previous_reset + timezone_adj ? previous_reset : shift_timezone_info_previous_reset + timezone_adj;
		}
	}
	shift_timezone_iam->info->previous_reset = shift_timezone_current_time;

	#ifdef TIMEMON_DEBUG
		printk("\n");
		printk("  after jump:\n");
		printk("    first_start = %ld\n", history->first_start);
		printk("    first_end   = %ld\n", history->first_end);
		printk("    last_end    = %ld\n", history->last_end);
		printk("\n\n");
	#endif

}
static void shift_timezone_of_id(char* key, void* value)
{
	info_and_maps* iam = (info_and_maps*)value;
	int history_found = 0;
	if(iam == NULL)
	{
		return;
	}
	if(iam->info == NULL)
	{
		return;
	}
	
	#ifdef TIMEMON_DEBUG
		printk("shifting id %s\n", key);
	#endif	

	shift_timezone_iam = iam;
	if( (iam->info->reset_is_constant_interval == 0 && iam->info->reset_interval == TIMEMON_NEVER) || iam->info->cmp == TIMEMON_CHECK )
	{
		return;
	}

	if(iam->ip_history_map != NULL)
	{
		if(iam->ip_history_map->num_elements > 0)
		{
			history_found = 1;
			shift_timezone_info_previous_reset = iam->info->previous_reset;
			apply_to_every_long_map_value(iam->ip_history_map, shift_timezone_of_ip);
		}
	}
	if(history_found == 0)
	{
		iam->info->previous_reset = iam->info->previous_reset + ((old_minutes_west - local_minutes_west )*60);
		if(iam->info->previous_reset > shift_timezone_current_time)
		{
			iam->info->next_reset = get_next_reset_time(iam->info, shift_timezone_current_time, shift_timezone_current_time);
			iam->info->previous_reset = shift_timezone_current_time;
		}
		else
		{
			iam->info->next_reset = get_next_reset_time(iam->info, shift_timezone_current_time, iam->info->previous_reset);
			while (iam->info->next_reset < shift_timezone_current_time)
			{
				iam->info->previous_reset = iam->info->next_reset;
				iam->info->next_reset = get_next_reset_time(iam->info, iam->info->previous_reset, iam->info->previous_reset);
			}
		}
	}
	shift_timezone_iam = NULL;
	iam->info->previous_reset = shift_timezone_current_time;
}

static void check_for_timezone_shift(time_t now, int already_locked)
{
	
	if(already_locked == 0) { spin_lock_bh(&timemon_lock); }
	if(now != last_local_mw_update ) /* make sure nothing changed while waiting for lock */
	{
		local_minutes_west = sys_tz.tz_minuteswest;
		local_seconds_west = 60*local_minutes_west;
		last_local_mw_update = now;
		if(local_seconds_west > last_local_mw_update)
		{
			/* we can't let adjusted time be < 0 -- pretend timezone is still UTC */
			local_minutes_west = 0;
			local_seconds_west = 0;
		}

		if(local_minutes_west != old_minutes_west)
		{
			int adj_minutes = old_minutes_west-local_minutes_west;
			adj_minutes = adj_minutes < 0 ? adj_minutes*-1 : adj_minutes;	
			
			if(already_locked == 0) { down(&userspace_lock); }

			printk("ipt_timemon: timezone shift of %d minutes detected, adjusting\n", adj_minutes);
			printk("               old minutes west=%d, new minutes west=%d\n", old_minutes_west, local_minutes_west);
			
			/* this function is always called with absolute time, not time adjusted for timezone.  Correct that before adjusting */
			shift_timezone_current_time = now - local_seconds_west;
			apply_to_every_string_map_value(id_map, shift_timezone_of_id);

			old_minutes_west = local_minutes_west;


			if(already_locked == 0) { up(&userspace_lock); }
		}
	}
	if(already_locked == 0) { spin_unlock_bh(&timemon_lock); }
}



static tm_history* initialize_history(uint32_t max_nodes)
{
	tm_history* new_history = (tm_history*)kmalloc(sizeof(tm_history), GFP_ATOMIC);
	if(new_history != NULL)
	{
		new_history->history_data = (uint64_t*)kmalloc((1+max_nodes)*sizeof(uint64_t), GFP_ATOMIC); /*number to save +1 for current */
		if(new_history->history_data == NULL) /* deal with malloc failure */
		{
			kfree(new_history);
			new_history = NULL;
		}
		else
		{
			new_history->first_start = 0;
			new_history->first_end = 0;
			new_history->last_end = 0;
			new_history->max_nodes = max_nodes+1; /*number to save +1 for current */
			new_history->num_nodes = 1;
			new_history->non_zero_nodes = 0; /* counts non_zero nodes other than current, so initialize to 0 */
			new_history->current_index = 0;
			memset(new_history->history_data, 0, max_nodes*sizeof(uint64_t));
		}
	}
	return new_history; /* in case of malloc failure new_history will be NULL, this should be safe */
}

/* returns 1 if there are non-zero nodes in history, 0 if history is empty (all zero) */
static unsigned char update_history(tm_history* history, time_t interval_start, time_t interval_end, struct ipt_timemon_info* info)
{
	unsigned char history_is_nonzero = 0;
	if(history != NULL) /* should never be null, but let's be sure */
	{

		/* adjust number of non-zero nodes */
		if(history->num_nodes == history->max_nodes)
		{
			uint32_t first_index =  (history->current_index+1) % history->max_nodes; 
			if( (history->history_data)[first_index] > 0)
			{
				history->non_zero_nodes = history->non_zero_nodes -1;
			}
		}
		if( (history->history_data)[history->current_index] > 0 ) 
		{
			history->non_zero_nodes = history->non_zero_nodes + 1;
		}
		history_is_nonzero = history->non_zero_nodes > 0 ? 1 : 0;


		/* update interval start/end */
		if(history->first_start == 0)
		{
			history->first_start = interval_start;
			history->first_end = interval_end;
		}
		if(history->num_nodes >= history->max_nodes)
		{
			history->first_start = history->first_end;
			history->first_end = get_next_reset_time(info, history->first_start, history->first_start);
		}
		history->last_end = interval_end;


		history->num_nodes = history->num_nodes < history->max_nodes ? history->num_nodes+1 : history->max_nodes;
		history->current_index = (history->current_index+1) % history->max_nodes;
		(history->history_data)[history->current_index] = 0;
		
		#ifdef TIMEMON_DEBUG
			printk("after update history->num_nodes = %d\n", history->num_nodes);
			printk("after update history->current_index = %d\n", history->current_index);
		#endif	
	}
	return history_is_nonzero;
}


static struct ipt_timemon_info* do_reset_info = NULL;
static long_map* do_reset_ip_map = NULL;
static long_map* do_reset_delete_ips = NULL;
static time_t do_reset_interval_start = 0;
static time_t do_reset_interval_end = 0;
static void do_reset(unsigned long key, void* value)
{
	tm_history* history = (tm_history*)value;
	if(history != NULL && do_reset_info != NULL) /* should never be null.. but let's be sure */
	{
		unsigned char history_contains_data = update_history(history, do_reset_interval_start, do_reset_interval_end, do_reset_info);
		if(history_contains_data == 0 || do_reset_ip_map == NULL)
		{
			//schedule data for ip to be deleted (can't delete history while we're traversing history tree data structure!)
			if(do_reset_delete_ips != NULL) /* should never be null.. but let's be sure */
			{
				set_long_map_element(do_reset_delete_ips, key, (void*)(history->history_data + history->current_index));
			}
		}
		else
		{
			set_long_map_element(do_reset_ip_map, key, (void*)(history->history_data + history->current_index) );
		}
	}
}

long_map* clear_ip_map = NULL;
long_map* clear_ip_history_map = NULL;
static void clear_ips(unsigned long key, void* value)
{
	if(clear_ip_history_map != NULL && clear_ip_map != NULL)
	{
		tm_history* history;
		
		#ifdef TIMEMON_DEBUG
			unsigned long* ip = &key;
			printk("clearing ip = %u.%u.%u.%u\n", *((char*)ip), *(((char*)ip)+1), *(((char*)ip)+2), *(((char*)ip)+3) );
		#endif

		remove_long_map_element(clear_ip_map, key);
		history = (tm_history*)remove_long_map_element(clear_ip_history_map, key);
		if(history != NULL)
		{
			kfree(history->history_data);
			kfree(history);
		}
	}
}

static void set_timemon_to_zero(unsigned long key, void* value)
{
	*((uint64_t*)value) = 0;
}


long_map* reset_histories_ip_map = NULL;
static void reset_histories(unsigned long key, void* value)
{
	tm_history* bh = (tm_history*)value;
	bh->first_start = 0;
	bh->first_end = 0;
	bh->last_end = 0; 
	bh->num_nodes = 1;
	bh->non_zero_nodes = 1;
	bh->current_index = 0;
	(bh->history_data)[0] = 0;
	if(reset_histories_ip_map != NULL)
	{
		set_long_map_element(reset_histories_ip_map, key, bh->history_data);
	}
}


static void handle_interval_reset(info_and_maps* iam, time_t now)
{
	struct ipt_timemon_info* info;

	#ifdef TIMEMON_DEBUG
		printk("now, handling interval reset\n");
	#endif
	if(iam == NULL)
	{
		#ifdef TIMEMON_DEBUG
			printk("error: doing reset, iam is null \n");
		#endif
		return;
	}
	if(iam->ip_map == NULL)
	{
		#ifdef TIMEMON_DEBUG
			printk("error: doing reset, ip_map is null\n");
		#endif
		return;
	}
	if(iam->info == NULL)
	{
		#ifdef TIMEMON_DEBUG
			printk("error: doing reset, info is null\n");
		#endif

		return;
	}

	info = iam->info;
	#ifdef TIMEMON_DEBUG
		printk("doing reset for case where no intervals are saved\n");
	#endif

	if(info->next_reset <= now)
	{
		info->next_reset = get_next_reset_time(info, info->previous_reset, info->previous_reset);
		if(info->next_reset <= now)
		{
			info->next_reset = get_next_reset_time(info, now, info->previous_reset);
		}
	}
	apply_to_every_long_map_value(iam->ip_map, set_timemon_to_zero);
	info->combined_tm = (uint64_t*)get_long_map_element(iam->ip_map, 0);
	info->current_timeusage = 0;
	info->last_record_time = 0;
}

/* 
 * set max timeusage to be max possible using 63 of the
 * 64 bits in our record.  In some systems uint64_t is treated
 * like signed, so to prevent errors, use only 63 bits
 */
static uint64_t pow64(uint64_t base, uint64_t pow)
{
	uint64_t val = 1;
	if(pow > 0)
	{
		val = base*pow64(base, pow-1);
	}
	return val;
}
static uint64_t get_tm_record_max(void) /* called by init to set global variable */
{
	return  (pow64(2,62)) + (pow64(2,62)-1);
}
static uint64_t timemon_record_max;


#define ADD_UP_TO_MAX(original,add,is_check) (timemon_record_max - original > add && is_check== 0) ? original+add : (is_check ? original : timemon_record_max);


/*
 * Shamelessly yoinked from xt_time.c
 * "That is so amazingly amazing, I think I'd like to steal it." 
 *      -- Zaphod Beeblebrox
 */

static const u_int16_t days_since_year[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
};

static const u_int16_t days_since_leapyear[] = {
	0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335,
};

/*
 * Since time progresses forward, it is best to organize this array in reverse,
 * to minimize lookup time.  These are days since epoch since start of each year,
 * going back to 1970
 */
#define DSE_FIRST 2039
static const u_int16_t days_since_epoch_for_each_year_start[] = {
	/* 2039 - 2030 */
	25202, 24837, 24472, 24106, 23741, 23376, 23011, 22645, 22280, 21915,
	/* 2029 - 2020 */
	21550, 21184, 20819, 20454, 20089, 19723, 19358, 18993, 18628, 18262,
	/* 2019 - 2010 */
	17897, 17532, 17167, 16801, 16436, 16071, 15706, 15340, 14975, 14610,
	/* 2009 - 2000 */
	14245, 13879, 13514, 13149, 12784, 12418, 12053, 11688, 11323, 10957,
	/* 1999 - 1990 */
	10592, 10227, 9862, 9496, 9131, 8766, 8401, 8035, 7670, 7305,
	/* 1989 - 1980 */
	6940, 6574, 6209, 5844, 5479, 5113, 4748, 4383, 4018, 3652,
	/* 1979 - 1970 */
	3287, 2922, 2557, 2191, 1826, 1461, 1096, 730, 365, 0,
};

static inline int is_leap(unsigned int y)
{
	return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

/* end of code  yoinked from xt_time */


static time_t get_nominal_previous_reset_time(struct ipt_timemon_info *info, time_t current_next_reset)
{
	time_t previous_reset = current_next_reset;
	if(info->reset_is_constant_interval == 0)
	{
		/* skip backwards in halves of interval after next, until  */
		time_t next = get_next_reset_time(info, current_next_reset, 0);
		time_t half_interval = (next-current_next_reset)/2;
		time_t half_count, tmp;
		half_interval = half_interval == 0 ? 1 : half_interval; /* must be at least one second, otherwise we loop forever*/
	
		half_count = 1;
		tmp = get_next_reset_time(info, (current_next_reset-(half_count*half_interval)),0);
		while(previous_reset >= current_next_reset)
		{
			previous_reset = tmp;
			half_count++;
			tmp = get_next_reset_time(info, (current_next_reset-(half_count*half_interval)),0);
		}
	}
	else
	{
		previous_reset = current_next_reset - info->reset_interval;
	}
	return previous_reset;
}


static time_t get_next_reset_time(struct ipt_timemon_info *info, time_t now, time_t previous_reset)
{
	//first calculate when next reset would be if reset_time is 0 (which it may be)
	time_t next_reset = 0;
	if(info->reset_is_constant_interval == 0)
	{
		if(info->reset_interval == TIMEMON_MINUTE)
		{
			next_reset = ( (long)(now/60) + 1)*60;
			if(info->reset_time > 0)
			{
				time_t alt_reset = next_reset + info->reset_time - 60;
				next_reset = alt_reset > now ? alt_reset : next_reset+info->reset_time;
			}
		}
		else if(info->reset_interval == TIMEMON_HOUR)
		{
			next_reset = ( (long)(now/(60*60)) + 1)*60*60;
			if(info->reset_time > 0)
			{
				time_t alt_reset = next_reset + info->reset_time - (60*60);
				next_reset = alt_reset > now ? alt_reset : next_reset+info->reset_time;
			}
		}
		else if(info->reset_interval == TIMEMON_DAY)
		{
			next_reset = ( (long)(now/(60*60*24)) + 1)*60*60*24;
			if(info->reset_time > 0)
			{
				time_t alt_reset = next_reset + info->reset_time - (60*60*24);
				next_reset = alt_reset > now ? alt_reset : next_reset+info->reset_time;
			}
		}	
		else if(info->reset_interval == TIMEMON_WEEK)
		{
			long days_since_epoch = now/(60*60*24);
			long current_weekday = (4 + days_since_epoch ) % 7 ;
			next_reset = (days_since_epoch + (7-current_weekday) )*(60*60*24);
			if(info->reset_time > 0)
			{
				time_t alt_reset = next_reset + info->reset_time - (60*60*24*7);
				next_reset = alt_reset > now ? alt_reset : next_reset+info->reset_time;
			}
		}
		else if(info->reset_interval == TIMEMON_MONTH)
		{
			/* yeah, most of this is yoinked from xt_time too */
			int year;
			int year_index;
			int year_day;
			int month;
			long days_since_epoch = now/(60*60*24);
			uint16_t* month_start_days;	
			time_t alt_reset;

			for (year_index = 0, year = DSE_FIRST; days_since_epoch_for_each_year_start[year_index] > days_since_epoch; year_index++)
			{
				year--;
			}
			year_day = days_since_epoch - days_since_epoch_for_each_year_start[year_index];
			if (is_leap(year)) 
			{
				month_start_days = (u_int16_t*)days_since_leapyear;
			}
			else
			{
				month_start_days = (u_int16_t*)days_since_year;
			}
			for (month = 11 ; month > 0 && month_start_days[month] > year_day; month--){}
			
			/* end majority of yoinkage */
			
			alt_reset = (days_since_epoch_for_each_year_start[year_index] + month_start_days[month])*(60*60*24) + info->reset_time;
			if(alt_reset > now)
			{
				next_reset = alt_reset;
			}
			else if(month == 11)
			{
				next_reset = days_since_epoch_for_each_year_start[year_index-1]*(60*60*24) + info->reset_time;
			}
			else
			{
				next_reset = (days_since_epoch_for_each_year_start[year_index] + month_start_days[month+1])*(60*60*24) + info->reset_time;
			}
		}
	}
	else
	{
		if(info->reset_time > 0 && previous_reset > 0 && previous_reset <= now)
		{
			unsigned long adj_reset_time = info->reset_time;
			unsigned long tz_secs = 60 * local_minutes_west;
			if(adj_reset_time < tz_secs)
			{
				unsigned long interval_multiple = 1+(tz_secs/info->reset_interval);
				adj_reset_time = adj_reset_time + (interval_multiple*info->reset_interval);
			}
			adj_reset_time = adj_reset_time - tz_secs;
			
			if(info->reset_time > now)
			{
				unsigned long whole_intervals = ((info->reset_time - now)/info->reset_interval) + 1; /* add one to make sure integer gets rounded UP (since we're subtracting) */
				next_reset = info->reset_time - (whole_intervals*info->reset_interval);
				while(next_reset <= now)
				{
					next_reset = next_reset + info->reset_interval;
				}
				
			}
			else /* info->reset_time <= now */
			{
				unsigned long whole_intervals = (now-info->reset_time)/info->reset_interval; /* integer gets rounded down */
				next_reset = info->reset_time + (whole_intervals*info->reset_interval);
				while(next_reset <= now)
				{
					next_reset = next_reset + info->reset_interval;
				}
			}
		}
		else if(previous_reset > 0)
		{
			next_reset = previous_reset;
			if(next_reset <= now) /* check just to be sure, if this is not true VERY BAD THINGS will happen */
			{
				unsigned long  whole_intervals = (now-next_reset)/info->reset_interval; /* integer gets rounded down */
				next_reset = next_reset + (whole_intervals*info->reset_interval);
				while(next_reset <= now)
				{
					next_reset = next_reset + info->reset_interval;
				}
			}
		}
		else
		{
			next_reset = now + info->reset_interval;
		}
	}
	
	return next_reset;
}



static uint64_t* initialize_map_entries_for_ip(info_and_maps* iam, unsigned long ip, uint64_t initial_timeusage)
{
	#ifdef TIMEMON_DEBUG
		printk("initializing entry for ip, time=%lld\n", initial_timeusage);
	#endif
	
	#ifdef TIMEMON_DEBUG
		if(iam == NULL){ printk("error in initialization: iam is null!\n"); }
	#endif


	uint64_t* new_tm = NULL;
	if(iam != NULL) /* should never happen, but let's be certain */
	{
		struct ipt_timemon_info *info = iam->info;
		long_map* ip_map = iam->ip_map;
		long_map* ip_history_map = iam->ip_history_map;

		#ifdef TIMEMON_DEBUG
			if(info == NULL){ printk("error in initialization: info is null!\n"); }
			if(ip_map == NULL){ printk("error in initialization: ip_map is null!\n"); }
		#endif


		if(info != NULL && ip_map != NULL) /* again... should never happen but let's be sure */
		{
			if(ip_history_map == NULL)
			{
				#ifdef TIMEMON_DEBUG
					printk("  initializing entry for ip without history\n");
				#endif
				new_tm = (uint64_t*)kmalloc(sizeof(uint64_t), GFP_ATOMIC);
			}
			else
			{
				#ifdef TIMEMON_DEBUG
					printk("  initializing entry for ip with history\n");
				#endif

				tm_history *new_history = initialize_history(0);
				if(new_history != NULL) /* check for kmalloc failure */
				{
					tm_history* old_history;
					#ifdef TIMEMON_DEBUG
						printk("  malloc succeeded, new history is non-null\n");
					#endif

					new_tm = (uint64_t*)(new_history->history_data + new_history->current_index);
					old_history = set_long_map_element(ip_history_map, ip, (void*)new_history);
					if(old_history != NULL)
					{
						#ifdef TIMEMON_DEBUG
							printk("  after initialization old_history not null!  (something is FUBAR)\n");
						#endif
						kfree(old_history->history_data);
						kfree(old_history);
					}

					#ifdef TIMEMON_DEBUG
						
					#endif
				}
			}
			if(new_tm != NULL) /* check for kmalloc failure */
			{
				uint64_t* old_tm;
				*new_tm = initial_timeusage;
			       	old_tm = set_long_map_element(ip_map, ip, (void*)new_tm );
				
				/* only free old_tm if num_intervals_to_save is zero -- otherwise it already got freed above when we wiped the old history */
				if(old_tm != NULL)
				{
					free(old_tm);
				}

				if(ip == 0)
				{
					info->combined_tm = new_tm;
				}

				#ifdef TIMEMON_DEBUG
					uint64_t *test = (uint64_t*)get_long_map_element(ip_map, ip);
					if(test == NULL)
					{
						printk("  after initialization tm is null!\n");
					}
					else
					{
						printk("  after initialization tm is %lld\n", *new_tm);
						printk("  after initialization test is %lld\n", *test);
					}
				#endif
			}
		}
	}

	return new_tm;
}


static bool match(const struct sk_buff *skb, struct xt_action_param *par)
{

	struct ipt_timemon_info *info = ((const struct ipt_timemon_info*)(par->matchinfo))->non_const_self;
	
	time_t now;
	int match_found;


	unsigned char is_check = info->cmp == TIMEMON_CHECK ? 1 : 0;
	unsigned char do_src_dst_swap = 0;
	info_and_maps* iam = NULL;
	long_map* ip_map = NULL;
	
	uint64_t* tms[2] = {NULL, NULL};

	/* if we're currently setting this id, ignore new data until set is complete */
	if(set_in_progress == 1)
	{
		if(strcmp(info->id, set_id) == 0)
		{
			return 0;
		}
	}
	

	

	/* 
	 * BEFORE we lock, check for timezone shift 
	 * this will almost always be be very,very quick,
	 * but in the event there IS a shift this
	 * function will lock both kernel update spinlock 
	 * and userspace i/o semaphore,  and do a lot of 
	 * number crunching so we shouldn't 
	 * already be locked.
	 */
	now = get_seconds();
	

	if(now != last_local_mw_update )
	{
		check_for_timezone_shift(now, 0);
		check_for_backwards_time_shift(now);
	}
	now = now -  local_seconds_west;  /* Adjust for local timezone */

	spin_lock_bh(&timemon_lock);
	
	if(is_check)
	{
		info_and_maps* check_iam;
		do_src_dst_swap = info->check_type == TIMEMON_CHECK_SWAP ? 1 : 0;
		check_iam = (info_and_maps*)get_string_map_element_with_hashed_key(id_map, info->hashed_id);
		if(check_iam == NULL)
		{
			spin_unlock_bh(&timemon_lock);
			return 0;
		}
		info = check_iam->info;
	}




	if(info->reset_interval != TIMEMON_NEVER)
	{
		if(info->next_reset < now)
		{
			//do reset
			//iam = (info_and_maps*)get_string_map_element_with_hashed_key(id_map, info->hashed_id);
			iam = (info_and_maps*)info->iam;
			if(iam != NULL) /* should never be null, but let's be sure */
			{
				handle_interval_reset(iam, now);
				ip_map = iam->ip_map;
			}
			else
			{
				/* even in case of malloc failure or weird error we can update these params */
				info->current_timeusage = 0;
				info->next_reset = get_next_reset_time(info, now, info->previous_reset);
			}
		}
	}

	if(info->type == TIMEMON_COMBINED)
	{
		if(iam == NULL)
		{
			//iam = (info_and_maps*)get_string_map_element_with_hashed_key(id_map, info->hashed_id);
			iam = (info_and_maps*)info->iam;
			if(iam != NULL)
			{
				ip_map = iam->ip_map;
			}
		}
		if(ip_map != NULL) /* if this ip_map != NULL iam can never be NULL, so we don't need to check this */
		{
			
			if(info->combined_tm == NULL)
			{
				tms[0] = initialize_map_entries_for_ip(iam, 0, TIMEMON_INTERVAL_DURATION);
				info->last_record_time = now;
			}
			else
			{
				tms[0] = info->combined_tm;
				// We have a 5 second leniency on packets, so if we are in that window, don't increment
				if(info->last_record_time < (now - TIMEMON_INTERVAL_DURATION))
				{
					*(tms[0]) = ADD_UP_TO_MAX(*(tms[0]), (uint64_t)TIMEMON_INTERVAL_DURATION, is_check);
					info->last_record_time = now;
				}
			}
		}
		else
		{
			#ifdef TIMEMON_DEBUG
				printk("error: ip_map is null in match!\n");
			#endif
		}
		if(info->last_record_time < (now - TIMEMON_INTERVAL_DURATION))
		{
			info->current_timeusage = ADD_UP_TO_MAX(info->current_timeusage, (uint64_t)TIMEMON_INTERVAL_DURATION, is_check);
			info->last_record_time = now;
		}
	}
	else
	{
		uint32_t tm_ip, tm_ip_index;
		uint32_t tm_ips[2] = {0, 0};
		struct iphdr* iph = (struct iphdr*)(skb_network_header(skb));
		if(info->type == TIMEMON_INDIVIDUAL_SRC)
		{
			//src ip
			tm_ips[0] = iph->saddr;
			if(do_src_dst_swap)
			{
				tm_ips[0] = iph->daddr;
			}
		}
		else if (info->type == TIMEMON_INDIVIDUAL_DST)
		{
			//dst ip
			tm_ips[0] = iph->daddr;
			if(do_src_dst_swap)
			{
				tm_ips[0] = iph->saddr;
			}
		}
		else if(info->type ==  TIMEMON_INDIVIDUAL_LOCAL ||  info->type == TIMEMON_INDIVIDUAL_REMOTE)
		{
			//remote or local ip -- need to test both src && dst
			uint32_t src_ip = iph->saddr;
			uint32_t dst_ip = iph->daddr;
			if(info->type == TIMEMON_INDIVIDUAL_LOCAL)
			{
				tm_ips[0] = ((info->local_subnet_mask & src_ip) == info->local_subnet) ? src_ip : 0;
				tm_ips[1] = ((info->local_subnet_mask & dst_ip) == info->local_subnet) ? dst_ip : 0;
			}
			else if(info->type == TIMEMON_INDIVIDUAL_REMOTE)
			{
				tm_ips[0] = ((info->local_subnet_mask & src_ip) != info->local_subnet ) ? src_ip : 0;
				tm_ips[1] = ((info->local_subnet_mask & dst_ip) != info->local_subnet ) ? dst_ip : 0;
			}
		}
		
		if(ip_map == NULL)
		{
			//iam = (info_and_maps*)get_string_map_element_with_hashed_key(id_map, info->hashed_id);
			iam = (info_and_maps*)info->iam;
			if(iam != NULL)
			{
				ip_map = iam->ip_map;
			}	
		}
		if(!is_check && info->cmp == TIMEMON_MONITOR)
		{
			uint64_t* combined_oldval = info->combined_tm;
			if(combined_oldval == NULL)
			{
				combined_oldval = initialize_map_entries_for_ip(iam, 0, (uint64_t)TIMEMON_INTERVAL_DURATION);
			}
			else
			{
				if(info->last_record_time < (now - TIMEMON_INTERVAL_DURATION))
				{
					*combined_oldval = ADD_UP_TO_MAX(*combined_oldval, (uint64_t)TIMEMON_INTERVAL_DURATION, is_check);
					info->last_record_time = now;
				}
			}
		}
		tm_ip_index = tm_ips[0] == 0 ? 1 : 0;
		tm_ip = tm_ips[tm_ip_index];
		if(tm_ip != 0 && ip_map != NULL)
		{
			uint64_t* oldval = get_long_map_element(ip_map, (unsigned long)tm_ip);
			if(oldval == NULL)
			{
				if(!is_check)
				{
					/* may return NULL on malloc failure but that's ok */
					oldval = initialize_map_entries_for_ip(iam, (unsigned long)tm_ip, (uint64_t)TIMEMON_INTERVAL_DURATION);
				}
			}
			else
			{
				if(info->last_record_time < (now - TIMEMON_INTERVAL_DURATION))
				{
					*oldval = ADD_UP_TO_MAX(*oldval, (uint64_t)TIMEMON_INTERVAL_DURATION, is_check);
					info->last_record_time = now;
				}
			}
			
			/* this is fine, setting tms[tm_ip_index] to NULL on check for undefined value or kmalloc failure won't crash anything */
			tms[tm_ip_index] = oldval;
		}
		
	}

	match_found = 0;
	if(info->cmp != TIMEMON_MONITOR)
	{
		if(info->cmp == TIMEMON_GT)
		{
			match_found = tms[0] != NULL ? ( *(tms[0]) > info->timeusage_cutoff ? 1 : match_found ) : match_found;
			match_found = tms[1] != NULL ? ( *(tms[1]) > info->timeusage_cutoff ? 1 : match_found ) : match_found;
			match_found = info->current_timeusage > info->timeusage_cutoff ? 1 : match_found;
		}
		else if(info->cmp == TIMEMON_LT)
		{
			match_found = tms[0] != NULL ? ( *(tms[0]) < info->timeusage_cutoff ? 1 : match_found ) : match_found;
			match_found = tms[1] != NULL ? ( *(tms[1]) < info->timeusage_cutoff ? 1 : match_found ) : match_found;
			match_found = info->current_timeusage < info->timeusage_cutoff ? 1 : match_found;
		}
	}
		
	spin_unlock_bh(&timemon_lock);

	#ifdef TIMEMON_DEBUG
		printf("Current Usage: %lld\n", info->current_timeusage);
		printf("Cutoff: %lld\n", info->timeusage_cutoff);
		printf("Match? %d\n",match_found);
	#endif

	return match_found;
}










/**********************
 * Get functions
 *********************/

#define MAX_IP_STR_LENGTH 16

#define ERROR_NONE 0
#define ERROR_NO_ID 1
#define ERROR_BUFFER_TOO_SHORT 2
#define ERROR_NO_HISTORY 3
#define ERROR_UNKNOWN 4
typedef struct get_req_struct 
{
	uint32_t ip;
	uint32_t next_ip_index;
	unsigned char return_history;
	char id[TIMEMON_MAX_ID_LENGTH];
} get_request;

static unsigned long* output_ip_list = NULL;
static unsigned long output_ip_list_length = 0;

static char add_ip_block(	uint32_t ip, 
			unsigned char full_history_requested,
			info_and_maps* iam,
			unsigned char* output_buffer, 
			uint32_t* current_output_index, 
			uint32_t buffer_length 
			);
static void parse_get_request(unsigned char* request_buffer, get_request* parsed_request);
static int handle_get_failure(int ret_value, int unlock_user_sem, int unlock_timemon_spin, unsigned char error_code, unsigned char* out_buffer, unsigned char* free_buffer );


/* 
 * returns whether we succeeded in adding ip block, 0= success, 
 * otherwise error code of problem that we found
 */
static char add_ip_block(	uint32_t ip, 
				unsigned char full_history_requested,
				info_and_maps* iam,
				unsigned char* output_buffer, 
				uint32_t* current_output_index, 
				uint32_t output_buffer_length 
				)
{
	#ifdef TIMEMON_DEBUG
		uint32_t *ipp = &ip;
		printk("doing output for ip = %u.%u.%u.%u\n", *((unsigned char*)ipp), *(((unsigned char*)ipp)+1), *(((unsigned char*)ipp)+2), *(((unsigned char*)ipp)+3) );
	#endif

	if(full_history_requested)
	{
		tm_history* history = NULL;
		if(history == NULL)
		{
			#ifdef TIMEMON_DEBUG
				printk("  no history map for ip, dumping latest value in history format\n" );
			#endif


			uint32_t block_length = (2*4) + (3*8);
			uint64_t *tm;

			if(*current_output_index + block_length > output_buffer_length)
			{
				return ERROR_BUFFER_TOO_SHORT;
			}
			*( (uint32_t*)(output_buffer + *current_output_index) ) = ip;
			*current_output_index = *current_output_index + 4;
	
			*( (uint32_t*)(output_buffer + *current_output_index) ) = 1;
			*current_output_index = *current_output_index + 4;

			*( (uint64_t*)(output_buffer + *current_output_index) ) = (uint64_t)iam->info->previous_reset + (60 * local_minutes_west);
			*current_output_index = *current_output_index + 8;

			*( (uint64_t*)(output_buffer + *current_output_index) ) = (uint64_t)iam->info->previous_reset + (60 * local_minutes_west);
			*current_output_index = *current_output_index + 8;

			*( (uint64_t*)(output_buffer + *current_output_index) ) = (uint64_t)iam->info->previous_reset + (60 * local_minutes_west);
			*current_output_index = *current_output_index + 8;

			tm = (uint64_t*)get_long_map_element(iam->ip_map, ip);
			if(tm == NULL)
			{
				*( (uint64_t*)(output_buffer + *current_output_index) ) = 0;
			}
			else
			{
				*( (uint64_t*)(output_buffer + *current_output_index) ) = *tm;
			}
			*current_output_index = *current_output_index + 8;

		}
		else
		{
			uint32_t block_length = (2*4) + (3*8) + (8*history->num_nodes);
			uint64_t last_reset;
			uint32_t node_num;
			uint32_t next_index;

			if(*current_output_index + block_length > output_buffer_length)
			{
				return ERROR_BUFFER_TOO_SHORT;
			}
		
			*( (uint32_t*)(output_buffer + *current_output_index) ) = ip;
			*current_output_index = *current_output_index + 4;
	
			*( (uint32_t*)(output_buffer + *current_output_index) )= history->num_nodes;
			*current_output_index = *current_output_index + 4;

			
			
			/* need to return times in regular UTC not the UTC - minutes west, which is useful for processing */
			last_reset = (uint64_t)iam->info->previous_reset + (60 * local_minutes_west);
			*( (uint64_t*)(output_buffer + *current_output_index) ) = history->first_start > 0 ? (uint64_t)history->first_start + (60 * local_minutes_west) : last_reset;
			#ifdef TIMEMON_DEBUG
				printk("  dumping first start = %lld\n", *( (uint64_t*)(output_buffer + *current_output_index) )   );
			#endif
			*current_output_index = *current_output_index + 8;



			*( (uint64_t*)(output_buffer + *current_output_index) ) = history->first_end > 0 ?   (uint64_t)history->first_end + (60 * local_minutes_west) : last_reset;
			#ifdef TIMEMON_DEBUG
				printk("  dumping first end   = %lld\n", *( (uint64_t*)(output_buffer + *current_output_index) )   );
			#endif
			*current_output_index = *current_output_index + 8;



			*( (uint64_t*)(output_buffer + *current_output_index) ) = history->last_end > 0 ?    (uint64_t)history->last_end + (60 * local_minutes_west) : last_reset;
			#ifdef TIMEMON_DEBUG
				printk("  dumping last end    = %lld\n", *( (uint64_t*)(output_buffer + *current_output_index) )   );
			#endif
			*current_output_index = *current_output_index + 8;



			node_num = 0;
			next_index = history->num_nodes == history->max_nodes ? history->current_index+1 : 0;
			next_index = next_index >= history->max_nodes ? 0 : next_index;
			for(node_num=0; node_num < history->num_nodes; node_num++)
			{
				*( (uint64_t*)(output_buffer + *current_output_index) ) = (history->history_data)[ next_index ];
				*current_output_index = *current_output_index + 8;
				next_index = (next_index + 1) % history->max_nodes;
			}
		}
	}
	else
	{
		uint64_t *tm;
		if(*current_output_index + 8 > output_buffer_length)
		{
			return ERROR_BUFFER_TOO_SHORT;
		}

		*( (uint32_t*)(output_buffer + *current_output_index) ) = ip;
		*current_output_index = *current_output_index + 4;


		tm = (uint64_t*)get_long_map_element(iam->ip_map, ip);
		if(tm == NULL)
		{
			*( (uint64_t*)(output_buffer + *current_output_index) ) = 0;
		}
		else
		{
			*( (uint64_t*)(output_buffer + *current_output_index) ) = *tm;
		}
		*current_output_index = *current_output_index + 8; 
	}
	return ERROR_NONE;
}



/* 
 * convenience method for cleaning crap up after failed malloc or other 
 * error that we can't recover  from in get function
 */
static int handle_get_failure(int ret_value, int unlock_user_sem, int unlock_timemon_spin, unsigned char error_code, unsigned char* out_buffer, unsigned char* free_buffer )
{
	copy_to_user(out_buffer, &error_code, 1);
	if( free_buffer != NULL ) { kfree(free_buffer); }
	if(unlock_timemon_spin) { spin_unlock_bh(&timemon_lock); }
	if(unlock_user_sem) { up(&userspace_lock); }
	return ret_value;
}

/* 
 * request structure: 
 * bytes 1:4 is ip (uint32_t)
 * bytes 4:8 is the next ip index (uint32_t)
 * byte  9   is whether to return full history or just current usage (unsigned char)
 * bytes 10:10+MAX_ID_LENGTH are the id (a string)
 */
static void parse_get_request(unsigned char* request_buffer, get_request* parsed_request)
{
	uint32_t* ip = (uint32_t*)(request_buffer+0);
	uint32_t* next_ip_index = (uint32_t*)(request_buffer+4);
	unsigned char* return_history = (unsigned char*)(request_buffer+8);

	

	parsed_request->ip = *ip;
	parsed_request->next_ip_index = *next_ip_index;
	parsed_request->return_history = *return_history;
	memcpy(parsed_request->id, request_buffer+9, TIMEMON_MAX_ID_LENGTH);
	(parsed_request->id)[TIMEMON_MAX_ID_LENGTH-1] = '\0'; /* make sure id is null terminated no matter what */
	
	#ifdef TIMEMON_DEBUG
		printk("ip = %u.%u.%u.%u\n", *((char*)ip), *(((char*)ip)+1), *(((char*)ip)+2), *(((char*)ip)+3) );
		printk("next ip index = %d\n", *next_ip_index);
		printk("return_history = %d\n", *return_history);
	#endif
}


static int ipt_timemon_get_ctl(struct sock *sk, int cmd, void *user, int *len)
{
	/* check for timezone shift & adjust if necessary */
	char* buffer;
	get_request query;
	info_and_maps* iam;

	unsigned char* error;
	uint32_t* total_ips;
	uint32_t* start_index;
	uint32_t* num_ips_in_response;
	uint64_t* reset_interval;
	uint64_t* reset_time;
	unsigned char* reset_is_constant_interval;
	uint32_t  current_output_index;
	time_t now = get_seconds();
	check_for_timezone_shift(now, 0);
	check_for_backwards_time_shift(now);
	now = now -  local_seconds_west;  /* Adjust for local timezone */
	

	down(&userspace_lock);
	
	
	/* first check that query buffer is big enough to hold the info needed to parse the query */
	if(*len < TIMEMON_MAX_ID_LENGTH + 9)
	{

		return handle_get_failure(0, 1, 0, ERROR_BUFFER_TOO_SHORT, user, NULL);
	}
	
	

	/* copy the query from userspace to kernel space & parse */
	buffer = kmalloc(*len, GFP_ATOMIC);
	if(buffer == NULL) /* check for malloc failure */
	{
		return handle_get_failure(0, 1, 0, ERROR_UNKNOWN, user, NULL);
	}
	copy_from_user(buffer, user, *len);
	parse_get_request(buffer, &query);
	


	
	
	
	/* 
	 * retrieve data for this id and verify all variables are properly defined, just to be sure
	 * this is a kernel module -- it pays to be paranoid! 
	 */
	spin_lock_bh(&timemon_lock);
	
	iam = (info_and_maps*)get_string_map_element(id_map, query.id);
	
	if(iam == NULL)
	{
		return handle_get_failure(0, 1, 1, ERROR_NO_ID, user, buffer);
	}
	if(iam->info == NULL || iam->ip_map == NULL)
	{
		return handle_get_failure(0, 1, 1, ERROR_NO_ID, user, buffer);
	}
	
	/* allocate ip list if this is first query */
	if(query.next_ip_index == 0 && query.ip == 0)
	{
		if(output_ip_list != NULL)
		{
			kfree(output_ip_list);
		}
		if(iam->info->type == TIMEMON_COMBINED)
		{
			output_ip_list_length = 1;
			output_ip_list = (unsigned long*)kmalloc(sizeof(unsigned long), GFP_ATOMIC);
			if(output_ip_list != NULL) { *output_ip_list = 0; }
		}
		else
		{
			output_ip_list = get_sorted_long_map_keys(iam->ip_map, &output_ip_list_length);
		}
		
		if(output_ip_list == NULL)
		{
			return handle_get_failure(0, 1, 1, ERROR_UNKNOWN, user, buffer);
		}
	}

	/* if this is not first query do a sanity check -- make sure it's within bounds of allocated ip list */
	if(query.next_ip_index > 0 && (output_ip_list == NULL || query.next_ip_index > output_ip_list_length))
	{
		return handle_get_failure(0, 1, 1, ERROR_UNKNOWN, user, buffer);
	}




	/*
	// values only reset when a packet hits a rule, so 
	// reset may have expired without data being reset.
	// So, test if we need to reset values to zero 
	*/
	if(iam->info->reset_interval != TIMEMON_NEVER)
	{
		if(iam->info->next_reset < now)
		{
			//do reset
			handle_interval_reset(iam, now);
		}
	}



	/* compute response & store it in buffer
	 *
	 * format of response:
	 * byte 1 : error code (0 for ok)
	 * bytes 2-5 : total_num_ips found in query (further gets may be necessary to retrieve them)
	 * bytes 6-9 : start_index, index (in a list of total_num_ips) of first ip in response
	 * bytes 10-13 : num_ips_in_response, number of ips in this response
	 * bytes 14-21 : reset_interval (helps deal with DST shifts in userspace)
	 * bytes 22-29 : reset_time (helps deal with DST shifts in userspace)
	 * byte  30    : reset_is_constant_interval (helps deal with DST shifts in userspace)
	 * remaining bytes contain blocks of ip data
	 * format is dependent on whether history was queried
	 * 
	 * if history was NOT queried we have
	 * bytes 1-4 : ip
	 * bytes 5-12 : timeusage
	 *
	 * if history WAS queried we have
	 *   (note we are using 64 bit integers for time here
	 *   even though time_t is 32 bits on most 32 bit systems
	 *   just to be on the safe side)
	 * bytes 1-4 : ip
	 * bytes 4-8 : history_length number of history values (including current)
	 * bytes 9-16 : first start
	 * bytes 17-24 : first end
	 * bytes 25-32 : recent end 
	 * 33 onward : list of 64 bit integers of length history_length
	 *
	 */
	error = buffer;
	total_ips = (uint32_t*)(buffer+1);
	start_index = (uint32_t*)(buffer+5);
	num_ips_in_response = (uint32_t*)(buffer+9);
	reset_interval = (uint64_t*)(buffer+13);
	reset_time = (uint64_t*)(buffer+21);
	reset_is_constant_interval = (char*)(buffer+29);

	*reset_interval = (uint64_t)iam->info->reset_interval;
	*reset_time = (uint64_t)iam->info->reset_time;
	*reset_is_constant_interval = iam->info->reset_is_constant_interval;

	current_output_index = 30;
	if(query.ip != 0)
	{
		*error = add_ip_block(	query.ip, 
					query.return_history,
					iam,
					buffer, 
					&current_output_index, 
					*len 
					);

		*total_ips = *error == 0;
		*start_index = 0;
		*num_ips_in_response = *error == 0 ? 1 : 0;
	}
	else
	{
		uint32_t next_index = query.next_ip_index;
		*error = ERROR_NONE;
		*total_ips = output_ip_list_length;
		*start_index = next_index;
		*num_ips_in_response = 0;
		while(*error == ERROR_NONE && next_index < output_ip_list_length)
		{
			uint32_t next_ip = output_ip_list[next_index];
			*error = add_ip_block(	next_ip, 
						query.return_history,
						iam,
						buffer, 
						&current_output_index, 
						*len
						);
			if(*error == ERROR_NONE)
			{
				*num_ips_in_response = *num_ips_in_response + 1;
				next_index++;
			}
		}
		if(*error == ERROR_BUFFER_TOO_SHORT && *num_ips_in_response > 0)
		{
			*error = ERROR_NONE;
		}
		if(next_index == output_ip_list_length)
		{
			kfree(output_ip_list);
			output_ip_list = NULL;
			output_ip_list_length = 0;
		}
	}

	spin_unlock_bh(&timemon_lock);
	
	copy_to_user(user, buffer, *len);
	kfree(buffer);



	up(&userspace_lock);


	return 0;
}





/********************
 * Set functions
 ********************/

typedef struct set_header_struct
{
	uint32_t total_ips;
	uint32_t next_ip_index;
	uint32_t num_ips_in_buffer;
	unsigned char history_included;
	unsigned char zero_unset_ips;
	time_t last_backup;
	char id[TIMEMON_MAX_ID_LENGTH];
} set_header;

static int handle_set_failure(int ret_value, int unlock_user_sem, int unlock_timemon_spin, unsigned char* free_buffer );
static void parse_set_header(unsigned char* input_buffer, set_header* header);
static void set_single_ip_data(unsigned char history_included, info_and_maps* iam, unsigned char* buffer, uint32_t* buffer_index, time_t now);

static int handle_set_failure(int ret_value, int unlock_user_sem, int unlock_timemon_spin, unsigned char* free_buffer )
{
	if( free_buffer != NULL ) { kfree(free_buffer); }
	set_in_progress = 0;
	if(unlock_timemon_spin) { spin_unlock_bh(&timemon_lock); }
	if(unlock_user_sem) { up(&userspace_lock); }
	return ret_value;
}

static void parse_set_header(unsigned char* input_buffer, set_header* header)
{
	/* 
	 * set header structure:
	 * bytes 1-4   :  total_ips being set in this and subsequent requests
	 * bytes 5-8   :  next_ip_index, first ip being set in this set command
	 * bytes 9-12  :  num_ips_in_buffer, the number of ips in this set request
	 * byte 13     :  history_included (whether history data is included, or just current data)
	 * byte 14     :  zero_unset_ips, whether to zero all ips not included in this and subsequent requests
	 * bytes 15-22 :  last_backup time (64 bit)
	 * bytes 23-23+TIMEMON_MAX_ID_LENGTH : id
	 * bytes 23+   :  ip data
	 */

	uint32_t* total_ips = (uint32_t*)(input_buffer+0);
	uint32_t* next_ip_index = (uint32_t*)(input_buffer+4);
	uint32_t* num_ips_in_buffer = (uint32_t*)(input_buffer+8);
	unsigned char* history_included = (unsigned char*)(input_buffer+12);
	unsigned char* zero_unset_ips = (unsigned char*)(input_buffer+13);
	uint64_t* last_backup = (uint64_t*)(input_buffer+14);


	header->total_ips = *total_ips;
	header->next_ip_index = *next_ip_index;
	header->num_ips_in_buffer = *num_ips_in_buffer;
	header->history_included = *history_included;
	header->zero_unset_ips = *zero_unset_ips;
	header->last_backup = (time_t)*last_backup;
	memcpy(header->id, input_buffer+22, TIMEMON_MAX_ID_LENGTH);
	(header->id)[TIMEMON_MAX_ID_LENGTH-1] = '\0'; /* make sure id is null terminated no matter what */

	#ifdef TIMEMON_DEBUG
		printk("parsed set header:\n");
		printk("  total_ips         = %d\n", header->total_ips);
		printk("  next_ip_index     = %d\n", header->next_ip_index);
		printk("  num_ips_in_buffer = %d\n", header->num_ips_in_buffer);
		printk("  zero_unset_ips    = %d\n", header->zero_unset_ips);
		printk("  last_backup       = %ld\n", header->last_backup);
		printk("  id                = %s\n", header->id);
	#endif
}
static void set_single_ip_data(unsigned char history_included, info_and_maps* iam, unsigned char* buffer, uint32_t* buffer_index, time_t now)
{
	/* 
	 * note that times stored within the module are adjusted so they are equal to seconds 
	 * since unix epoch that corrosponds to the UTC wall-clock time (timezone offset 0) 
	 * that is equal to the wall-clock time in the current time-zone.  Incoming values must 
	 * be adjusted similarly
	 */
	uint32_t ip = *( (uint32_t*)(buffer + *buffer_index) );
			
	#ifdef TIMEMON_DEBUG
		uint32_t* ipp = &ip;
		printk("doing set for ip = %u.%u.%u.%u\n", *((unsigned char*)ipp), *(((unsigned char*)ipp)+1), *(((unsigned char*)ipp)+2), *(((unsigned char*)ipp)+3) );
		printk("ip index = %d\n", *buffer_index);
	#endif

	if(history_included)
	{
		uint32_t num_history_nodes = *( (uint32_t*)(buffer + *buffer_index+4));
		uint64_t tm;
		*buffer_index = *buffer_index + (2*4) + (3*8) + ((num_history_nodes-1)*8);
		tm = *( (uint64_t*)(buffer + *buffer_index));
		initialize_map_entries_for_ip(iam, ip, tm); /* automatically frees existing values if they exist */
		*buffer_index = *buffer_index + 8;
		if(ip == 0)
		{
			iam->info->current_timeusage = tm;
		}
	}
	else
	{
		uint64_t tm = *( (uint64_t*)(buffer + *buffer_index+4) );
		#ifdef TIMEMON_DEBUG
			printk("  setting tm to %lld\n", tm );
		#endif

		
		initialize_map_entries_for_ip(iam, ip, tm); /* automatically frees existing values if they exist */
		*buffer_index = *buffer_index + 12;

		if(ip == 0)
		{
			iam->info->current_timeusage = tm;
		}
	}
	

}

static int ipt_timemon_set_ctl(struct sock *sk, int cmd, void *user, u_int32_t len)
{
	/* check for timezone shift & adjust if necessary */
	char* buffer;
	set_header header;
	info_and_maps* iam;
	uint32_t buffer_index;
	uint32_t next_ip_index;
	time_t now = get_seconds();
	check_for_timezone_shift(now, 0);
	check_for_backwards_time_shift(now);
	now = now -  local_seconds_west;  /* Adjust for local timezone */


	/* just return right away if user buffer is too short to contain even the header */
	if(len < (3*4) + 2 + 8 + TIMEMON_MAX_ID_LENGTH)
	{
		#ifdef TIMEMON_DEBUG
			printk("set error: buffer not large enough!\n");
		#endif
		return 0;
	}

	down(&userspace_lock);
	set_in_progress = 1;
	
	buffer = kmalloc(len, GFP_ATOMIC);
	if(buffer == NULL) /* check for malloc failure */
	{
		return handle_set_failure(0, 1, 0, NULL);
	}
	copy_from_user(buffer, user, len);
	parse_set_header(buffer, &header);

	
	
	
	/* 
	 * retrieve data for this id and verify all variables are properly defined, just to be sure
	 * this is a kernel module -- it pays to be paranoid! 
	 */
	spin_lock_bh(&timemon_lock);
	

	iam = (info_and_maps*)get_string_map_element(id_map, header.id);
	if(iam == NULL)
	{
		return handle_set_failure(0, 1, 1, buffer);
	}
	if(iam->info == NULL || iam->ip_map == NULL)
	{
		return handle_set_failure(0, 1, 1, buffer);
	}

	/* 
	 * during set unconditionally set combined_tm to NULL 
	 * if combined data (ip=0) exists after set exits cleanly, we will restore it
	 */
	iam->info->combined_tm = NULL;

	//if zero_unset_ips == 1 && next_ip_index == 0
	//then clear data for all ips for this id
	if(header.zero_unset_ips && header.next_ip_index == 0)
	{
		//clear data
		while(iam->ip_map->num_elements > 0)
		{
			unsigned long key;
			uint64_t *tm = remove_smallest_long_map_element(iam->ip_map, &key);
			kfree(tm);
		}
	}

	/* 
	 * last_backup parameter is only relevant for case where we are not setting history
	 * and when we don't have a constant interval length or a specified reset_time (since in this case start time gets reset when rule is inserted and there is therefore no constant end)
	 * If num_intervals_to_save =0 and is_constant_interval=0, check it.  If it's nonzero (0=ignore) and invalid, return.
	 */
	if(header.last_backup > 0 && (iam->info->reset_is_constant_interval == 0 || iam->info->reset_time != 0) )
	{
		time_t adjusted_last_backup_time = header.last_backup - (60 * local_minutes_west); 
		time_t next_reset_of_last_backup = get_next_reset_time(iam->info, adjusted_last_backup_time, adjusted_last_backup_time);
		if(next_reset_of_last_backup != iam->info->next_reset)
		{
			return handle_set_failure(0, 1, 1, buffer);
		}
	}


	/*
	 * iterate over each ip block in buffer, 
	 * loading data into necessary kerenel-space data structures
	*/
	buffer_index = (3*4) + 1 + 1 + 8 + TIMEMON_MAX_ID_LENGTH;
	next_ip_index = header.next_ip_index;
	
	while(next_ip_index < header.num_ips_in_buffer)
	{
		set_single_ip_data(header.history_included, iam, buffer, &buffer_index, now);
		next_ip_index++;
	}

	if (next_ip_index == header.total_ips)
	{
		set_in_progress = 0;
	}

	/* set combined_tm */
	iam->info->combined_tm = (uint64_t*)get_long_map_element(iam->ip_map, 0);

	kfree(buffer);
	spin_unlock_bh(&timemon_lock);
	up(&userspace_lock);
	return 0;
}
static int checkentry(const struct xt_mtchk_param *par)
{


	struct ipt_timemon_info *info = (struct ipt_timemon_info*)(par->matchinfo);



	#ifdef TIMEMON_DEBUG
		printk("checkentry called\n");	
	#endif
	




	if(info->ref_count == NULL) /* first instance, we're inserting rule */
	{
		struct ipt_timemon_info *master_info = (struct ipt_timemon_info*)kmalloc(sizeof(struct ipt_timemon_info), GFP_ATOMIC);
		info->ref_count = (unsigned long*)kmalloc(sizeof(unsigned long), GFP_ATOMIC);

		if(info->ref_count == NULL) /* deal with kmalloc failure */
		{
			printk("ipt_timemon: kmalloc failure in checkentry!\n");
			return 0;
		}
		*(info->ref_count) = 1;
		info->non_const_self = master_info;
		info->hashed_id = sdbm_string_hash(info->id);
		info->iam = NULL;
		info->combined_tm = NULL;

		memcpy(master_info->id, info->id, TIMEMON_MAX_ID_LENGTH);
		master_info->type                       = info->type;
		master_info->check_type                 = info->check_type;
		master_info->local_subnet               = info->local_subnet;
		master_info->local_subnet_mask          = info->local_subnet_mask;
		master_info->cmp                        = info->cmp;
		master_info->reset_is_constant_interval = info->reset_is_constant_interval;
		master_info->reset_interval             = info->reset_interval;
		master_info->reset_time                 = info->reset_time;
		master_info->timeusage_cutoff           = info->timeusage_cutoff;
		master_info->current_timeusage          = info->current_timeusage;
		master_info->next_reset                 = info->next_reset;
		master_info->previous_reset             = info->previous_reset;
		master_info->last_backup_time           = info->last_backup_time;
		
		master_info->hashed_id                  = info->hashed_id;
		master_info->iam                        = info->iam;
		master_info->combined_tm                = info->combined_tm;
		master_info->non_const_self             = info->non_const_self;
		master_info->ref_count                  = info->ref_count;

		#ifdef TIMEMON_DEBUG
			printk("   after increment, ref count = %ld\n", *(info->ref_count) );
		#endif

		if(info->cmp != TIMEMON_CHECK)
		{
			info_and_maps *iam;
		
			down(&userspace_lock);
			spin_lock_bh(&timemon_lock);
			

	
			iam = (info_and_maps*)get_string_map_element(id_map, info->id);
			if(iam != NULL)
			{
				printk("ipt_timemon: error, \"%s\" is a duplicate id\n", info->id); 
				spin_unlock_bh(&timemon_lock);
				up(&userspace_lock);
				return 0;
			}

			if(info->reset_interval != TIMEMON_NEVER)
			{
				time_t now = get_seconds();
				if(now != last_local_mw_update )
				{
					check_for_timezone_shift(now, 1);
				}
				
				
				now = now -  (60 * local_minutes_west);  /* Adjust for local timezone */
				info->previous_reset = now;
				master_info->previous_reset = now;
				if(info->next_reset == 0)
				{
					info->next_reset = get_next_reset_time(info, now, now);
					master_info->next_reset = info->next_reset;
					/* 
					 * if we specify last backup time, check that next reset is consistent, 
					 * otherwise reset current_timeusage to 0 
					 * 
					 * only applies to combined type -- otherwise we need to handle setting timeusage
					 * through userspace library
					 */
					if(info->last_backup_time != 0 && info->type == TIMEMON_COMBINED)
					{
						time_t adjusted_last_backup_time = info->last_backup_time - (60 * local_minutes_west); 
						time_t next_reset_of_last_backup = get_next_reset_time(info, adjusted_last_backup_time, adjusted_last_backup_time);
						if(next_reset_of_last_backup != info->next_reset)
						{
							info->current_timeusage = 0;
							master_info->current_timeusage = 0;
						}
						info->last_backup_time = 0;
						master_info->last_backup_time = 0;
					}
				}
			}
	
			iam = (info_and_maps*)kmalloc( sizeof(info_and_maps), GFP_ATOMIC);
			if(iam == NULL) /* handle kmalloc failure */
			{
				printk("ipt_timemon: kmalloc failure in checkentry!\n");
				spin_unlock_bh(&timemon_lock);
				up(&userspace_lock);
				return 0;
			}
			iam->ip_map = initialize_long_map();
			if(iam->ip_map == NULL) /* handle kmalloc failure */
			{
				printk("ipt_timemon: kmalloc failure in checkentry!\n");
				spin_unlock_bh(&timemon_lock);
				up(&userspace_lock);
				return 0;
			}
			iam->ip_history_map = NULL;


			iam->info = master_info;
			set_string_map_element(id_map, info->id, iam);

			info->iam = (void*)iam;
			master_info->iam = (void*)iam;


			spin_unlock_bh(&timemon_lock);
			up(&userspace_lock);
		}
	}
	
	else
	{
		/* info->non_const_self = info; */


		*(info->ref_count) = *(info->ref_count) + 1;
		#ifdef TIMEMON_DEBUG
			printk("   after increment, ref count = %ld\n", *(info->ref_count) );
		#endif
		

		/*
		if(info->cmp != BANDWIDTH_CHECK)
		{
			info_and_maps* iam;
			down(&userspace_lock);
			spin_lock_bh(&bandwidth_lock);
			iam = (info_and_maps*)get_string_map_element(id_map, info->id);
			if(iam != NULL)
			{
				iam->info = info;
			}
			spin_unlock_bh(&bandwidth_lock);
			up(&userspace_lock);
		}
		*/
	}
	
	#ifdef TIMEMON_DEBUG
		printk("checkentry complete\n");
	#endif
	return 0;
}

static void destroy(const struct xt_mtdtor_param *par)
{

	struct ipt_timemon_info *info = (struct ipt_timemon_info*)(par->matchinfo);

	#ifdef TIMEMON_DEBUG
		printk("destroy called\n");
	#endif
	
	*(info->ref_count) = *(info->ref_count) - 1;
	
	#ifdef TIMEMON_DEBUG
		printk("   after decrement refcount = %ld\n", *(info->ref_count));
	#endif
	
	if(*(info->ref_count) == 0)
	{
		info_and_maps* iam;
		down(&userspace_lock);
		spin_lock_bh(&timemon_lock);
		
		info->combined_tm = NULL;
		iam = (info_and_maps*)remove_string_map_element(id_map, info->id);
		if(iam != NULL && info->cmp != TIMEMON_CHECK)
		{
			unsigned long num_destroyed;
			if(iam->ip_map != NULL && iam->ip_history_map != NULL)
			{
				unsigned long history_index = 0;
				tm_history** histories_to_free;
				
				destroy_long_map(iam->ip_map, DESTROY_MODE_IGNORE_VALUES, &num_destroyed);
				
				histories_to_free = (tm_history**)destroy_long_map(iam->ip_history_map, DESTROY_MODE_RETURN_VALUES, &num_destroyed);
				
				/* num_destroyed will be 0 if histories_to_free is null after malloc failure, so this is safe */
				for(history_index = 0; history_index < num_destroyed; history_index++) 
				{
					tm_history* h = histories_to_free[history_index];
					if(h != NULL)
					{
						kfree(h->history_data);
						kfree(h);
					}
				}
				
			}
			else if(iam->ip_map != NULL)
			{
				destroy_long_map(iam->ip_map, DESTROY_MODE_FREE_VALUES, &num_destroyed);
			}
			kfree(iam);
			/* info portion of iam gets taken care of automatically */
		}	
		kfree(info->ref_count);
		kfree(info->non_const_self);

		spin_unlock_bh(&timemon_lock);
		up(&userspace_lock);
	}
	
	#ifdef TIMEMON_DEBUG
		printk("destroy complete\n");
	#endif
}

static struct nf_sockopt_ops ipt_timemon_sockopts = 
{
	.pf = PF_INET,
	.set_optmin = TIMEMON_SET,
	.set_optmax = TIMEMON_SET+1,
	.set = ipt_timemon_set_ctl,
	.get_optmin = TIMEMON_GET,
	.get_optmax = TIMEMON_GET+1,
	.get = ipt_timemon_get_ctl
};


static struct xt_match timemon_match __read_mostly = 
{
	.name		= "timemon",
	.match		= &match,
	.family		= AF_INET,
	.matchsize	= sizeof(struct ipt_timemon_info),
	.checkentry	= &checkentry,
	.destroy	= &destroy,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	/* Register setsockopt */
	if (nf_register_sockopt(&ipt_timemon_sockopts) < 0)
	{
		printk("ipt_timemon: Can't register sockopts. Aborting\n");
	}
	timemon_record_max = get_tm_record_max();
	local_minutes_west = old_minutes_west = sys_tz.tz_minuteswest;
	local_seconds_west = local_minutes_west*60;
	last_local_mw_update = get_seconds();
	if(local_seconds_west > last_local_mw_update)
	{
		/* we can't let adjusted time be < 0 -- pretend timezone is still UTC */
		local_minutes_west = 0;
		local_seconds_west = 0;
	}

	id_map = initialize_string_map(0);
	if(id_map == NULL) /* deal with kmalloc failure */
	{
		printk("id map is null, returning -1\n");
		return -1;
	}


	return xt_register_match(&timemon_match);
}

static void __exit fini(void)
{
	down(&userspace_lock);
	spin_lock_bh(&timemon_lock);
	if(id_map != NULL)
	{
		unsigned long num_returned;
		info_and_maps **iams = (info_and_maps**)destroy_string_map(id_map, DESTROY_MODE_RETURN_VALUES, &num_returned);
		int iam_index;
		for(iam_index=0; iam_index < num_returned; iam_index++)
		{
			info_and_maps* iam = iams[iam_index];
			long_map* ip_map = iam->ip_map;
			unsigned long num_destroyed;
			destroy_long_map(ip_map, DESTROY_MODE_FREE_VALUES, &num_destroyed);
			kfree(iam);
			/* info portion of iam gets taken care of automatically */
		}
	}
	nf_unregister_sockopt(&ipt_timemon_sockopts);
	xt_unregister_match(&timemon_match);
	spin_unlock_bh(&timemon_lock);
	up(&userspace_lock);

}

module_init(init);
module_exit(fini);
